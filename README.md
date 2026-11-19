# Nordic Thingy:91 X – Sparkplug B

Firmware til nRF Connect SDK 3.4.0. BME680 måler temperatur, luftfugtighed,
lufttryk og gasmodstand. Data sendes via mobilnettet til Webdock med MQTT/TLS.

## Data og strømbesparelse

- Thingy sender ved første gyldige måling, så snart forbindelsen er klar.
- Derefter måler Thingy hvert 10. sekund og sender en batch efter 30 målinger.
- NBIRTH beskriver de nyeste værdier. De opsamlede målinger sendes derefter som historiske NDATA.
- NDEATH afslutter forbindelsen, så modemmet kan gå i PSM.
- PSM anmoder om 10 sekunders aktiv tid og TAU på 1800 sekunder. Mobilnettet afgør de godkendte tider.
- Måletid ligger på hver metric; beskedens timestamp er afsendelsestiden.

Broker: `gruppe7.vps.webdock.cloud:8883`. Gruppe: `Group7`. Enhed: `thingy91x-01`.
Topics: `spBv1.0/Group7/NBIRTH/thingy91x-01`, tilsvarende `NDATA`, `NDEATH` og `NCMD`.

Brug subscriberen på [mqtt-sparkplug-subscriber](https://github.com/DavidSKoppel/Industriel-IoT/tree/mqtt-sparkplug-subscriber).
Den gemmer NDATA. En normal batch på 32 beskeder giver 30 målinger.
QuestDB `ts` er modtagelsestiden; `date_time` er den oprindelige måletid.

## Byg og flash i Windows

Åbn en nRF Connect SDK 3.4.0-terminal i VS Code. Kør fra projektmappen:

```powershell
west build -p always -b thingy91x/nrf9151/ns --sysbuild -d "$env:USERPROFILE\sp-build" -- -DEXTRA_CONF_FILE=overlay-tls-nrf91.conf
west thingy91x-dfu --image "$env:USERPROFILE\sp-build\dfu_application.zip"
```

Tilslut Thingy via USB, tænd den og luk den serielle monitor før flash.
Den korte build-sti undgår problemer med lange Windows-stier.

## Statuslys

- Gul blinker, mens Thingy forbinder. Grøn lyser, når MQTT er forbundet.
- Blå blinker under afsendelse, efterfulgt af kort grønt lys.
- Rød blinker ved fejl opdaget af Thingy, f.eks. hvis en MQTT-afsendelse fejler.
- Lyset er slukket i modemets dvale og mellem batches efter den korte visning.
- Et tryk på topknappen viser den seneste farve i cirka ét sekund.

## Vigtige filer

| Fil | Indhold |
| --- | --- |
| `prj.conf` | Sensorer, batchstørrelse og PSM-anmodning |
| `overlay-tls-nrf91.conf` | TLS |
| `src/modules/transport/transport.c` | Buffer, MQTT-forbindelse og batches |
| `src/modules/transport/sparkplug_codec.c` | Protobuf-kodning |

NBIRTH/NDATA bruger QoS 0. NDEATH bruger QoS 1 og samme bdSeq som NBIRTH.
Thingy venter på subscriberens Host STATE og understøtter NCMD-rebirth.
Første MQTT-forbindelse bruges til at installere den binære Last Will; derefter genforbindes før NBIRTH.

Testet 18/11/2026 kl. 12:54 dansk tid: firmware flashet, og 30 unikke målinger gemt i QuestDB.
Implementeringen er projektspecifik og ikke fuldt Sparkplug-certificeret.
