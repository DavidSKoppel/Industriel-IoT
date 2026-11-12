#include <mqtt/async_client.h>
#include <mqtt/ssl_options.h>
#include <mqtt/will_options.h>

#include <pqxx/pqxx>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

const std::string SERVER_ADDRESS = "ssl://gruppe7.vps.webdock.cloud:8883";
const std::string CLIENT_ID = "questdb-subscriber-01";
const std::string GROUP_ID = "Group7";
const std::string EDGE_NODE_ID = "thingy91x-01";
const std::string EDGE_TOPIC_FILTER =
    "spBv1.0/" + GROUP_ID + "/+/" + EDGE_NODE_ID;
const std::string NODE_COMMAND_TOPIC =
    "spBv1.0/" + GROUP_ID + "/NCMD/" + EDGE_NODE_ID;
const std::string STATE_TOPIC = "spBv1.0/STATE/" + CLIENT_ID;

class ProtoReader {
public:
    explicit ProtoReader(std::string_view bytes) : bytes_(bytes) {}

    bool empty() const { return position_ == bytes_.size(); }

    bool read_varint(std::uint64_t& value)
    {
        value = 0;
        for (unsigned shift = 0; shift < 70; shift += 7) {
            if (position_ >= bytes_.size()) {
                return false;
            }

            const auto byte = static_cast<std::uint8_t>(bytes_[position_++]);
            if (shift == 63 && (byte & 0xfeU) != 0) {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0) {
                return true;
            }
        }
        return false;
    }

    bool read_key(std::uint32_t& field, std::uint8_t& wire_type)
    {
        std::uint64_t key;
        if (!read_varint(key) || key == 0 || (key >> 3) > 0x1fffffffU) {
            return false;
        }
        field = static_cast<std::uint32_t>(key >> 3);
        wire_type = static_cast<std::uint8_t>(key & 0x07U);
        return wire_type == 0 || wire_type == 1 || wire_type == 2 || wire_type == 5;
    }

    bool read_bytes(std::string_view& value)
    {
        std::uint64_t length;
        if (!read_varint(length) || length > bytes_.size() - position_) {
            return false;
        }
        value = bytes_.substr(position_, static_cast<std::size_t>(length));
        position_ += static_cast<std::size_t>(length);
        return true;
    }

    bool read_fixed32(std::uint32_t& value)
    {
        if (bytes_.size() - position_ < sizeof(value)) {
            return false;
        }
        value = 0;
        for (unsigned i = 0; i < sizeof(value); ++i) {
            value |= static_cast<std::uint32_t>(
                         static_cast<std::uint8_t>(bytes_[position_++]))
                     << (i * 8);
        }
        return true;
    }

    bool read_fixed64(std::uint64_t& value)
    {
        if (bytes_.size() - position_ < sizeof(value)) {
            return false;
        }
        value = 0;
        for (unsigned i = 0; i < sizeof(value); ++i) {
            value |= static_cast<std::uint64_t>(
                         static_cast<std::uint8_t>(bytes_[position_++]))
                     << (i * 8);
        }
        return true;
    }

    bool skip(std::uint8_t wire_type)
    {
        std::uint64_t ignored;
        std::string_view bytes;
        switch (wire_type) {
        case 0:
            return read_varint(ignored);
        case 1:
            if (bytes_.size() - position_ < 8) return false;
            position_ += 8;
            return true;
        case 2:
            return read_bytes(bytes);
        case 5:
            if (bytes_.size() - position_ < 4) return false;
            position_ += 4;
            return true;
        default:
            return false;
        }
    }

private:
    std::string_view bytes_;
    std::size_t position_ = 0;
};

struct SparkplugMetric {
    std::string name;
    std::optional<std::uint64_t> alias;
    std::optional<double> value;
    bool is_null = false;
};

struct SparkplugPayload {
    std::vector<SparkplugMetric> metrics;
    std::optional<std::uint64_t> sequence;
    std::optional<std::uint64_t> timestamp_ms;
};

bool parse_metric(std::string_view bytes, SparkplugMetric& metric)
{
    ProtoReader reader(bytes);
    while (!reader.empty()) {
        std::uint32_t field;
        std::uint8_t wire;
        if (!reader.read_key(field, wire)) return false;

        if (field == 1 && wire == 2) {
            std::string_view name;
            if (!reader.read_bytes(name)) return false;
            metric.name.assign(name.data(), name.size());
        } else if ((field == 2 || field == 10 || field == 11 || field == 7) && wire == 0) {
            std::uint64_t value;
            if (!reader.read_varint(value)) return false;
            if (field == 2) metric.alias = value;
            if (field == 7) metric.is_null = value != 0;
            if (field == 10 || field == 11) metric.value = static_cast<double>(value);
        } else if (field == 12 && wire == 5) {
            std::uint32_t bits;
            if (!reader.read_fixed32(bits)) return false;
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            metric.value = static_cast<double>(value);
        } else if (field == 13 && wire == 1) {
            std::uint64_t bits;
            if (!reader.read_fixed64(bits)) return false;
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            metric.value = value;
        } else if (!reader.skip(wire)) {
            return false;
        }
    }
    return true;
}

bool parse_payload(std::string_view bytes, SparkplugPayload& payload)
{
    ProtoReader reader(bytes);
    while (!reader.empty()) {
        std::uint32_t field;
        std::uint8_t wire;
        if (!reader.read_key(field, wire)) return false;

        if (field == 1 && wire == 0) {
            std::uint64_t timestamp_ms;
            if (!reader.read_varint(timestamp_ms)) return false;
            payload.timestamp_ms = timestamp_ms;
        } else if (field == 2 && wire == 2) {
            if (payload.metrics.size() >= 64) return false;
            std::string_view metric_bytes;
            SparkplugMetric metric;
            if (!reader.read_bytes(metric_bytes) || !parse_metric(metric_bytes, metric)) {
                return false;
            }
            payload.metrics.push_back(std::move(metric));
        } else if (field == 3 && wire == 0) {
            std::uint64_t sequence;
            if (!reader.read_varint(sequence)) return false;
            payload.sequence = sequence;
        } else if (!reader.skip(wire)) {
            return false;
        }
    }
    return true;
}

std::int64_t current_time_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string format_questdb_timestamp(std::uint64_t timestamp_ms)
{
    if (timestamp_ms > static_cast<std::uint64_t>(
                           std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("Sparkplug timestamp is outside the supported range");
    }

    const auto seconds = static_cast<std::time_t>(timestamp_ms / 1000U);
    std::tm utc{};
    if (gmtime_r(&seconds, &utc) == nullptr) {
        throw std::runtime_error("Could not convert Sparkplug timestamp to UTC");
    }

    char buffer[40];
    const auto milliseconds = static_cast<unsigned>(timestamp_ms % 1000U);
    const int length = std::snprintf(
        buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
        utc.tm_sec, milliseconds);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(buffer)) {
        throw std::runtime_error("Could not format Sparkplug timestamp");
    }
    return buffer;
}

void append_varint(std::string& output, std::uint64_t value)
{
    while (value >= 0x80U) {
        output.push_back(static_cast<char>((value & 0x7fU) | 0x80U));
        value >>= 7;
    }
    output.push_back(static_cast<char>(value));
}

void append_uint_field(std::string& output, std::uint32_t field, std::uint64_t value)
{
    append_varint(output, (static_cast<std::uint64_t>(field) << 3U));
    append_varint(output, value);
}

void append_bytes_field(std::string& output, std::uint32_t field, std::string_view value)
{
    append_varint(output, (static_cast<std::uint64_t>(field) << 3U) | 2U);
    append_varint(output, value.size());
    output.append(value.data(), value.size());
}

std::string rebirth_command(std::uint64_t timestamp_ms)
{
    std::string metric;
    append_bytes_field(metric, 1, "Node Control/Rebirth");
    append_uint_field(metric, 3, timestamp_ms);
    append_uint_field(metric, 4, 11); // Sparkplug Boolean
    append_uint_field(metric, 14, 1); // boolean_value = true

    std::string payload;
    append_uint_field(payload, 1, timestamp_ms);
    append_bytes_field(payload, 2, metric);
    append_uint_field(payload, 3, 0); // first Host command in this session
    return payload;
}

std::string host_state(bool online, std::int64_t timestamp_ms)
{
    return std::string("{\"online\":") + (online ? "true" : "false") +
           ",\"timestamp\":" + std::to_string(timestamp_ms) + "}";
}

static std::string db_connection()
{
    const char *password = std::getenv("QUESTDB_PASSWORD");
    if (password == nullptr || password[0] == '\0') {
        throw std::runtime_error("QUESTDB_PASSWORD environment variable is not set");
    }

    return "host=localhost port=8812 dbname=qdb user=admin password=" +
           std::string(password);
}

static void ensure_questdb_table()
{
    pqxx::connection conn(db_connection());
    pqxx::work txn(conn);
    txn.exec(
        "CREATE TABLE IF NOT EXISTS thingy91x_data ("
        "ts TIMESTAMP, device_id SYMBOL, uptime_ms LONG, "
        "temperature_c DOUBLE, humidity_pct DOUBLE, pressure_kpa DOUBLE, gas_ohm DOUBLE"
        ") TIMESTAMP(ts) PARTITION BY DAY;"
    );
    txn.commit();
}

class callback : public virtual mqtt::callback
{
private:
    mqtt::async_client& client;
    std::mutex state_mutex;
    std::unordered_map<std::uint64_t, std::string> aliases;
    bool rebirth_requested = false;

    void request_rebirth()
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (rebirth_requested) return;
            rebirth_requested = true;
        }
        try {
            client.publish(NODE_COMMAND_TOPIC,
                           rebirth_command(static_cast<std::uint64_t>(current_time_ms())),
                           1, false);
            std::cout << "Requested Sparkplug node rebirth" << std::endl;
        } catch (const mqtt::exception& e) {
            std::lock_guard<std::mutex> lock(state_mutex);
            rebirth_requested = false;
            std::cerr << "Could not request Sparkplug node rebirth: " << e.what() << std::endl;
        }
    }

    void handle_birth(const std::string& topic, std::string_view bytes)
    {
        SparkplugPayload payload;
        if (!parse_payload(bytes, payload)) {
            throw std::runtime_error("Malformed Sparkplug NBIRTH Protobuf payload");
        }

        std::lock_guard<std::mutex> lock(state_mutex);
        aliases.clear();
        rebirth_requested = false;
        for (const auto& metric : payload.metrics) {
            if (metric.alias && !metric.name.empty() && metric.name != "bdSeq") {
                aliases[*metric.alias] = metric.name;
            }
        }
        std::cout << "Received Sparkplug NBIRTH on " << topic << "; registered "
                  << aliases.size() << " metric aliases" << std::endl;
    }

    void handle_data(const std::string& topic, std::string_view bytes)
    {
        SparkplugPayload payload;
        if (!parse_payload(bytes, payload)) {
            throw std::runtime_error("Malformed Sparkplug NDATA Protobuf payload");
        }
        if (!payload.timestamp_ms) {
            throw std::runtime_error("Sparkplug NDATA has no payload timestamp");
        }
        std::unordered_map<std::uint64_t, std::string> known_aliases;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            known_aliases = aliases;
        }
        if (known_aliases.empty()) {
            request_rebirth();
            return;
        }

        std::unordered_map<std::string, double> values;
        for (const auto& metric : payload.metrics) {
            if (!metric.value || !metric.alias) continue;
            const auto alias = known_aliases.find(*metric.alias);
            if (alias != known_aliases.end()) {
                values[alias->second] = *metric.value;
            }
        }
        if (values.size() < 5U) {
            request_rebirth();
            throw std::runtime_error("NDATA contains unknown or missing metric aliases");
        }

        const auto get = [&values](const char *name) -> double {
            const auto it = values.find(name);
            if (it == values.end()) {
                throw std::runtime_error(std::string("NDATA is missing metric ") + name);
            }
            return it->second;
        };

        const auto uptime_ms = static_cast<std::int64_t>(get("uptime_ms"));
        const double temperature_c = get("temperature_c");
        const double humidity_pct = get("humidity_pct");
        const double pressure_kpa = get("pressure_kpa");
        const double gas_ohm = get("gas_ohm");
        const std::string device_id = topic.substr(topic.find_last_of('/') + 1);
        const std::string sample_timestamp = format_questdb_timestamp(*payload.timestamp_ms);

        pqxx::connection conn(db_connection());
        pqxx::work txn(conn);
        txn.exec_params(
            "INSERT INTO thingy91x_data "
            "(ts, device_id, uptime_ms, temperature_c, humidity_pct, pressure_kpa, gas_ohm) "
            "VALUES (to_timestamp($1, 'yyyy-MM-ddTHH:mm:ss.SSSZ'), $2, $3, $4, $5, $6, $7);",
            sample_timestamp, device_id, uptime_ms, temperature_c, humidity_pct,
            pressure_kpa, gas_ohm);
        txn.commit();

        std::cout << "Stored Sparkplug NDATA from " << device_id
                  << " (sequence " << payload.sequence.value_or(0) << ") in QuestDB"
                  << std::endl;
    }

public:
    explicit callback(mqtt::async_client& cli) : client(cli) {}

    void reset_session()
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        aliases.clear();
        rebirth_requested = false;
    }

    void connected(const std::string& cause) override
    {
        (void)cause;
        std::cout << "MQTT connection established" << std::endl;
    }

    void connection_lost(const std::string& cause) override
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        aliases.clear();
        rebirth_requested = false;
        std::cerr << "MQTT connection lost";
        if (!cause.empty()) std::cerr << ": " << cause;
        std::cerr << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override
    {
        try {
            const std::string topic = msg->get_topic();
            const std::string payload_bytes = msg->to_string();
            const std::string_view bytes(payload_bytes.data(), payload_bytes.size());

            if (topic.find("/NBIRTH/") != std::string::npos) {
                handle_birth(topic, bytes);
            } else if (topic.find("/NDATA/") != std::string::npos) {
                handle_data(topic, bytes);
            } else if (topic.find("/NDEATH/") != std::string::npos) {
                std::lock_guard<std::mutex> lock(state_mutex);
                aliases.clear();
                rebirth_requested = false;
                std::cout << "Received Sparkplug NDEATH on " << topic << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing Sparkplug message: " << e.what() << std::endl;
        }
    }

    void delivery_complete(mqtt::delivery_token_ptr tok) override
    {
        (void)tok;
    }
};

} // namespace

int main(int argc, char* argv[])
{
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [ca-certificate.pem]\n"
                  << "Pass the broker CA file if it is not in the system trust store.\n";
        return 2;
    }

    try {
        ensure_questdb_table();

        mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
        callback cb(client);
        client.set_callback(cb);

        mqtt::ssl_options ssl_opts;
        ssl_opts.set_enable_server_cert_auth(true);
        if (argc == 2) {
            ssl_opts.set_trust_store(argv[1]);
        }

        while (true) {
            const auto state_timestamp_ms = current_time_ms();
            const mqtt::will_options state_will(
                STATE_TOPIC, host_state(false, state_timestamp_ms), 1, true);
            mqtt::connect_options conn_opts;
            conn_opts.set_clean_session(true);
            conn_opts.set_automatic_reconnect(false);
            conn_opts.set_will(state_will);
            conn_opts.set_ssl(ssl_opts);

            try {
                cb.reset_session();
                std::cout << "Connecting to " << SERVER_ADDRESS << "..." << std::endl;
                client.connect(conn_opts)->wait();
                client.subscribe(EDGE_TOPIC_FILTER, 1)->wait();
                client.publish(STATE_TOPIC, host_state(true, state_timestamp_ms), 1, true)->wait();
                std::cout << "Subscribed to Sparkplug data and published Host STATE"
                          << std::endl;
                while (client.is_connected()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                std::cerr << "MQTT disconnected; retrying in 5 seconds" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "MQTT connection failed: " << e.what()
                          << "; retrying in 5 seconds" << std::endl;
                if (client.is_connected()) {
                    try {
                        client.publish(STATE_TOPIC,
                                       host_state(false, state_timestamp_ms), 1, true)->wait();
                        client.disconnect()->wait();
                    } catch (const std::exception& disconnect_error) {
                        std::cerr << "Could not close failed MQTT session cleanly: "
                                  << disconnect_error.what() << std::endl;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    } catch (const std::exception& e) {
        std::cerr << "Subscriber error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
