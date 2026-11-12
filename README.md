# Sparkplug B to QuestDB subscriber

C++ MQTT subscriber for Thingy:91 X sensor readings. It connects to the Webdock Mosquitto broker over TLS, subscribes to the Sparkplug B topic for the Thingy, decodes the Protobuf payloads, and inserts the measurements into a local QuestDB instance.

## Sparkplug B data flow

The Thingy is the Sparkplug Edge Node. Its group ID is `Group7` and its node ID is `thingy91x-01`. It publishes Sparkplug B Protobuf messages on:

```text
spBv1.0/Group7/NBIRTH/thingy91x-01
spBv1.0/Group7/NDATA/thingy91x-01
spBv1.0/Group7/NDEATH/thingy91x-01
```

`NBIRTH` announces the five metric names and aliases: temperature, humidity, pressure, gas resistance, and uptime. It includes their current values plus the `bdSeq` and required `Node Control/Rebirth` metrics. Each sensor sample is then sent as a timestamped `NDATA` payload using those aliases. `NBIRTH` and `NDATA` use QoS 0. `NDEATH` is configured as the broker's non-retained QoS 1 Last Will so an unexpected disconnect marks the node offline. The Edge Node subscribes to `NCMD` for rebirth requests and waits for the retained Primary Host `STATE` before publishing a birth. The sensor sampling interval remains configured in the Zephyr application.

The subscriber acts as the Sparkplug Host Application: it subscribes to the node's Sparkplug topics, then publishes a retained online JSON `STATE` with a matching offline Last Will. Each MQTT session gets a fresh timestamp shared by its online `STATE` and Last Will. If it receives `NDATA` before `NBIRTH`, it sends an `NCMD` `Node Control/Rebirth` request so the Edge Node can reannounce its metric aliases. It decodes the Protobuf payload, maps metric aliases back to names, and stores each sample in QuestDB using the sample's UTC timestamp. The implementation handles the numeric Protobuf fields used by this project, following Eclipse Tahu's `sparkplug_b.proto` schema; it is a project-specific subset, not a general-purpose Sparkplug library.

The NCS MQTT Helper exposes Last Will configuration only as a build-time text value, while Sparkplug `NDEATH` is binary Protobuf. The firmware therefore makes one initial MQTT connection to attach the binary Last Will to the helper's MQTT client, disconnects, and reconnects before publishing `NBIRTH`. The second connection is the active Sparkplug session.

## Get the project

Clone the Sparkplug subscriber branch from GitHub and enter the repository folder:

```sh
git clone --branch mqtt-sparkplug-subscriber https://github.com/DavidSKoppel/Industriel-IoT.git
cd Industriel-IoT
```

## Install build dependencies (Ubuntu/Debian)

```sh
sudo apt update
sudo apt install -y build-essential cmake libssl-dev ca-certificates \
  libpaho-mqtt-dev libpaho-mqttpp-dev libpqxx-dev pkg-config
```

## Configure and compile

Run these commands from the repository directory (the one containing `CMakeLists.txt`):

```sh
cmake -S . -B build
cmake --build build --parallel
```

The executable will be created at `build/mqtt_questdb_subscriber`. `cmake -S . -B build` configures the project; `cmake --build build --parallel` compiles it. If CMake reports a missing dependency, check that the packages above installed successfully.

## Run

QuestDB must be running on the same machine and accept PostgreSQL wire-protocol
connections on port `8812`. Set the database password in the environment before
running the subscriber; do not put it in the source code or commit it:

```sh
export QUESTDB_PASSWORD='your-QuestDB-password'
```

If the broker's CA certificate is installed in the system trust store:

```sh
./build/mqtt_questdb_subscriber
```

Otherwise, provide the CA certificate file as the only argument:

```sh
./build/mqtt_questdb_subscriber /path/to/ca.crt
```

The subscriber validates the broker certificate. Keep the broker hostname in the TLS URL so hostname verification can be performed. The current sample does not configure MQTT username/password authentication.
