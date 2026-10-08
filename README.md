# MQTT to QuestDB subscriber

C++ MQTT subscriber for Thingy:91 X sensor readings. It connects to the Webdock Mosquitto broker over TLS, subscribes to `thingy91x-01/data`, parses the JSON payload, and inserts the fields into a local QuestDB instance.

## Get the project

Clone the CMake-enabled branch from GitHub and enter the repository folder:

```sh
git clone --branch Linux-Cmake https://github.com/DavidSKoppel/Industriel-IoT.git
cd Industriel-IoT
```

## Install build dependencies (Ubuntu/Debian)

```sh
sudo apt update
sudo apt install -y build-essential cmake libssl-dev ca-certificates \
  libpaho-mqtt-dev libpaho-mqttpp-dev nlohmann-json3-dev libpqxx-dev pkg-config
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
