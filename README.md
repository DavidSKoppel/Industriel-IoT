# Nordic Thingy:91 X Sparkplug B Edge Node

Zephyr application for the Nordic Thingy:91 X (nRF9151), based on nRF Connect SDK 3.4.0.
It reads temperature, humidity, pressure, and gas resistance from the onboard BME680
sensor and publishes the readings as Sparkplug B Protobuf messages to the study group's
Mosquitto broker over MQTT/TLS.

## Sparkplug B messages

- Broker: `gruppe7.vps.webdock.cloud`, TLS port `8883`
- Sparkplug Group ID: `Group7`
- Edge Node ID: `thingy91x-01`
- Topics: `spBv1.0/Group7/NBIRTH/thingy91x-01`, `spBv1.0/Group7/NDATA/thingy91x-01`, and `spBv1.0/Group7/NDEATH/thingy91x-01`
- Interval: 10 seconds
- Encoding: Sparkplug B Protobuf with `temperature_c`, `humidity_pct`, `pressure_kpa`,
  `gas_ohm`, and `uptime_ms` metrics
- Metric timestamps and payload timestamps use UTC milliseconds from the NCS date-time library.
  Sampling is skipped until valid UTC time is available.

The Edge Node subscribes to `spBv1.0/Group7/NCMD/thingy91x-01` for Sparkplug rebirth
requests and to `spBv1.0/STATE/questdb-subscriber-01` to wait for the Primary Host.
It publishes `NBIRTH` after the Host is online, both subscriptions are acknowledged,
and it has a sensor sample. The birth contains the current sensor values, their names
and aliases, `bdSeq`, and the required `Node Control/Rebirth` metric. `NDATA` then
publishes the five readings with aliases every 10 seconds. `NBIRTH` and `NDATA` use
QoS 0. `NDEATH` is a non-retained QoS 1 MQTT Last Will. The `bdSeq` counter is saved
in the Thingy's settings partition so it advances across new MQTT sessions and restarts.
On an NCMD rebirth request, the Edge Node pauses data publication and sends a new
`NBIRTH` using the current sample and the current `bdSeq`.

The TLS settings are in `overlay-tls-nrf91.conf`; the broker hostname, client ID,
sensor support, and publishing interval are in `prj.conf`. The BME680 node is enabled
in `boards/thingy91x_nrf9151_ns.overlay`.

Build for `thingy91x/nrf9151/ns` with sysbuild from an nRF Connect SDK 3.4.0
environment, including `overlay-tls-nrf91.conf` as an extra config file. Flash with
the `west thingy91x-dfu` command from the nRF Connect SDK terminal.
