#include <mqtt/async_client.h>
#include <mqtt/ssl_options.h>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

using json = nlohmann::json;

// Thingy:91 X MQTT settings. The Nordic sample prefixes the publish topic with
// the client ID, so client ID "thingy91x-01" and topic "data" become
// "thingy91x-01/data".
const std::string SERVER_ADDRESS = "ssl://gruppe7.vps.webdock.cloud:8883";
const std::string CLIENT_ID = "thingy91x-questdb-subscriber";
const std::string SENSOR_TOPIC = "thingy91x-01/data";

// QuestDB PostgreSQL wire-protocol connection (QuestDB runs on this machine).
const std::string DB_CONNECTION =
    "host=localhost "
    "port=8812 "
    "dbname=qdb "
    "user=admin "
    "password=quest";

static void ensure_questdb_table()
{
    pqxx::connection conn(DB_CONNECTION);
    pqxx::work txn(conn);

    txn.exec(
        "CREATE TABLE IF NOT EXISTS thingy91x_data ("
        "ts TIMESTAMP, "
        "device_id SYMBOL, "
        "uptime_ms LONG, "
        "temperature_c DOUBLE, "
        "humidity_pct DOUBLE, "
        "pressure_kpa DOUBLE, "
        "gas_ohm DOUBLE"
        ") TIMESTAMP(ts) PARTITION BY DAY;"
    );

    txn.commit();
}

class callback : public virtual mqtt::callback
{
private:
    mqtt::async_client& client;

public:
    explicit callback(mqtt::async_client& cli)
        : client(cli)
    {
    }

    void connected(const std::string& cause) override
    {
        (void)cause;
        std::cout << "Connected to Webdock MQTT broker!" << std::endl;

        try
        {
            client.subscribe(SENSOR_TOPIC, 1);
            std::cout << "Subscribed to " << SENSOR_TOPIC << std::endl;
        }
        catch (const mqtt::exception& e)
        {
            std::cerr << "Subscribe error: " << e.what() << std::endl;
        }
    }

    void connection_lost(const std::string& cause) override
    {
        std::cerr << "MQTT connection lost" << std::endl;
        if (!cause.empty())
        {
            std::cerr << "Cause: " << cause << std::endl;
        }
    }

    void message_arrived(mqtt::const_message_ptr msg) override
    {
        try
        {
            const std::string topic = msg->get_topic();
            const std::string payload = msg->to_string();

            std::cout << "\nMQTT message on " << topic << ": "
                      << payload << std::endl;

            if (topic != SENSOR_TOPIC)
            {
                std::cerr << "Ignoring unexpected topic: " << topic << std::endl;
                return;
            }

            const json data = json::parse(payload);
            const std::string device_id = data.at("device_id").get<std::string>();
            const std::int64_t uptime_ms = data.at("uptime_ms").get<std::int64_t>();
            const double temperature_c = data.at("temperature_c").get<double>();
            const double humidity_pct = data.at("humidity_pct").get<double>();
            const double pressure_kpa = data.at("pressure_kpa").get<double>();
            const double gas_ohm = data.at("gas_ohm").get<double>();

            pqxx::connection conn(DB_CONNECTION);
            pqxx::work txn(conn);
            txn.exec_params(
                "INSERT INTO thingy91x_data "
                "(ts, device_id, uptime_ms, temperature_c, humidity_pct, pressure_kpa, gas_ohm) "
                "VALUES (now(), $1, $2, $3, $4, $5, $6);",
                device_id, uptime_ms, temperature_c, humidity_pct,
                pressure_kpa, gas_ohm
            );
            txn.commit();

            std::cout << "Inserted Thingy:91 X sensor sample into QuestDB"
                      << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error processing MQTT message: " << e.what() << std::endl;
        }
    }

    void delivery_complete(mqtt::delivery_token_ptr tok) override
    {
        (void)tok;
    }
};

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " [ca-certificate.pem]\n"
                  << "Pass the broker CA file if it is not in the system trust store.\n";
        return 2;
    }

    try
    {
        ensure_questdb_table();

        mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
        callback cb(client);
        client.set_callback(cb);

        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true);

        mqtt::ssl_options sslOpts;
        sslOpts.set_enable_server_cert_auth(true);
        if (argc == 2)
        {
            sslOpts.set_trust_store(argv[1]);
        }
        connOpts.set_ssl(sslOpts);

        std::cout << "Connecting to " << SERVER_ADDRESS << "..." << std::endl;
        client.connect(connOpts)->wait();

        // Keep the process alive while Paho handles MQTT callbacks in the background.
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::hours(1));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Subscriber error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
