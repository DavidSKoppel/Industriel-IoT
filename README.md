# Industriel IoT – Projekt C

Thingy:91 X måler temperatur, luftfugtighed, lufttryk og gasmodstand med BME680.
Målingerne sendes via mobilnettet til Webdock og gemmes i QuestDB.

```text
Thingy → Sparkplug B/Protobuf over MQTT/TLS → Mosquitto → C++ subscriber → QuestDB
```

## Find den rigtige kode

| Branch eller mappe | Bruges til |
| --- | --- |
| [mqtt-publisher-thingy](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-publisher-thingy) | Den aktuelle Thingy-firmware med batching og PSM |
| [feature/issue-20-sparkplug-publisher](https://github.com/DavidSKoppel/Industriel-IoT/tree/feature/issue-20-sparkplug-publisher) | Samme firmware, bevaret som arbejdsbranch |
| [mqtt-sparkplug-subscriber](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-sparkplug-subscriber) | Den aktuelle subscriber til Thingy og QuestDB |
| [mqtt-subscriber-broker](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-subscriber-broker) | Den tidligere JSON-subscriber; bruges ikke til Thingys Sparkplug-data |
| [deploy/mosquitto](deploy/mosquitto) | Docker-opsætningen til MQTT på Webdock |
| [deploy/systemd](deploy/systemd) | QuestDB-service og overblik over automatisk opstart |
| [esp32-subscriber](esp32-subscriber) | Den separate JSON-subscriber til ESP32-data |
| [security/password-policy](security/password-policy) | Passwordpolitik, Docker-lab og Webdock-scripts til CR 1.7 |

`main` indeholder overblik, serveropsætning og den oprindelige Blinky-app.
Brug publisher- og subscriber-brancherne til den aktuelle Thingy-løsning.

## Sådan sendes data

- Thingy måler hvert 10. sekund og sender efter 30 opsamlede målinger.
- NBIRTH beskriver de nyeste værdier. Derefter følger 30 historiske NDATA og NDEATH.
- Subscriberen gemmer de 30 målinger én gang hver.
- QuestDB `ts` er modtagelsestiden. `date_time` er den oprindelige måletid.
- PSM anmoder om 10 sekunders aktiv tid og TAU på 1800 sekunder. Mobilnettet afgør de godkendte tider.

Mosquitto kører i Docker **på Webdock**. QuestDB og subscriberne kører som services uden for Docker.

## Seneste test

Den 18/11/2026 kl. 12:54 dansk tid blev hele Thingy-kæden testet:
32 Sparkplug-beskeder gav præcis 30 unikke målinger i QuestDB.
Løsningen bruger et projektspecifikt udsnit af Sparkplug; den er ikke fuldt certificeret.

Adgangskoder, private certifikatnøgler og build-filer skal blive uden for Git.
