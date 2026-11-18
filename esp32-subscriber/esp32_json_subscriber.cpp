#include <mqtt/async_client.h>
#include <mqtt/ssl_options.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
using json = nlohmann::json;
using namespace std::chrono;

std::string connection_string() {
    auto password = std::getenv("QUESTDB_PASSWORD");
    if (!password || !*password) throw std::runtime_error("QUESTDB_PASSWORD is required");
    // libpq keyword values require escaping, including quotes and backslashes.
    std::string escaped;
    for (const char c : std::string(password)) {
        if (c == '\'' || c == '\\') escaped += '\\';
        escaped += c;
    }
    return "host=127.0.0.1 port=8812 dbname=qdb user=admin password='" + escaped + "'";
}

double number(const json& data, const char* name, double low, double high) {
    const auto& value = data.at(name);
    if (!value.is_number()) throw std::runtime_error(std::string(name) + " must be numeric");
    const double result = value.get<double>();
    if (!std::isfinite(result) || result < low || result > high)
        throw std::runtime_error(std::string(name) + " out of range");
    return result;
}

struct Sample { bool temperature; std::string device; long long measured; json fields; };
Sample decode(const std::string& topic, const std::string& payload) {
    if (payload.size() > 2048) throw std::runtime_error("Payload too large");
    auto data = json::parse(payload);
    const bool temp = topic == "esp32-t/data";
    if (!temp && topic != "esp32-g/data") throw std::runtime_error("Unexpected topic");
    auto device = data.at("device_id").get<std::string>();
    if (device != (temp ? "esp32-t" : "esp32-g")) throw std::runtime_error("Device/topic mismatch");
    if (!data.at("date_time").is_number_integer()) throw std::runtime_error("date_time must be Unix milliseconds");
    auto measured = data.at("date_time").get<long long>();
    auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    if (measured < 1700000000000LL || measured > now + 60000) throw std::runtime_error("Invalid measurement time");
    if (temp) {
        number(data, "temperature", -40, 80);
        number(data, "humidity", 0, 100);
    } else {
        number(data, "air_quality", 0, 4095);
        if (data.contains("gas_detected") && !data.at("gas_detected").is_boolean())
            throw std::runtime_error("gas_detected must be boolean");
    }
    return {temp, device, measured, data};
}

void store(const Sample& sample, long long received) {
    pqxx::connection db(connection_string());
    pqxx::work tx(db);
    const std::string prefix = "INSERT INTO " + std::string(sample.temperature ? "esp32_t_data" : "esp32_g_data");
    const std::string times = "cast(" + std::to_string(received) + " as timestamp)," +
        tx.quote(sample.device) + ",cast(" + std::to_string(sample.measured) + " as date),";
    if (sample.temperature) {
        tx.exec(prefix + " (ts,device_id,date_time,temperature,humidity) VALUES (" + times +
            sample.fields.at("temperature").dump() + "," + sample.fields.at("humidity").dump() + ")");
    } else {
        tx.exec(prefix + " (ts,device_id,date_time,air_quality) VALUES (" + times +
            sample.fields.at("air_quality").dump() + ")");
    }
    tx.commit();
}

int main(int argc, char** argv) {
    try {
        // Fail visibly on missing/mismatched tables rather than changing the existing schema.
        {
            pqxx::connection db(connection_string()); pqxx::work tx(db);
            tx.exec("SELECT ts,device_id,date_time,temperature,humidity FROM esp32_t_data LIMIT 0");
            tx.exec("SELECT ts,device_id,date_time,air_quality FROM esp32_g_data LIMIT 0");
            tx.exec("SELECT cast(1700000000000000 as timestamp), cast(1700000000000 as date)");
            tx.commit();
        }
        if (argc == 2 && std::string(argv[1]) == "--check-db") {
            std::cout << "Existing ESP32 database schemas are accessible" << std::endl; return 0;
        }
        mqtt::async_client client("ssl://gruppe7.vps.webdock.cloud:8883", "esp32-json-questdb-subscriber");
        mqtt::ssl_options ssl;
        ssl.set_trust_store("/etc/mosquitto/certs/ca.crt");
        ssl.set_enable_server_cert_auth(true);
        ssl.set_verify(true);
        mqtt::connect_options options;
        options.set_ssl(ssl); options.set_clean_session(true); options.set_connect_timeout(10);
        client.start_consuming();
        while (true) {
            try {
                if (!client.is_connected()) {
                    client.connect(options)->wait();
                    client.subscribe("esp32-t/data", 1)->wait();
                    client.subscribe("esp32-g/data", 1)->wait();
                    std::cout << "Subscribed to esp32-t/data and esp32-g/data over TLS" << std::endl;
                }
                mqtt::const_message_ptr message;
                if (!client.try_consume_message_for(&message, seconds(1)) || !message) continue;
                const auto received = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
                Sample sample;
                try { sample = decode(message->get_topic(), message->to_string()); }
                catch (const std::exception& e) { std::cerr << "Rejected message: " << e.what() << std::endl; continue; }
                // Hold this valid sample and retry while QuestDB is unavailable.
                while (true) {
                    try { store(sample, received); break; }
                    catch (const std::exception& e) {
                        std::cerr << "Database write failed; retrying: " << e.what() << std::endl;
                        std::this_thread::sleep_for(seconds(5));
                    }
                }
                std::cout << "Stored " << sample.device << " measured_ms=" << sample.measured << std::endl;
            } catch (const mqtt::exception& e) {
                std::cerr << "MQTT error; reconnecting: " << e.what() << std::endl;
                std::this_thread::sleep_for(seconds(5));
            }
        }
    } catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
