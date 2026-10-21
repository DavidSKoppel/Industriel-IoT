# MQTT to QuestDB Subscriber

C++ subscriber for sensor data published by the Nordic Thingy:91 X. It connects to the Webdock Mosquitto broker over TLS, subscribes to `thingy91x-01/data`, parses the JSON payload, and inserts each sample into QuestDB.

## Get the project

Clone the `mqtt-subscriber-broker` branch and enter the repository directory:

```sh
git clone --branch mqtt-subscriber-broker https://github.com/DavidSKoppel/Industriel-IoT.git
cd Industriel-IoT
```

## Install dependencies (Ubuntu/Debian)

```sh
sudo apt update
sudo apt install -y build-essential cmake libssl-dev ca-certificates \
  libpaho-mqtt-dev libpaho-mqttpp-dev nlohmann-json3-dev libpqxx-dev pkg-config
```

## Configure and build

Run these commands from the repository directory (the one containing `CMakeLists.txt`):

```sh
cmake -S . -B build
cmake --build build --parallel
```

The executable will be created at `build/mqtt_questdb_subscriber`. The first command configures the project; the second compiles it. If CMake reports a missing dependency, check that the packages above installed successfully.

## Run

QuestDB must be running on the **same machine** as the subscriber and accept PostgreSQL wire-protocol connections on `localhost`, port `8812`. The current source uses database `qdb` and user `admin`.

Set the QuestDB password in the environment before starting the subscriber. Replace the example value with your password; do not put a real password in the source code or commit it:

```sh
export QUESTDB_PASSWORD='your-QuestDB-password'
```

If the broker's CA certificate is already in the system trust store, run:

```sh
./build/mqtt_questdb_subscriber
```

Otherwise, pass the CA certificate file as the only argument:

```sh
./build/mqtt_questdb_subscriber /path/to/ca.crt
```

## Configuration notes

- The broker address and TLS port are configured in the source as `gruppe7.vps.webdock.cloud:8883`; the subscriber validates the broker certificate and keeps hostname verification enabled.
- The subscription topic is `thingy91x-01/data`.
- The QuestDB host is currently hard-coded to `localhost`. If QuestDB is on another machine, update the connection settings in `mqtt_questdb_subscriber.cpp` first.
- The MQTT client ID is fixed as `thingy91x-questdb-subscriber`. Do not run another copy with the same ID while the Webdock service is running: the broker may disconnect one of the copies. Change `CLIENT_ID` in the source if you need a separate simultaneous subscriber.
- This sample does not configure MQTT username/password authentication.
