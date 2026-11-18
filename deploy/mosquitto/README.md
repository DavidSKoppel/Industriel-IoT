# MQTT i Docker på Webdock

Kun Mosquitto kører i container. QuestDB og subscriberne beholder deres services.
Forbindelsen er stadig `gruppe7.vps.webdock.cloud:8883` med TLS.
Docker Desktop på din pc er ikke nødvendigt.

## Se eller genstart broker

Kør på Webdock:

```bash
cd /opt/group7-mqtt
sudo docker compose ps
sudo docker compose logs --tail 30 mosquitto
sudo docker compose restart mosquitto
```

Containeren starter automatisk med serveren, medmindre den er stoppet manuelt.
Start en manuelt stoppet container med `sudo docker compose up -d`.

## Første installation

Denne mappe indeholder migrationsscriptet fra 18/11/2026. Webdock er allerede migreret.
**Kør ikke installationen igen på den nuværende server.**

Ved en tilsvarende førstegangsinstallation skal den oprindelige Mosquitto-service,
certifikater i `/etc/mosquitto/certs` og brugeren `mosquitto` med UID/GID `123:122` findes.
Kør fra denne mappe på serveren:

```bash
sudo bash deploy.sh
```

Scriptet installerer Docker, tester TLS, tager backup og skifter til containeren.
Ved fejl under skiftet startes den oprindelige broker igen.
Den installerede opsætning ligger i `/opt/group7-mqtt`.

Certifikater monteres skrivebeskyttet. Private nøgler kopieres ikke ind i Git eller Docker-imaget.
Den eksisterende adgang uden MQTT-brugernavn/password er bevaret; TLS er fortsat aktivt.

## Gå tilbage til den tidligere broker

```bash
sudo bash /opt/group7-mqtt/rollback.sh
```

Dette genstarter den gamle service. Dens gemte beskeder er fra før migrationen;
nyere data i containeren kopieres ikke automatisk tilbage.

Testet 18/11/2026: TLS, pub/sub og 30 rigtige Thingy-målinger gennem containeren til QuestDB.
