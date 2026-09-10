#include <mqtt/async_client.h>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <iostream>
#include <string>

using json = nlohmann::json;

// MQTT settings
const std::string SERVER_ADDRESS = "tcp://192.168.1.100:1883";
const std::string CLIENT_ID = "curtain_backend";

// QuestDB PostgreSQL connection
const std::string DB_CONNECTION =
    "host=localhost "
    "port=8812 "
    "dbname=qdb "
    "user=admin "
    "password=quest";

class callback : public virtual mqtt::callback
{
private:
    mqtt::async_client& client;

public:
    callback(mqtt::async_client& cli)
        : client(cli)
    {
    }

    void connected(const std::string& cause) override
    {
        std::cout << "Connected to MQTT broker!"
                  << std::endl;

        try
        {
            client.subscribe("sensors/temperature", 1);
            client.subscribe("sensors/light", 1);

            std::cout << "Subscribed to MQTT topics!"
                      << std::endl;
        }
        catch (const mqtt::exception& e)
        {
            std::cerr << "Subscribe error: "
                      << e.what()
                      << std::endl;
        }
        std::cout << "So high"
                      << std::endl;
    }

    void connection_lost(const std::string& cause) override
    {
        std::cout << "Connection lost!"
                  << std::endl;

        if (!cause.empty())
        {
            std::cout << "Cause: "
                      << cause
                      << std::endl;
        }
    }

    void message_arrived(mqtt::const_message_ptr msg) override
    {
        try
        {
            std::string topic = msg->get_topic();
            std::string payload = msg->to_string();

            std::cout << "\n=== MQTT MESSAGE RECEIVED ==="
                      << std::endl;

            std::cout << "Topic: "
                      << topic
                      << std::endl;

            std::cout << "Payload: "
                      << payload
                      << std::endl;

            // Parse JSON
            json data = json::parse(payload);

            std::string device_id = data["device_id"];

            // Connect to QuestDB
            pqxx::connection conn(DB_CONNECTION);

            pqxx::work txn(conn);

            // =========================
            // TEMPERATURE SENSOR
            // =========================
            if (topic == "sensors/temperature")
            {
                double temperature = data["temperature"];

                std::string query =
                    "INSERT INTO temperature_data "
                    "VALUES ("
                    "now(), "
                    "'" + device_id + "', "
                    + std::to_string(temperature) +
                    ");";

                txn.exec(query);

                std::cout << "Inserted temperature into QuestDB!"
                          << std::endl;

                if (temperature > 28)
                {
                    std::cout << "Temperature high -> Curtain should OPEN"
                              << std::endl;
                }
            }

            // =========================
            // LIGHT SENSOR
            // =========================
            else if (topic == "sensors/light")
            {
                double light = data["light"];

                std::string query =
                    "INSERT INTO light_data "
                    "VALUES ("
                    "now(), "
                    "'" + device_id + "', "
                    + std::to_string(light) +
                    ");";

                txn.exec(query);

                std::cout << "Inserted light into QuestDB!"
                          << std::endl;

                if (light > 700)
                {
                    std::cout << "Light high -> Curtain should CLOSE"
                              << std::endl;
                }
            }

            txn.commit();
        }
        catch (const std::exception& e)
        {
            std::cerr << "ERROR: "
                      << e.what()
                      << std::endl;
        }
    }

    void delivery_complete(mqtt::delivery_token_ptr tok) override
    {
    }
};

int main()
{
    mqtt::async_client client(
        SERVER_ADDRESS,
        CLIENT_ID
    );

    callback cb(client);

    client.set_callback(cb);

    mqtt::connect_options connOpts;
    connOpts.set_clean_session(true);
    connOpts.set_automatic_reconnect(true);

    try
    {
        std::cout << "Connecting to MQTT broker..."
                  << std::endl;

        client.connect(connOpts)->wait();

        while (true)
        {
        }
    }
    catch (const mqtt::exception& exc)
    {
        std::cerr << "MQTT ERROR: "
                  << exc.what()
                  << std::endl;
    }

    return 0;
}