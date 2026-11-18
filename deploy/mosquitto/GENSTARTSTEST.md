# Genstartstest – issue #20

**Bestået 18/11/2026. Alle tider nedenfor er dansk tid.**

1. Mosquitto-containeren blev genstartet kl. 14:18:23 med `sudo docker restart group7-mosquitto`.
2. Thingy-subscriberen forbandt automatisk igen kl. 14:18:30 og annoncerede Host STATE.
3. Begge subscriber-services var aktive efter genstarten.
4. Thingys næste batch blev gemt i QuestDB kl. 14:19:51: præcis 30 rækker med 30 forskellige måletider.
5. Beskederne blev afkodet med det officielle Protobuf-schema: NBIRTH + 30 historiske NDATA + NDEATH.
   Sekvensnumre, QoS, retain og bdSeq var korrekte. NBIRTH matchede den nyeste måling.

Thingy blev også genstartet ved firmware-flash tidligere samme dag.
Den efterfølgende batch kl. 12:54 gav ligeledes 30 unikke målinger i QuestDB.

Testen brugte rigtige sensormålinger. Der blev ikke indsat kunstige testdata.
Dataflowet virker efter genstart af både broker og Thingy, som krævet i issue #20.
