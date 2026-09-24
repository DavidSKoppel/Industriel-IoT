# Nordic Thingy:91 X MQTT Publisher

Zephyr application for the Nordic Thingy:91 X (nRF9151), based on nRF Connect SDK 3.4.0.
It reads temperature, humidity, pressure, and gas resistance from the onboard BME680
sensor and publishes the readings to the study group's Mosquitto broker over MQTT/TLS.

## MQTT output

- Broker: `gruppe7.vps.webdock.cloud`, TLS port `8883`
- Client ID: `thingy91x-01`
- Topic: `thingy91x-01/data` (the transport prepends the client ID to the configured `data` topic)
- Interval: 10 seconds
- Payload: JSON containing `device_id`, `uptime_ms`, `temperature_c`, `humidity_pct`,
  `pressure_kpa`, and `gas_ohm`

The TLS settings are in `overlay-tls-nrf91.conf`; the broker hostname, client ID,
topic suffix, sensor support, and publishing interval are in `prj.conf`. The BME680
node is enabled in `boards/thingy91x_nrf9151_ns.overlay`.

Build for `thingy91x/nrf9151/ns` with sysbuild from an nRF Connect SDK 3.4.0
environment, including `overlay-tls-nrf91.conf` as an extra config file. Flash with
the `west thingy91x-dfu` command from the nRF Connect SDK terminal.
