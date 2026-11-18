#define main subscriber_program_main
#include "../mqtt_questdb_subscriber.cpp"
#undef main
#include <sstream>
#include <fstream>
#include <set>

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    // A real batched birth has a NEW session timestamp and OLD measurement
    // timestamps. bdSeq also uses the session time and must not override data.
    SparkplugPayload birth;
    birth.timestamp_ms = 1794565500000ULL;
    const std::uint64_t measured = 1794565200000ULL;
    std::unordered_map<std::uint64_t, std::string> mapping;
    unsigned alias = 1;
    for (const char* name : {"temperature_c", "humidity_pct", "pressure_kpa", "gas_ohm", "uptime_ms"}) {
        SparkplugMetric metric;
        metric.name = name; metric.alias = alias; metric.timestamp_ms = measured; metric.value = 0.0;
        mapping[alias++] = name;
        birth.metrics.push_back(metric);
    }
    SparkplugMetric control;
    control.name = "bdSeq"; control.timestamp_ms = birth.timestamp_ms; control.value = 1;
    birth.metrics.push_back(control);
    require(measurement_timestamp(birth, mapping) == measured, "Birth must keep the oldest measurement time");

    // Exercise protobuf metric timestamp decoding, including a valid zero value.
    std::string bytes;
    append_uint_field(bytes, 2, 1);
    append_uint_field(bytes, 3, measured);
    append_uint_field(bytes, 5, 1);
    append_uint_field(bytes, 10, 0);
    SparkplugMetric decoded;
    require(parse_metric(bytes, decoded), "Metric protobuf should parse");
    require(decoded.timestamp_ms == measured && decoded.value == 0.0, "Timestamp or zero value lost");
    require(decoded.is_historical, "Historical flag lost");

    SparkplugPayload data;
    data.timestamp_ms = birth.timestamp_ms;
    data.metrics.push_back(decoded);
    require(measurement_timestamp(data, mapping) == measured, "Alias metric timestamp must be used");
    data.metrics[0].timestamp_ms.reset();
    require(measurement_timestamp(data, mapping) == *data.timestamp_ms, "Legacy payload timestamp fallback failed");
    birth.metrics[1].timestamp_ms = measured + 1000;
    bool rejected = false;
    try { (void)measurement_timestamp(birth, mapping); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Mixed measurement timestamps must not silently combine readings");

    SparkplugPayload command;
    require(parse_payload(rebirth_command(measured), command), "Rebirth command must parse");
    require(!command.sequence, "NCMD must not include seq");
    require(command.timestamp_ms == measured, "NCMD must retain publication timestamp");
    require(command.metrics.size() == 1 && command.metrics[0].name == "Node Control/Rebirth",
            "NCMD must address Node Control/Rebirth");
    static_assert(NODE_COMMAND_QOS == 0, "NCMD requires QoS 0");

    const auto with_sequence = [](double seq) {
        SparkplugPayload p;
        SparkplugMetric m; m.name = "bdSeq"; m.value = seq;
        p.metrics.push_back(m);
        return p;
    };
    BirthDeathSession session;
    session.begin(with_sequence(4));
    session.begin(with_sequence(5));
    require(!session.end(with_sequence(4)), "Old death must not close the new session");
    require(session.bd_seq == 5, "Old death must preserve current session");
    require(!session.end(SparkplugPayload{}), "Missing bdSeq must not close session");
    require(!session.end(with_sequence(5.5)), "Fractional bdSeq must be rejected");
    require(session.end(with_sequence(5)), "Matching death must close session");
    require(!session.end(with_sequence(5)), "Duplicate death must be ignored");
    session.begin(with_sequence(255));
    session.begin(with_sequence(0));
    require(!session.end(with_sequence(255)), "bdSeq wrap must not accept prior death");
    require(session.end(with_sequence(0)), "Zero bdSeq is valid after wrap");

    // A complete NBIRTH must only register aliases, never open a DB connection.
    // No MQTT connection is made by this client during the test.
    mqtt::async_client client("tcp://127.0.0.1:1", "offline-birth-regression");
    callback cb(client);
    std::string birth_bytes;
    append_uint_field(birth_bytes, 1, measured);
    for (const auto& m : birth.metrics) {
        std::string metric;
        append_bytes_field(metric, 1, m.name);
        if (m.alias) append_uint_field(metric, 2, *m.alias);
        append_uint_field(metric, 3, measured);
        append_uint_field(metric, 11, m.name == "bdSeq" ? 5 : 0);
        append_bytes_field(birth_bytes, 2, metric);
    }
    std::ostringstream errors;
    auto* previous_errors = std::cerr.rdbuf(errors.rdbuf());
    cb.message_arrived(mqtt::make_message("spBv1.0/Group7/NBIRTH/thingy91x-01", birth_bytes));
    std::cerr.rdbuf(previous_errors);
    require(errors.str().empty(), "NBIRTH must not attempt database insertion");

    if (argc == 2) {
        // Fixtures come from the actual publisher C encoder, independently
        // decoded against the official schema by test_codec.py.
        std::ifstream fixtures(argv[1]);
        require(fixtures.good(), "Cannot open publisher fixtures");
        std::string kind, hex;
        unsigned data_count = 0, packet_count = 0;
        std::set<std::uint64_t> acquisition_times;
        std::uint64_t latest_birth_time = 0;
        BirthDeathSession fixture_session;
        mapping.clear();
        while (fixtures >> kind >> hex) {
            std::string packet;
            for (std::size_t i = 0; i < hex.size(); i += 2)
                packet.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            SparkplugPayload decoded_packet;
            require(parse_payload(packet, decoded_packet), "Publisher payload must parse in subscriber");
            ++packet_count;
            if (kind == "NBIRTH") {
                fixture_session.begin(decoded_packet);
                for (const auto& m : decoded_packet.metrics)
                    if (m.alias) mapping[*m.alias] = m.name;
                latest_birth_time = measurement_timestamp(decoded_packet, mapping);
            } else if (kind == "NDATA") {
                ++data_count;
                require(decoded_packet.sequence == data_count, "Batch sequence mismatch");
                require(decoded_packet.metrics.size() == 5, "Must retain all sensor fields");
                for (const auto& m : decoded_packet.metrics)
                    require(m.is_historical && m.value && m.alias && mapping.count(*m.alias),
                            "Historical sample must resolve every alias and value");
                const auto acquired = measurement_timestamp(decoded_packet, mapping);
                require(acquired < *decoded_packet.timestamp_ms, "Measurement time must survive newer publication time");
                acquisition_times.insert(acquired);
            } else if (kind == "NDEATH") {
                require(fixture_session.end(decoded_packet), "Batch death must match birth");
            } else require(false, "Unexpected fixture packet");
        }
        require(packet_count == 32 && data_count == 30 && acquisition_times.size() == 30,
                "One birth plus 30 unique data samples plus one death required");
        require(*acquisition_times.rbegin() == latest_birth_time,
                "Latest birth value must be stored exactly once through NDATA");
        std::cout << "PASS: actual publisher batch decoded by subscriber: 32 packets, 30 unique samples" << std::endl;
    }
    std::cout << "PASS: historical timestamps, NCMD format, old/matching/missing/wrapped bdSeq and birth-only registration" << std::endl;
}
