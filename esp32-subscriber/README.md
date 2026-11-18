# ESP32-data til QuestDB

Dette er den separate JSON-subscriber. Thingys Sparkplug-subscriber findes på sin egen branch.

| Topic | Data | Tabel |
| --- | --- | --- |
| `esp32-t/data` | Temperatur og luftfugtighed fra DHT11 via Modbus | `esp32_t_data` |
| `esp32-g/data` | Rå gasmåling fra ADC | `esp32_g_data` |

Gatewayens gasindgange er AO på GPIO34 og DO på GPIO26.
`air_quality` er en rå ADC-værdi, ikke ppm. `gas_detected` gemmes ikke i den nuværende tabel.
Modbus-serverens register 0 og 1 indeholder temperatur og luftfugtighed ganget med 10.
`date_time` er måletid i millisekunder; `ts` er modtagelsestid i databasen.

## Byg på Ubuntu

```bash
sudo apt install build-essential cmake pkg-config libssl-dev libpaho-mqtt-dev libpaho-mqttpp-dev libpqxx-dev nlohmann-json3-dev
cmake -S esp32-subscriber -B esp32-subscriber/build
cmake --build esp32-subscriber/build --parallel 2
```

Kør fra roden af `main`. Sæt `QUESTDB_PASSWORD` i terminalens miljø, før programmet startes.
Programmet forbinder til Webdock MQTT med TLS og til QuestDB på samme server.

## På Webdock

Kode og program ligger i `/home/EF/esp32-json-subscriber`.
Den vedlagte bruger-service læser password fra `~/.config/questdb/credentials.env`.

```bash
systemctl --user status esp32-json-subscriber --no-pager
journalctl --user -u esp32-json-subscriber -n 30 --no-pager
```

Subscriberens build og databasekontrol er testet. Stabil levering af rigtige DHT11-data
er endnu ikke bekræftet; Wi-Fi/Modbus-fejlsøgning er et separat arbejde (#16, #33 og #34).
Hotspot-passwords og lokale gateway-backups er ikke medtaget.
