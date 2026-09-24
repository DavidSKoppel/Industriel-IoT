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

#include "message_channel.h"

/* Register log module */
LOG_MODULE_REGISTER(sampler, CONFIG_MQTT_SAMPLE_SAMPLER_LOG_LEVEL);

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(sampler, CONFIG_MQTT_SAMPLE_SAMPLER_MESSAGE_QUEUE_SIZE);

static const struct device *const bme680 = DEVICE_DT_GET(DT_NODELABEL(bme680));

/* Convert a Zephyr sensor value into a signed decimal string for JSON. */
static void sensor_value_to_string(const struct sensor_value *value,
				  char *buffer, size_t buffer_size)
{
	int64_t micro_value = sensor_value_to_micro(value);
	int64_t whole = micro_value / 1000000;
	int64_t fraction = micro_value % 1000000;

	if (fraction < 0) {
		fraction = -fraction;
	}

	if (micro_value < 0) {
		whole = -whole;
		snprintk(buffer, buffer_size, "-%lld.%06lld",
		 (long long)whole, (long long)fraction);
	} else {
		snprintk(buffer, buffer_size, "%lld.%06lld",
		 (long long)whole, (long long)fraction);
	}
}

static void sample(void)
{
	struct payload payload = { 0 };
	struct sensor_value temperature;
	struct sensor_value humidity;
	struct sensor_value pressure;
	struct sensor_value gas_resistance;
	char temperature_text[24];
	char humidity_text[24];
	char pressure_text[24];
	char gas_resistance_text[24];
	uint32_t uptime_ms;
	int err;
	int len;

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

	sensor_value_to_string(&temperature, temperature_text, sizeof(temperature_text));
	sensor_value_to_string(&humidity, humidity_text, sizeof(humidity_text));
	sensor_value_to_string(&pressure, pressure_text, sizeof(pressure_text));
	sensor_value_to_string(&gas_resistance, gas_resistance_text,
			       sizeof(gas_resistance_text));

	/* Uptime is a relative timestamp; synchronized wall-clock time can be added later. */
	uptime_ms = k_uptime_get_32();
	len = snprintk(payload.string, sizeof(payload.string),
		       "{\"device_id\":\"thingy91x-01\",\"uptime_ms\":%u,"
		       "\"temperature_c\":%s,\"humidity_pct\":%s,"
		       "\"pressure_kpa\":%s,\"gas_ohm\":%s}",
		       uptime_ms, temperature_text, humidity_text,
		       pressure_text, gas_resistance_text);
	if ((len < 0) || (len >= sizeof(payload.string))) {
		LOG_ERR("Failed to construct sensor payload, error: %d", len);
		return;
	}

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
