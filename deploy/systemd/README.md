# Automatisk opstart på Webdock

Serveren bruger disse services:

| Service | Funktion |
| --- | --- |
| `docker` | Starter Mosquitto-containeren |
| `questdb` | Starter databasen som brugeren DK |
| `mqtt-questdb-subscriber` | Modtager Thingys Sparkplug-data som brugeren EF |
| `esp32-json-subscriber` | Modtager ESP32-data som brugeren EF |

Kør dette efter SSH-login som EF:

```bash
systemctl status questdb --no-pager
systemctl --user status mqtt-questdb-subscriber --no-pager
systemctl --user status esp32-json-subscriber --no-pager
```

Bruger-services kan starte uden login, fordi EF har lingering aktiveret.
`questdb.service` er en kopi af den aktuelle serveropsætning til QuestDB 10.0.1.
Den bruger installationen i `/opt/questdb/questdb-10.0.1-rt-linux-x86-64`
og data i `/home/DK/.questdb`. Den skal ikke installeres på en anden server uden tilpasning.

Thingy-service og installationsguide findes på branchen
[mqtt-sparkplug-subscriber](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-sparkplug-subscriber).
Adgangskoder ligger i EF's lokale `~/.config/questdb/credentials.env`, ikke i Git.
