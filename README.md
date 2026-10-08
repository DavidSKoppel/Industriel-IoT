# Projekt C – Nordic Thingy:91 X

Studieprojekt i industriel IoT: En Nordic Thingy:91 X måler sensordata og sender dem via mobilnettet til gruppens egen cloud-server på Webdock. Firmwaren kører Zephyr RTOS og udvikles med nRF Connect SDK 3.4.0.

## Systemets opbygning

```text
Thingy:91 X / BME680
        │ Mobilnet + MQTT over TLS
        ▼
Mosquitto på Webdock
        │ MQTT subscriber (C++)
        ▼
QuestDB
```

Publisheren sender målinger. Mosquitto fordeler beskederne til subscribers. C++-subscriberen læser JSON og skriver målingerne i QuestDB. Diagrammet viser den tilsigtede samlede datakæde; databaseintegration og drift følges som særskilte opgaver.

## Find den rigtige kode

Udviklingen er fordelt på branches. Et almindeligt clone åbner `main`, som stadig indeholder den oprindelige Blinky-app.

| Branch | Indhold |
| --- | --- |
| [`main`](https://github.com/DavidSKoppel/Industriel-IoT/tree/main) | Projektets overblik og den oprindelige Blinky-app |
| [`mqtt-publisher-thingy`](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-publisher-thingy) | Thingy-firmware med BME680 og MQTT/TLS |
| [`mqtt-subscriber-broker`](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-subscriber-broker) | `mqtt_questdb_subscriber.cpp`, som modtager målinger og skriver til QuestDB |
| [`network-thingy`](https://github.com/DavidSKoppel/Industriel-IoT/tree/network-thingy) | Arbejdet med enhedens netværksforbindelse |

## MQTT og sensordata

Publisher-branchens opsætning er:

| Indstilling | Værdi |
| --- | --- |
| Broker | `gruppe7.vps.webdock.cloud` |
| TLS-port | `8883` |
| Client ID | `thingy91x-01` |
| Topic | `thingy91x-01/data` |
| Måle- og sendeinterval | 10 sekunder |
| Sensor | BME680: temperatur, luftfugtighed, lufttryk og gasmodstand |

Topic-suffikset er `data`; Nordic-samplets transport tilføjer client ID foran.

Eksempel på JSON-payload (illustrative værdier):

```json
{
  "device_id": "thingy91x-01",
  "uptime_ms": 120000,
  "temperature_c": 24.59,
  "humidity_pct": 46.314,
  "pressure_kpa": 101.315,
  "gas_ohm": 12917167.0
}
```

`uptime_ms` er tiden siden enheden startede, ikke et UTC-timestamp. Subscriberen gemmer modtagelsestidspunktet i QuestDB-kolonnen `ts` og beholder enhedens uptime separat. Tabellen hedder `thingy91x_data`.

## Build og flash af publisheren

Brug en nRF Connect SDK 3.4.0-terminal i VS Code, så `west` og toolchain er tilgængelige.

```powershell
git clone --branch mqtt-publisher-thingy https://github.com/DavidSKoppel/Industriel-IoT.git
cd Industriel-IoT
west build -p always -b thingy91x/nrf9151/ns --sysbuild -- -DEXTRA_CONF_FILE=overlay-tls-nrf91.conf
west thingy91x-dfu
```

Luk den serielle terminal før DFU, hvis den holder COM-porten åben. Åbn den igen efter flash for at følge enhedens output.

- `prj.conf`: broker, client ID, topic-suffiks, sensorunderstøttelse og interval.
- `overlay-tls-nrf91.conf`: TLS-konfiguration.
- `boards/thingy91x_nrf9151_ns.overlay`: aktivering af BME680.

CA-certifikatet skal være tilgængeligt for enhedens TLS-konfiguration. Se [publisherens README](https://github.com/DavidSKoppel/Industriel-IoT/blob/mqtt-publisher-thingy/README.md) for den branchespecifikke opsætning.

## Subscriber og QuestDB

C++-subscriberen bruger Eclipse Paho MQTT C++, nlohmann/json og libpqxx. Den forbinder med TLS til brokerens topic og bruger QuestDBs PostgreSQL-protokol på port `8812`.

Den aktuelle kode forventer QuestDB på samme maskine som subscriberen. Tilpas databaseforbindelsen til den faktiske serveropsætning før brug. Subscriberen kan få en CA-fil som argument, hvis brokerens CA ikke findes i systemets trust store.

## Status og næste opgaver

- Cellulær forbindelse og læsning af de fire BME680-målinger er afprøvet.
- MQTT-beskeder fra Thingy er modtaget på Webdock. Publisher-koden sender nu sensordata som JSON hvert 10. sekund.
- Subscriber-koden er tilpasset samme topic og dataformat; den samlede kæde til QuestDB skal verificeres i serveropsætningen.
- Sparkplug B v1.0 er et projektkrav. Den nuværende JSON-payload er endnu ikke Sparkplug B.
- Lavt strømforbrug og dataforbrug skal undersøges og dokumenteres, fx sleep/interrupts og små payloads eller batching.
- Rapporten skal beskrive arkitektur, testresultater og sikkerhedsovervejelser, herunder CRA og/eller IEC 62443.

## Docker som mulig videreudvikling

Docker kan samle Mosquitto, subscriber og QuestDB i separate containere på Webdock. Docker Compose kan beskrive netværk, tjenester og vedvarende datavolumes i én opsætning, som også kan testes lokalt.

Docker er foreløbig en mulighed under [Issue #13](https://github.com/DavidSKoppel/Industriel-IoT/issues/13), ikke en færdig deployment i dette repo. Thingy-firmwaren kører fortsat Zephyr. TLS, adgangskontrol og certifikater skal konfigureres, og databasefiler skal bevares i et persistent volume. Adgangskoder og private nøgler skal holdes uden for Git.

## Den oprindelige Blinky-app

`main` indeholder stadig Blinky-koden og dens build-filer. Den generelle dokumentation fra Zephyr-samplet findes i [README.rst](README.rst). Brug publisher-branchen til arbejdet med sensordata og MQTT.
