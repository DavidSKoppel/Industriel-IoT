# Tidligere JSON-subscriber

Denne branch er den tidligere løsning til `thingy91x-01/data` med JSON.
Den bevares som historik, men bruges ikke til Thingys aktuelle Sparkplug-firmware.
Dens databaseskema svarer heller ikke til den nuværende Thingy-tabel.

Brug i stedet:

- [mqtt-sparkplug-subscriber](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-sparkplug-subscriber): aktuel subscriber, build, tests og automatisk opstart.
- [mqtt-publisher-thingy](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-publisher-thingy): aktuel Thingy-firmware.
- [main](https://github.com/DavidSKoppel/Industriel-IoT): overblik, Docker-broker og ESP32-subscriber.

På Webdock hedder den aktuelle Thingy-service stadig `mqtt-questdb-subscriber`.
Navnet er bevaret, men den kører nu Sparkplug-koden fra `/home/EF/thingy91x-sparkplug`.
QuestDB og subscriberne kører uden for Docker; kun Mosquitto er containeriseret.
