/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <date_time.h>

#include "message_channel.h"

/* Register log module */
LOG_MODULE_REGISTER(sampler, CONFIG_MQTT_SAMPLE_SAMPLER_LOG_LEVEL);

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(sampler, CONFIG_MQTT_SAMPLE_SAMPLER_MESSAGE_QUEUE_SIZE);

static const struct device *const bme680 = DEVICE_DT_GET(DT_NODELABEL(bme680));

static void sample(void)
{
	struct payload payload = { 0 };
	struct sensor_value temperature;
	struct sensor_value humidity;
	struct sensor_value pressure;
	struct sensor_value gas_resistance;
	uint32_t uptime_ms;
	int64_t timestamp_ms;
	int err;

	if (!device_is_ready(bme680)) {
		LOG_ERR("BME680 sensor is not ready");
		return;
	}

	err = sensor_sample_fetch(bme680);
	if (err == 0) {
		err = sensor_channel_get(bme680, SENSOR_CHAN_AMBIENT_TEMP,
					 &temperature);
	}
	if (err == 0) {
		err = sensor_channel_get(bme680, SENSOR_CHAN_HUMIDITY, &humidity);
	}
	if (err == 0) {
		err = sensor_channel_get(bme680, SENSOR_CHAN_PRESS, &pressure);
	}
	if (err == 0) {
		err = sensor_channel_get(bme680, SENSOR_CHAN_GAS_RES, &gas_resistance);
	}
	if (err != 0) {
		LOG_ERR("Failed to read BME680 data: %d", err);
		return;
	}
	err = date_time_now(&timestamp_ms);
	if (err != 0) {
		LOG_WRN("Valid UTC time is not available yet; skipping sample timestamp");
		return;
	}

	/* Keep numeric sensor values until the transport encodes the binary
	 * Sparkplug B Payload. Device ID is carried in the MQTT topic.
	 */
	payload.uptime_ms = k_uptime_get_32();
	payload.timestamp_ms = (uint64_t)timestamp_ms;
	payload.temperature_c = (double)sensor_value_to_micro(&temperature) / 1000000.0;
	payload.humidity_pct = (double)sensor_value_to_micro(&humidity) / 1000000.0;
	payload.pressure_kpa = (double)sensor_value_to_micro(&pressure) / 1000000.0;
	payload.gas_ohm = (double)sensor_value_to_micro(&gas_resistance) / 1000000.0;

	err = zbus_chan_pub(&PAYLOAD_CHAN, &payload, K_SECONDS(1));
	if (err != 0) {
		LOG_ERR("Failed to publish payload channel, error: %d", err);
		SEND_FATAL_ERROR();
	}
}

static void sampler_task(void)
{
	const struct zbus_channel *chan;

	while (!zbus_sub_wait(&sampler, &chan, K_FOREVER)) {
		if (&TRIGGER_CHAN == chan) {
			sample();
		}
	}
}

K_THREAD_DEFINE(sampler_task_id,
		CONFIG_MQTT_SAMPLE_SAMPLER_THREAD_STACK_SIZE,
		sampler_task, NULL, NULL, NULL, 3, 0, 0);
