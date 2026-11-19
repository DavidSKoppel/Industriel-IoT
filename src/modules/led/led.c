/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/led.h>

#include "message_channel.h"
#include "thingy_status.h"

/* Register log module */
LOG_MODULE_REGISTER(led, CONFIG_MQTT_SAMPLE_LED_LOG_LEVEL);

#if THINGY_STATUS_ENABLED

#include <zephyr/init.h>
#include <modem/lte_lc.h>

enum led_color {
	COLOR_OFF = 0,
	COLOR_RED = BIT(0),
	COLOR_GREEN = BIT(1),
	COLOR_BLUE = BIT(2),
	COLOR_YELLOW = COLOR_RED | COLOR_GREEN,
};

#define BLINK_MS 250
#define CONNECTED_DISPLAY_MS 300
#define MIN_SEND_DISPLAY_MS 1000
#define SENT_DISPLAY_MS 3000
#define ERROR_DISPLAY_MS 2000
#define REPLAY_DISPLAY_MS 1250

/* GPIO LED indices in the Thingy:91 X board's gpio-leds node. */
static const struct device *const rgb_led =
	DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led0)));

static struct k_spinlock status_lock;
static enum thingy_status current_status = THINGY_CONNECTING;
static enum led_color last_color = COLOR_YELLOW;
static enum led_color replay_color;
static bool modem_sleeping;
static int64_t connected_until;
static int64_t send_started;
static int64_t blue_until;
static int64_t green_until;
static int64_t error_until;
static int64_t replay_until;

static void render_status(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(status_work, render_status);

void thingy_status_set(enum thingy_status status)
{
	int64_t now = k_uptime_get();
	k_spinlock_key_t key = k_spin_lock(&status_lock);

	current_status = status;
	if (status == THINGY_CONNECTED) {
		connected_until = now + CONNECTED_DISPLAY_MS;
	} else if (status == THINGY_SENDING) {
		/* Make green visible even when the first publish follows immediately. */
		send_started = MAX(now, connected_until);
	} else if (status == THINGY_SENT) {
		blue_until = MAX(now, send_started + MIN_SEND_DISPLAY_MS);
		green_until = blue_until + SENT_DISPLAY_MS;
	}
	(void)k_work_reschedule(&status_work, K_NO_WAIT);
	k_spin_unlock(&status_lock, key);
}

void thingy_status_error(void)
{
	k_spinlock_key_t key = k_spin_lock(&status_lock);

	error_until = k_uptime_get() + ERROR_DISPLAY_MS;
	(void)k_work_reschedule(&status_work, K_NO_WAIT);
	k_spin_unlock(&status_lock, key);
}

void thingy_status_replay(void)
{
	k_spinlock_key_t key = k_spin_lock(&status_lock);

	replay_color = last_color;
	replay_until = k_uptime_get() + REPLAY_DISPLAY_MS;
	(void)k_work_reschedule(&status_work, K_NO_WAIT);
	k_spin_unlock(&status_lock, key);
}

static void render_status(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!device_is_ready(rgb_led)) {
		return;
	}

	int64_t now = k_uptime_get();
	int64_t next = 0;
	int64_t blink_start = 0;
	enum led_color color = COLOR_OFF;
	bool blink = false;
	k_spinlock_key_t key = k_spin_lock(&status_lock);

	if (now < replay_until) {
		color = replay_color;
		next = replay_until;
	} else if (modem_sleeping) {
		color = COLOR_OFF;
	} else if (now < error_until) {
		color = COLOR_RED;
		blink = true;
		blink_start = error_until - ERROR_DISPLAY_MS;
		next = error_until;
	} else if ((current_status == THINGY_SENDING || current_status == THINGY_SENT) &&
		   now < connected_until) {
		color = COLOR_GREEN;
		next = connected_until;
	} else {
		switch (current_status) {
		case THINGY_CONNECTING:
			color = COLOR_YELLOW;
			blink = true;
			break;
		case THINGY_CONNECTED:
			color = COLOR_GREEN;
			break;
		case THINGY_SENT:
			if (now < blue_until) {
				color = COLOR_BLUE;
				blink = true;
				blink_start = send_started;
				next = blue_until;
			} else if (now < green_until) {
				color = COLOR_GREEN;
				next = green_until;
			}
			break;
		case THINGY_SENDING:
			color = COLOR_BLUE;
			blink = true;
			blink_start = send_started;
			break;
		}
	}

	if (blink) {
		int64_t elapsed = MAX(0, now - blink_start);
		int64_t toggle = now + BLINK_MS - elapsed % BLINK_MS;

		next = next ? MIN(next, toggle) : toggle;
		if ((elapsed / BLINK_MS) % 2 != 0) {
			color = COLOR_OFF;
		}
	}
	if (color != COLOR_OFF) {
		last_color = color;
	}
	if (next > now) {
		(void)k_work_reschedule(&status_work, K_MSEC(next - now));
	}
	k_spin_unlock(&status_lock, key);

	for (unsigned int channel = 0; channel < 3; channel++) {
		(void)led_set_brightness(rgb_led, channel, (color & BIT(channel)) ? 100 : 0);
	}
}

static void on_lte_event(const struct lte_lc_evt *event)
{
	if (event->type != LTE_LC_EVT_MODEM_SLEEP_ENTER &&
	    event->type != LTE_LC_EVT_MODEM_SLEEP_EXIT) {
		return;
	}
	if (event->modem_sleep.type != LTE_LC_MODEM_SLEEP_PSM &&
	    event->modem_sleep.type != LTE_LC_MODEM_SLEEP_PROPRIETARY_PSM) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&status_lock);

	modem_sleeping = event->type == LTE_LC_EVT_MODEM_SLEEP_ENTER;
	(void)k_work_reschedule(&status_work, K_NO_WAIT);
	k_spin_unlock(&status_lock, key);
}

static int status_init(void)
{
	if (!device_is_ready(rgb_led)) {
		LOG_ERR("Thingy RGB LED is not ready");
		return 0;
	}
	lte_lc_register_handler(on_lte_event);
	thingy_status_set(THINGY_CONNECTING);
	return 0;
}

SYS_INIT(status_init, APPLICATION, 99);

static void led_callback(const struct zbus_channel *chan)
{
	const enum network_status *status = zbus_chan_const_msg(chan);

	if (*status == NETWORK_DISCONNECTED) {
		thingy_status_set(THINGY_CONNECTING);
	}
}

#else

const static struct device *led_device = DEVICE_DT_GET_ANY(gpio_leds);

/* LED 1, green on Thingy:91 boards. */
#define LED_1_GREEN 1

void led_callback(const struct zbus_channel *chan)
{
	int err = 0;
	const enum network_status *status;

	if (&NETWORK_CHAN == chan) {

		if (!device_is_ready(led_device)) {
			LOG_ERR("LED device is not ready");
			return;
		}

		/* Get network status from channel. */
		status = zbus_chan_const_msg(chan);

		switch (*status) {
		case NETWORK_CONNECTED:
			err = led_on(led_device, LED_1_GREEN);
			if (err) {
				LOG_ERR("led_on, error: %d", err);
			}
			break;
		case NETWORK_DISCONNECTED:
			err = led_off(led_device, LED_1_GREEN);
			if (err) {
				LOG_ERR("led_off, error: %d", err);
			}
			break;
		default:
			LOG_ERR("Unknown event: %d", *status);
			break;
		}
	}
}

#endif

/* Register listener - led_callback will be called everytime a channel that the module listens on
 * receives a new message.
 */
ZBUS_LISTENER_DEFINE(led, led_callback);
