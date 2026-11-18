# Thingy Sparkplug-subscriber

Modtager Thingys Sparkplug B/Protobuf fra Mosquitto og gemmer målingerne i QuestDB.
Brug denne branch sammen med den aktuelle `mqtt-publisher-thingy`.

## Hent og byg på Ubuntu

```bash
git clone --branch mqtt-sparkplug-subscriber https://github.com/DavidSKoppel/Industriel-IoT.git ~/thingy91x-sparkplug
cd ~/thingy91x-sparkplug
sudo apt install build-essential cmake pkg-config libssl-dev libpaho-mqtt-dev libpaho-mqttpp-dev libpqxx-dev
cmake -S . -B build
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

Testene kontrollerer format, tidsstempler og forbindelser uden at indsætte testdata i QuestDB.

## Start manuelt

QuestDB skal køre på samme server med PostgreSQL-port `8812`.
Sæt adgangskoden i **samme terminal**, som du starter programmet fra:

```bash
export QUESTDB_PASSWORD='din-QuestDB-adgangskode'
./build/mqtt_questdb_subscriber /etc/mosquitto/certs/ca.crt
```

Erstat teksten med din adgangskode. Den skal ikke skrives ind i kildekoden.
Broker er `gruppe7.vps.webdock.cloud:8883` med TLS.

## Sådan behandles en batch

1. NBIRTH beskriver de nyeste værdier og sensorernes aliases. Den gemmes ikke som en ekstra måling.
2. 30 NDATA-beskeder indeholder de gemte målinger, markeret som historiske.
3. NDEATH afslutter forbindelsen, hvis dens `bdSeq` matcher den seneste NBIRTH.

En rebirth-anmodning sendes som NCMD med QoS 0 og uden `seq`.
Subscriberen annoncerer sin status på `spBv1.0/STATE/questdb-subscriber-01`.

I `thingy91x_data` er `ts` databasens modtagelsestid. `date_time` er måletiden.
Kolonnerne `temperature`, `humidity`, `airpressure` og `gas` indeholder °C, %, kPa og ohm.
Der tilføjes ikke en ekstra `measured_at`-kolonne.

```sql
SELECT to_str(to_timezone(ts, 'Europe/Copenhagen'), 'dd/MM/yyyy HH:mm:ss') AS modtaget,
       device_id, temperature, humidity, airpressure, gas
FROM thingy91x_data ORDER BY ts DESC LIMIT 30;
```

## Automatisk opstart på Webdock

Programmet ligger i `/home/EF/thingy91x-sparkplug/build`.
Password ligger i `~/.config/questdb/credentials.env` som `QUESTDB_PASSWORD=...`.
Filen skal kun kunne læses af brugeren: `chmod 600 ~/.config/questdb/credentials.env`.

Den vedlagte service kan installeres efter build og oprettelse af password-filen:

```bash
mkdir -p ~/.config/systemd/user
cp deploy/mqtt-questdb-subscriber.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now mqtt-questdb-subscriber
sudo loginctl enable-linger "$USER"
```

På den nuværende Webdock er servicen allerede installeret. Brug:

```bash
systemctl --user status mqtt-questdb-subscriber --no-pager
journalctl --user -u mqtt-questdb-subscriber -n 30 --no-pager
```

Testet 18/11/2026 kl. 12:54 dansk tid: 32 beskeder gav 30 unikke målinger i QuestDB.
Det er en projektspecifik Sparkplug-implementering, ikke en fuld certificering.
