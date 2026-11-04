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

## Automatic startup on Webdock (systemd)

The deployed subscriber runs as a **user service owned by `EF`**, without root privileges. QuestDB runs separately as the system service `questdb.service`, using Linux user `DK` and data directory `/home/DK/.questdb`.

The subscriber restarts automatically if it exits. If QuestDB is not ready during boot, it retries every 15 seconds without hitting a restart limit. MQTT certificate and hostname verification remain enabled.

### Set up the subscriber service

Run the following on the Linux server as the account that will own the subscriber (`EF` on our Webdock server). Start from the repository directory after building it.

1. Install the compiled executable at the deployment path:

   ```sh
   install -D -m 0755 build/mqtt_questdb_subscriber \
     "$HOME/thingy91x-questdb-subscriber/mqtt_questdb_subscriber"
   ```

   When updating an existing deployment, stop its service before replacing the executable, then start it again afterwards.

2. Create a private environment file and edit it:

   ```sh
   install -d -m 0700 "$HOME/.config/questdb"
   touch "$HOME/.config/questdb/credentials.env"
   chmod 0600 "$HOME/.config/questdb/credentials.env"
   nano "$HOME/.config/questdb/credentials.env"
   ```

   Add the following line with the password for the QuestDB `admin` account:

   ```ini
   QUESTDB_PASSWORD="your-QuestDB-password"
   ```

   The service reads this file at startup, so an interactive `export` is not required. Keep the file on the server and out of Git. Restart the subscriber after changing it. Changing this file alone does not change the password configured in QuestDB.

3. Ensure the broker's CA certificate is trusted. The Webdock deployment uses the system CA trust store. For a fresh server with our private CA, install the public CA certificate:

   ```sh
   sudo install -m 0644 /path/to/ca.crt \
     /usr/local/share/ca-certificates/gruppe7-mqtt-ca.crt
   sudo update-ca-certificates
   ```

   Alternatively, append an absolute CA certificate path to `ExecStart` below. Keep TLS verification enabled in either case.

4. Create the user service:

   ```sh
   mkdir -p "$HOME/.config/systemd/user"
   cat > "$HOME/.config/systemd/user/mqtt-questdb-subscriber.service" <<'EOF'
   [Unit]
   Description=Thingy:91 X MQTT subscriber to QuestDB
   StartLimitIntervalSec=0

   [Service]
   Type=simple
   WorkingDirectory=%h/thingy91x-questdb-subscriber
   EnvironmentFile=%h/.config/questdb/credentials.env
   ExecStart=%h/thingy91x-questdb-subscriber/mqtt_questdb_subscriber
   Restart=always
   RestartSec=15

   [Install]
   WantedBy=default.target
   EOF
   ```

   `%h` expands to the service owner's home directory, `/home/EF` on Webdock.

5. Enable startup without an interactive login and start the service:

   ```sh
   sudo loginctl enable-linger "$USER"
   systemctl --user daemon-reload
   systemctl --user enable --now mqtt-questdb-subscriber.service
   ```

   QuestDB must also be enabled at boot. On the existing Webdock installation:

   ```sh
   sudo systemctl enable --now questdb.service
   ```

   The QuestDB service uses the bundled Java runtime at `/opt/questdb/questdb-10.0.1-rt-linux-x86-64/bin/java`. Its configuration file is `/home/DK/.questdb/conf/server.conf`. A missing configuration file prevents startup. A fresh QuestDB installation also needs its own system service; see the [official systemd deployment guide](https://questdb.com/docs/deployment/systemd/) and adapt the installation and data paths.

## Service commands

Connect from Windows PowerShell:

```powershell
ssh EF@gruppe7.vps.webdock.cloud
```

Run subscriber commands as `EF`. `systemctl --user` controls the current user's services; do not prefix these commands with `sudo`.

| Action | Subscriber command |
| --- | --- |
| Status | `systemctl --user status mqtt-questdb-subscriber.service --no-pager` |
| Start | `systemctl --user start mqtt-questdb-subscriber.service` |
| Stop | `systemctl --user stop mqtt-questdb-subscriber.service` |
| Restart | `systemctl --user restart mqtt-questdb-subscriber.service` |
| Logs from this boot | `journalctl --user -u mqtt-questdb-subscriber.service -b --no-pager -n 50` |
| Follow live logs | `journalctl --user -u mqtt-questdb-subscriber.service -f` |
| Check automatic startup | `systemctl --user is-enabled mqtt-questdb-subscriber.service` |
| Check lingering | `loginctl show-user EF -p Linger` |

Expected status: `active (running)`, `enabled`, and `Linger=yes`. A manual stop keeps the subscriber stopped until it is started again or the next boot.

| Action | QuestDB command |
| --- | --- |
| Status | `sudo systemctl status questdb.service --no-pager` |
| Start | `sudo systemctl start questdb.service` |
| Stop | `sudo systemctl stop questdb.service` |
| Restart | `sudo systemctl restart questdb.service` |
| Logs from this boot | `sudo journalctl -u questdb.service -b --no-pager -n 50` |
| Check automatic startup | `systemctl is-enabled questdb.service` |

QuestDB's web console is available at [gruppe7.vps.webdock.cloud:9000](http://gruppe7.vps.webdock.cloud:9000/index.html). Log in with the configured QuestDB account. The subscriber connects locally on port `8812`.

## Reboot test (issue #7)

Verified on **4 November 2026** with a full Webdock server reboot:

- QuestDB started automatically and authenticated web-console access and database queries succeeded.
- The existing 55 database rows were preserved.
- The subscriber started automatically without a manual service start. It retried three times while QuestDB was starting, then connected to the MQTT broker over TLS and subscribed to `thingy91x-01/data`.
- A new Thingy sensor message reaching QuestDB after the reboot was subsequently confirmed by the project operator.

To repeat the test, arrange a suitable interruption for everyone using the server, then run:

```sh
sudo systemctl reboot
```

The SSH connection closes. Reconnect after the server is available, then check:

```sh
uptime -s
systemctl status questdb.service --no-pager
systemctl --user status mqtt-questdb-subscriber.service --no-pager
journalctl --user -u mqtt-questdb-subscriber.service -b --no-pager -n 50
```

The subscriber log should contain:

```text
Connected to Webdock MQTT broker!
Subscribed to thingy91x-01/data
```

Earlier connection failures in the same boot can be normal while QuestDB is starting. Check that the latest attempt succeeds and the service stays running. Finally, send a new measurement from the Thingy and confirm a new row in QuestDB:

```sql
SELECT * FROM thingy91x_data ORDER BY ts DESC LIMIT 10;
```
