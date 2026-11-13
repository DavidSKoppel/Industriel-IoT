/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/smf.h>
#include <net/mqtt_helper.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/settings/settings.h>
#include <date_time.h>

#include "client_id.h"
#include "message_channel.h"
#include "sparkplug_codec.h"

/* Register log module */
LOG_MODULE_REGISTER(transport, CONFIG_MQTT_SAMPLE_TRANSPORT_LOG_LEVEL);

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(transport, CONFIG_MQTT_SAMPLE_TRANSPORT_MESSAGE_QUEUE_SIZE);

#define SPARKPLUG_GROUP_ID "Group7"
#define SPARKPLUG_TOPIC_BUFFER_SIZE 128
#define SPARKPLUG_HOST_ID "questdb-subscriber-01"
#define HOST_STATE_SUBSCRIPTION_ID 2470
#define NODE_COMMAND_SUBSCRIPTION_ID 2471
#define HOST_STATE_TOPIC "spBv1.0/STATE/" SPARKPLUG_HOST_ID
#define NODE_COMMAND_TOPIC "spBv1.0/Group7/NCMD/thingy91x-01"
#define BD_SEQ_SETTINGS_KEY "sparkplug/bd_seq"

/* Forward declarations */
static const struct smf_state state[];
static void connect_work_fn(struct k_work *work);

/* Define connection work - Used to handle reconnection attempts to the MQTT broker */
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);

/* Define stack_area of application workqueue */
K_THREAD_STACK_DEFINE(stack_area, CONFIG_MQTT_SAMPLE_TRANSPORT_WORKQUEUE_STACK_SIZE);

/* Declare application workqueue. This workqueue is used to call mqtt_helper_connect(), and
 * schedule reconnectionn attempts upon network loss or disconnection from MQTT.
 */
static struct k_work_q transport_queue;

/* Internal states */
enum module_state { MQTT_CONNECTED, MQTT_DISCONNECTED };

/* MQTT client ID buffer */
static char client_id[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE];

static uint8_t birth_topic[SPARKPLUG_TOPIC_BUFFER_SIZE];
static uint8_t data_topic[SPARKPLUG_TOPIC_BUFFER_SIZE];
static uint8_t death_topic[SPARKPLUG_TOPIC_BUFFER_SIZE];
static size_t birth_topic_size;
static size_t data_topic_size;
static size_t death_topic_size;

/* MQTT Helper only exposes a build-time string Last Will. Install the binary
 * Sparkplug NDEATH Last Will after its first CONNACK, then reconnect once so
 * the broker receives it in the next CONNECT packet.
 */
static uint8_t death_will_payload[SPARKPLUG_PAYLOAD_MAX_SIZE];
static struct mqtt_topic death_will_topic;
static struct mqtt_utf8 death_will_message;
static bool death_will_installed;
static bool bootstrap_connection_used;
static bool active_connection_seen;
static uint8_t bd_seq;
static uint8_t data_sequence;

/* Sample stockpile.
 *
 * Samples arrive on PAYLOAD_CHAN every trigger interval, but they are only transmitted in one
 * burst ("dump") once CONFIG_MQTT_SAMPLE_TRANSPORT_DUMP_SAMPLE_COUNT of them have been collected.
 * The MQTT connection only exists while a dump is in progress, so between dumps the modem is
 * idle and can enter PSM.
 *
 * The ring buffer is only accessed from the transport thread, so it needs no locking.
 */
BUILD_ASSERT(CONFIG_MQTT_SAMPLE_TRANSPORT_SAMPLE_BUFFER_SIZE >=
	     CONFIG_MQTT_SAMPLE_TRANSPORT_DUMP_SAMPLE_COUNT,
	     "The sample buffer must be able to hold at least one full dump");

static struct payload sample_buf[CONFIG_MQTT_SAMPLE_TRANSPORT_SAMPLE_BUFFER_SIZE];
static size_t sample_head;	/* Index of the oldest stockpiled sample */
static size_t sample_count;	/* Number of stockpiled samples */
static bool dump_requested;	/* A dump is due (or in progress) and the broker should be connected */
static bool delay_next_connect;	/* Wait for the reconnection timeout before the next attempt */

enum transport_event_type {
	CONNECTED,
	DISCONNECTED,
	HOST_ONLINE,
	HOST_OFFLINE,
	HOST_STATE_SUBSCRIBED,
	NODE_COMMAND_SUBSCRIBED,
	REBIRTH_REQUESTED,
};

struct transport_event {
	enum transport_event_type type;
	uint64_t timestamp_ms;
};

/* Private channel for internal events */
ZBUS_CHAN_DEFINE(TRANSPORT_PRIVATE_CHANNEL,
		 struct transport_event,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(transport),
		 ZBUS_MSG_INIT(0)
);

/* User defined state object.
 * Used to transfer data between state changes.
 */
static struct s_object {
	/* This must be first */
	struct smf_ctx ctx;

	/* Last channel type that a message was received on */
	const struct zbus_channel *chan;

	/* Network status */
	enum network_status status;

	/* Payload */
	struct payload payload;
	bool host_online;
	bool node_born;
	bool sample_seen;
	bool host_state_subscribed;
	bool node_command_subscribed;
	bool host_timestamp_seen;
	uint64_t host_timestamp_ms;
} s_obj;

static void transport_event_publish(enum transport_event_type type, uint64_t timestamp_ms)
{
	struct transport_event event = {
		.type = type,
		.timestamp_ms = timestamp_ms,
	};
	int err = zbus_chan_pub(&TRANSPORT_PRIVATE_CHANNEL, &event, K_SECONDS(1));

	if (err != 0) {
		LOG_ERR("Failed to queue MQTT event %d, error: %d", type, err);
		SEND_FATAL_ERROR();
	}
}

static int bd_seq_settings_init(void)
{
	uint8_t previous_bd_seq;
	int err = settings_subsys_init();

	if (err != 0) {
		return err;
	}

	err = settings_load_one(BD_SEQ_SETTINGS_KEY, &previous_bd_seq,
				sizeof(previous_bd_seq));
	if (err == -ENOENT || err == 0) {
		bd_seq = 0U;
		return 0;
	}
	if (err < 0) {
		return err;
	}
	if (err != sizeof(previous_bd_seq)) {
		return -EINVAL;
	}

	bd_seq = (uint8_t)(previous_bd_seq + 1U);
	return 0;
}

static int bd_seq_settings_save(void)
{
	return settings_save_one(BD_SEQ_SETTINGS_KEY, &bd_seq, sizeof(bd_seq));
}

static int sparkplug_topics_init(void)
{
	int len;

	len = snprintk(birth_topic, sizeof(birth_topic), "spBv1.0/%s/NBIRTH/%s",
		       SPARKPLUG_GROUP_ID, client_id);
	if (len < 0 || len >= sizeof(birth_topic)) {
		return -EMSGSIZE;
	}
	birth_topic_size = len;

	len = snprintk(data_topic, sizeof(data_topic), "spBv1.0/%s/NDATA/%s",
		       SPARKPLUG_GROUP_ID, client_id);
	if (len < 0 || len >= sizeof(data_topic)) {
		return -EMSGSIZE;
	}
	data_topic_size = len;

	len = snprintk(death_topic, sizeof(death_topic), "spBv1.0/%s/NDEATH/%s",
		       SPARKPLUG_GROUP_ID, client_id);
	if (len < 0 || len >= sizeof(death_topic)) {
		return -EMSGSIZE;
	}
	death_topic_size = len;

	return 0;
}

static int mqtt_publish_binary(const uint8_t *topic, size_t topic_size,
			       const uint8_t *payload, size_t payload_size, uint8_t qos)
{
	struct mqtt_publish_param param = {
		.message.payload.data = (uint8_t *)payload,
		.message.payload.len = payload_size,
		.message.topic.qos = qos,
		.message_id = mqtt_helper_msg_id_get(),
		.message.topic.topic.utf8 = topic,
		.message.topic.topic.size = topic_size,
	};
	int err = mqtt_helper_publish(&param);

	if (err != 0) {
		LOG_ERR("Failed to publish Sparkplug message, error: %d", err);
		return err;
	}

	LOG_INF("Published Sparkplug message on %.*s (%u-byte payload)",
		(int)topic_size, topic, (unsigned int)payload_size);
	return 0;
}

static int sparkplug_publish_birth(const struct payload *sample)
{
	uint8_t payload[SPARKPLUG_PAYLOAD_MAX_SIZE];
	size_t payload_size;
	int64_t timestamp_ms;
	int err = date_time_now(&timestamp_ms);

	if (err != 0) {
		LOG_ERR("Cannot publish Sparkplug NBIRTH without valid UTC time: %d", err);
		return err;
	}
	err = sparkplug_encode_birth(payload, sizeof(payload), &payload_size, bd_seq,
				     sample, (uint64_t)timestamp_ms);

	if (err != 0) {
		LOG_ERR("Failed to encode Sparkplug NBIRTH, error: %d", err);
		return err;
	}

	err = mqtt_publish_binary(birth_topic, birth_topic_size, payload, payload_size,
				  MQTT_QOS_0_AT_MOST_ONCE);
	if (err == 0) {
		data_sequence = 1U;
	}
	return err;
}

static int sparkplug_publish_data(const struct payload *sample)
{
	uint8_t payload[SPARKPLUG_PAYLOAD_MAX_SIZE];
	size_t payload_size;
	int err = sparkplug_encode_data(payload, sizeof(payload), &payload_size,
					sample, data_sequence);

	if (err != 0) {
		LOG_ERR("Failed to encode Sparkplug NDATA, error: %d", err);
		return err;
	}

	err = mqtt_publish_binary(data_topic, data_topic_size, payload, payload_size,
				  MQTT_QOS_0_AT_MOST_ONCE);
	if (err == 0) {
		data_sequence++;
	}
	return err;
}

static int sparkplug_publish_death(void)
{
	uint8_t payload[SPARKPLUG_PAYLOAD_MAX_SIZE];
	size_t payload_size;
	int64_t timestamp_ms;
	int err = date_time_now(&timestamp_ms);

	if (err != 0) {
		LOG_ERR("Cannot publish Sparkplug NDEATH without valid UTC time: %d", err);
		return err;
	}
	err = sparkplug_encode_death(payload, sizeof(payload), &payload_size, bd_seq,
				     (uint64_t)timestamp_ms);

	if (err != 0) {
		LOG_ERR("Failed to encode Sparkplug NDEATH, error: %d", err);
		return err;
	}

	return mqtt_publish_binary(death_topic, death_topic_size, payload, payload_size,
				   MQTT_QOS_1_AT_LEAST_ONCE);
}

/* Append a sample to the stockpile. If the buffer is full the oldest sample is dropped. */
static void sample_buf_push(const struct payload *sample)
{
	if (sample_count == ARRAY_SIZE(sample_buf)) {
		sample_head = (sample_head + 1U) % ARRAY_SIZE(sample_buf);
		sample_count--;
		LOG_WRN("Sample buffer full, dropping the oldest sample");
	}

	sample_buf[(sample_head + sample_count) % ARRAY_SIZE(sample_buf)] = *sample;
	sample_count++;
}

/* Oldest stockpiled sample, or NULL if the stockpile is empty. */
static const struct payload *sample_buf_peek(void)
{
	return sample_count > 0U ? &sample_buf[sample_head] : NULL;
}

/* Remove the oldest stockpiled sample. */
static void sample_buf_pop(void)
{
	if (sample_count > 0U) {
		sample_head = (sample_head + 1U) % ARRAY_SIZE(sample_buf);
		sample_count--;
	}
}

/* Publish every stockpiled sample as its own NDATA message, oldest first. Each message keeps
 * the timestamp the sample was taken at. A sample is only removed from the stockpile once it
 * has been handed to the MQTT library, so a failed dump loses nothing.
 */
static int dump_samples(void)
{
	const struct payload *sample;
	unsigned int sent = 0U;
	int err;

	while ((sample = sample_buf_peek()) != NULL) {
		err = sparkplug_publish_data(sample);
		if (err != 0) {
			LOG_ERR("Dump interrupted after %u samples, %u still buffered", sent,
				(unsigned int)sample_count);
			return err;
		}
		sample_buf_pop();
		sent++;
	}

	LOG_INF("Dump complete, %u samples published", sent);
	return 0;
}

/* Called right after a successful NBIRTH: send everything that was stockpiled, then close the
 * connection so that the modem can go back to sleep.
 */
static void dump_and_disconnect(struct s_object *user_object)
{
	ARG_UNUSED(user_object);

	if (dump_samples() != 0) {
		/* The unsent samples stay buffered. Drop the connection and let the normal
		 * reconnect path retry once the reconnection timeout has elapsed.
		 */
		delay_next_connect = true;
		(void)mqtt_helper_disconnect();
		return;
	}

	dump_requested = false;

	/* A clean MQTT disconnect does not make the broker send the Last Will, so tell the host
	 * explicitly that the node is going offline. node_born is deliberately left set here:
	 * connected_exit() clears it once the disconnect has actually happened, which also
	 * prevents a second NBIRTH from being published on this connection.
	 */
	if (IS_ENABLED(CONFIG_MQTT_SAMPLE_TRANSPORT_PUBLISH_NDEATH_AFTER_DUMP)) {
		(void)sparkplug_publish_death();
	}

	LOG_INF("Closing the MQTT connection until the next dump");
	(void)mqtt_helper_disconnect();
}

/* Start a dump (connect to the broker) once enough samples have been stockpiled. */
static void maybe_start_dump(void)
{
	if (dump_requested || sample_count < CONFIG_MQTT_SAMPLE_TRANSPORT_DUMP_SAMPLE_COUNT) {
		return;
	}

	dump_requested = true;
	LOG_INF("%u samples stockpiled, connecting to dump them", (unsigned int)sample_count);

	/* If the network is down the connect is scheduled when it comes back up. */
	if (s_obj.status == NETWORK_CONNECTED) {
		k_work_reschedule_for_queue(&transport_queue, &connect_work, K_NO_WAIT);
	}
}

static int maybe_publish_birth(struct s_object *user_object)
{
	const struct payload *birth_sample;
	bool from_stockpile;
	int err;

	if (!user_object->host_online || !user_object->host_state_subscribed ||
	    !user_object->node_command_subscribed || user_object->node_born) {
		return 0;
	}
	if (!user_object->sample_seen) {
		return 0;
	}

	/* The NBIRTH carries the oldest stockpiled sample. It is then removed from the stockpile
	 * so the dump that follows stays chronological and no reading is published twice.
	 */
	birth_sample = sample_buf_peek();
	from_stockpile = (birth_sample != NULL);
	if (!from_stockpile) {
		birth_sample = &user_object->payload;
	}

	err = sparkplug_publish_birth(birth_sample);
	if (err == 0) {
		if (from_stockpile) {
			sample_buf_pop();
		}
		user_object->node_born = true;
		dump_and_disconnect(user_object);
	}
	return err;
}

/* Callback handlers from MQTT helper library.
 * The functions are called whenever specific MQTT packets are received from the broker, or
 * some library state has changed.
 */
static void on_mqtt_connack(enum mqtt_conn_return_code return_code, bool session_present)
{
	ARG_UNUSED(session_present);
	if (bootstrap_connection_used) {
		active_connection_seen = true;
	}

	if (return_code != MQTT_CONNECTION_ACCEPTED) {
		LOG_ERR("MQTT broker rejected connection, return code: %d", return_code);
		k_work_reschedule_for_queue(&transport_queue, &connect_work,
					    K_SECONDS(CONFIG_MQTT_SAMPLE_TRANSPORT_RECONNECTION_TIMEOUT_SECONDS));
		return;
	}

	transport_event_publish(CONNECTED, 0U);
}

static int sparkplug_prepare_death_will(void)
{
	size_t payload_size;
	int64_t timestamp_ms;
	int err = date_time_now(&timestamp_ms);

	if (err != 0) {
		return err;
	}
	err = sparkplug_encode_death(death_will_payload, sizeof(death_will_payload),
				     &payload_size, bd_seq, (uint64_t)timestamp_ms);
	if (err != 0) {
		return err;
	}

	death_will_topic.topic.utf8 = death_topic;
	death_will_topic.topic.size = death_topic_size;
	death_will_topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	death_will_message.utf8 = death_will_payload;
	death_will_message.size = payload_size;
	death_will_installed = true;
	return 0;
}

static bool on_mqtt_all_events(struct mqtt_client *client, const struct mqtt_evt *event)
{
	if (event->type == MQTT_EVT_CONNACK &&
	    event->param.connack.return_code == MQTT_CONNECTION_ACCEPTED &&
	    !death_will_installed) {
		int err = sparkplug_prepare_death_will();

		if (err != 0) {
			LOG_ERR("Failed to create Sparkplug NDEATH Last Will, error: %d", err);
			return true;
		}

		client->will_topic = &death_will_topic;
		client->will_message = &death_will_message;
		client->will_retain = false;
		death_will_installed = true;
		LOG_INF("Prepared Sparkplug NDEATH Last Will for reconnect");
	}

	return true;
}

static void on_mqtt_disconnect(int result)
{
	ARG_UNUSED(result);
	transport_event_publish(DISCONNECTED, 0U);
}

static int json_value_offset(const struct mqtt_helper_buf *payload, const char *key,
			     size_t *value_offset)
{
	size_t key_size = strlen(key);

	for (size_t i = 0; i + key_size <= payload->size; i++) {
		if (memcmp(payload->ptr + i, key, key_size) != 0) {
			continue;
		}

		i += key_size;
		while (i < payload->size && (payload->ptr[i] == ' ' || payload->ptr[i] == '\t' ||
					     payload->ptr[i] == '\r' || payload->ptr[i] == '\n')) {
			i++;
		}
		if (i >= payload->size || payload->ptr[i++] != ':') {
			return -EINVAL;
		}
		while (i < payload->size && (payload->ptr[i] == ' ' || payload->ptr[i] == '\t' ||
					     payload->ptr[i] == '\r' || payload->ptr[i] == '\n')) {
			i++;
		}
		*value_offset = i;
		return 0;
	}

	return -ENOENT;
}

static int host_state_payload(const struct mqtt_helper_buf *payload, bool *online,
			      uint64_t *timestamp_ms)
{
	size_t value_offset;
	size_t timestamp_offset;
	uint64_t timestamp = 0U;
	int err;

	err = json_value_offset(payload, "\"online\"", &value_offset);
	if (err != 0) {
		return err;
	}
	if (value_offset + 4U <= payload->size &&
	    memcmp(payload->ptr + value_offset, "true", 4U) == 0) {
		*online = true;
	} else if (value_offset + 5U <= payload->size &&
		   memcmp(payload->ptr + value_offset, "false", 5U) == 0) {
		*online = false;
	} else {
		return -EINVAL;
	}

	err = json_value_offset(payload, "\"timestamp\"", &timestamp_offset);
	if (err != 0 || timestamp_offset >= payload->size || payload->ptr[timestamp_offset] < '0' ||
	    payload->ptr[timestamp_offset] > '9') {
		return -EINVAL;
	}
	for (size_t i = timestamp_offset; i < payload->size && payload->ptr[i] >= '0' &&
	     payload->ptr[i] <= '9'; i++) {
		uint8_t digit = (uint8_t)(payload->ptr[i] - '0');

		if (timestamp > (UINT64_MAX - digit) / 10U) {
			return -ERANGE;
		}
		timestamp = timestamp * 10U + digit;
	}
	*timestamp_ms = timestamp;
	return 0;
}

static void on_mqtt_publish(struct mqtt_helper_buf topic, struct mqtt_helper_buf payload)
{
	if (topic.size == sizeof(HOST_STATE_TOPIC) - 1 &&
	    memcmp(topic.ptr, HOST_STATE_TOPIC, sizeof(HOST_STATE_TOPIC) - 1) == 0) {
		bool online;
		uint64_t timestamp_ms;
		int err = host_state_payload(&payload, &online, &timestamp_ms);

		if (err != 0) {
			LOG_WRN("Could not parse Sparkplug Host STATE payload");
		} else if (online) {
			transport_event_publish(HOST_ONLINE, timestamp_ms);
		} else {
			transport_event_publish(HOST_OFFLINE, timestamp_ms);
		}
		return;
	}

	if (topic.size == sizeof(NODE_COMMAND_TOPIC) - 1 &&
	    memcmp(topic.ptr, NODE_COMMAND_TOPIC, sizeof(NODE_COMMAND_TOPIC) - 1) == 0) {
		bool requested;
		int err = sparkplug_decode_rebirth((const uint8_t *)payload.ptr, payload.size,
					   &requested);

		if (err != 0) {
			LOG_WRN("Could not decode Sparkplug NCMD payload: %d", err);
		} else if (requested) {
			transport_event_publish(REBIRTH_REQUESTED, 0U);
		}
		return;
	}

	LOG_WRN("Ignoring unexpected incoming MQTT message on %.*s", (int)topic.size,
		topic.ptr);
}

static void on_mqtt_suback(uint16_t message_id, int result)
{
	if ((message_id == HOST_STATE_SUBSCRIPTION_ID ||
	     message_id == NODE_COMMAND_SUBSCRIPTION_ID) && result >= 0 && result <= 2) {
		transport_event_publish(message_id == HOST_STATE_SUBSCRIPTION_ID ?
					 HOST_STATE_SUBSCRIBED : NODE_COMMAND_SUBSCRIBED,
					 0U);
		LOG_INF("Sparkplug MQTT subscription acknowledged: %u", message_id);
	} else if (message_id == HOST_STATE_SUBSCRIPTION_ID ||
		   message_id == NODE_COMMAND_SUBSCRIPTION_ID) {
		LOG_ERR("Sparkplug subscription %u failed, result: %d", message_id, result);
	}
}

static int subscribe_to_host_state(void)
{
	struct mqtt_topic topic = {
		.topic.utf8 = (const uint8_t *)HOST_STATE_TOPIC,
		.topic.size = sizeof(HOST_STATE_TOPIC) - 1,
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};
	struct mqtt_subscription_list list = {
		.list = &topic,
		.list_count = 1,
		.message_id = HOST_STATE_SUBSCRIPTION_ID,
	};
	int err = mqtt_helper_subscribe(&list);

	if (err != 0) {
		LOG_ERR("Failed to subscribe to Sparkplug Host STATE, error: %d", err);
	}
	return err;
}

static int subscribe_to_node_commands(void)
{
	struct mqtt_topic topic = {
		.topic.utf8 = (const uint8_t *)NODE_COMMAND_TOPIC,
		.topic.size = sizeof(NODE_COMMAND_TOPIC) - 1,
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};
	struct mqtt_subscription_list list = {
		.list = &topic,
		.list_count = 1,
		.message_id = NODE_COMMAND_SUBSCRIPTION_ID,
	};
	int err = mqtt_helper_subscribe(&list);

	if (err != 0) {
		LOG_ERR("Failed to subscribe to Sparkplug NCMD, error: %d", err);
	}
	return err;
}

/* Connect work - Used to establish a connection to the MQTT broker and schedule reconnection
 * attempts.
 */
static void connect_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	int err;
	struct mqtt_helper_conn_params conn_params = {
		.hostname.ptr = CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME,
		.hostname.size = strlen(CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME),
		.device_id.ptr = client_id,
		.device_id.size = strlen(client_id),
	};

	err = client_id_get(client_id, sizeof(client_id));
	if (err) {
		LOG_ERR("client_id_get, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	err = sparkplug_topics_init();
	if (err) {
		LOG_ERR("sparkplug_topics_init, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	if (bootstrap_connection_used) {
		if (active_connection_seen) {
			bd_seq++;
			active_connection_seen = false;
		}
		err = bd_seq_settings_save();
		if (err != 0) {
			LOG_ERR("Could not persist Sparkplug bdSeq: %d", err);
			SEND_FATAL_ERROR();
			return;
		}
		err = sparkplug_prepare_death_will();
		if (err != 0) {
			LOG_WRN("Waiting for valid UTC time before registering NDEATH Will: %d", err);
			k_work_reschedule_for_queue(&transport_queue, &connect_work,
						    K_SECONDS(CONFIG_MQTT_SAMPLE_TRANSPORT_RECONNECTION_TIMEOUT_SECONDS));
			return;
		}
	}

	err = mqtt_helper_connect(&conn_params);
	if (err) {
		LOG_ERR("Failed connecting to MQTT, error code: %d", err);
		k_work_reschedule_for_queue(&transport_queue, &connect_work,
					    K_SECONDS(CONFIG_MQTT_SAMPLE_TRANSPORT_RECONNECTION_TIMEOUT_SECONDS));
	}
}

/* Zephyr State Machine framework handlers */

/* Function executed when the module enters the disconnected state. */
static void disconnected_entry(void *o)
{
	struct s_object *user_object = o;

	/* Reschedule a connection attempt if we are connected to network, we enter the
	 * disconnected state and a dump is still due. Between dumps the broker connection is
	 * intentionally left closed so that the modem can sleep.
	 */
	if (user_object->status == NETWORK_CONNECTED && dump_requested) {
		k_timeout_t delay = K_NO_WAIT;

		if (delay_next_connect) {
			delay = K_SECONDS(CONFIG_MQTT_SAMPLE_TRANSPORT_RECONNECTION_TIMEOUT_SECONDS);
			delay_next_connect = false;
		}

		k_work_reschedule_for_queue(&transport_queue, &connect_work, delay);
	}
}

/* Function executed when the module is in the disconnected state. */
static enum smf_state_result disconnected_run(void *o)
{
	struct s_object *user_object = o;

	if ((user_object->status == NETWORK_DISCONNECTED) && (user_object->chan == &NETWORK_CHAN)) {
		/* If NETWORK_DISCONNECTED is received after the MQTT connection is closed,
		 * we cancel the connect work if it is onging.
		 */
		k_work_cancel_delayable(&connect_work);
	}

	if ((user_object->status == NETWORK_CONNECTED) && (user_object->chan == &NETWORK_CHAN) &&
	    dump_requested) {

		/* Wait for 5 seconds to ensure that the network stack is ready before
		 * attempting to connect to MQTT. This delay is only needed when building for
		 * Wi-Fi.
		 */
		k_work_reschedule_for_queue(&transport_queue, &connect_work, K_SECONDS(5));
	}

	return SMF_EVENT_HANDLED;
}

/* Function executed when the module enters the connected state. */
static void connected_entry(void *o)
{
	struct s_object *user_object = o;

	/* Cancel any ongoing connect work when we enter connected state */
	k_work_cancel_delayable(&connect_work);

	if (!death_will_installed) {
		LOG_ERR("Sparkplug NDEATH Last Will was not installed");
		(void)mqtt_helper_disconnect();
		return;
	}

	if (!bootstrap_connection_used) {
		/* The helper does not expose runtime binary Will fields. The first
		 * successful CONNECT installs them on its client object; reconnect so
		 * the broker receives the Protobuf NDEATH will in CONNECT.
		 */
		bootstrap_connection_used = true;
		LOG_INF("Reconnecting once to register the Sparkplug NDEATH Last Will");
		(void)mqtt_helper_disconnect();
		return;
	}

	LOG_INF("Connected to MQTT broker: %s", CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME);
	LOG_INF("Sparkplug Edge Node: %s", client_id);
	user_object->host_online = false;
	user_object->node_born = false;
	user_object->host_state_subscribed = false;
	user_object->node_command_subscribed = false;
	if (subscribe_to_node_commands() != 0 || subscribe_to_host_state() != 0) {
		(void)mqtt_helper_disconnect();
	}
}

/* Function executed when the module is in the connected state. */
static enum smf_state_result connected_run(void *o)
{
	struct s_object *user_object = o;

	if ((user_object->status == NETWORK_DISCONNECTED) && (user_object->chan == &NETWORK_CHAN)) {
		/* Explicitly disconnect the MQTT transport when losing network connectivity.
		 * This is to cleanup any internal library state.
		 * The call to this function will cause on_mqtt_disconnect() to be called.
		 */
		if (user_object->node_born) {
			(void)sparkplug_publish_death();
			user_object->node_born = false;
		}
		(void)mqtt_helper_disconnect();
		return SMF_EVENT_HANDLED;
	}

	/* Samples are stockpiled in transport_task() and transmitted in one burst from
	 * maybe_publish_birth() once the Sparkplug session is established, so there is nothing
	 * to do per sample here.
	 */
	return SMF_EVENT_HANDLED;
}

/* Function executed when the module exits the connected state. */
static void connected_exit(void *o)
{
	struct s_object *user_object = o;

	user_object->host_online = false;
	user_object->node_born = false;
	user_object->host_state_subscribed = false;
	user_object->node_command_subscribed = false;

	LOG_INF("Disconnected from MQTT broker");
}

/* Construct state table */
static const struct smf_state state[] = {
	[MQTT_DISCONNECTED] = SMF_CREATE_STATE(disconnected_entry, disconnected_run, NULL,
					       NULL, NULL),
	[MQTT_CONNECTED] = SMF_CREATE_STATE(connected_entry, connected_run, connected_exit,
					    NULL, NULL),
};

static void transport_task(void)
{
	int err;
	const struct zbus_channel *chan;
	enum network_status status;
	struct payload payload;
	struct mqtt_helper_cfg cfg = {
		.cb = {
			.on_all_events = on_mqtt_all_events,
			.on_connack = on_mqtt_connack,
			.on_disconnect = on_mqtt_disconnect,
			.on_publish = on_mqtt_publish,
			.on_suback = on_mqtt_suback,
		},
	};

	/* Initialize and start application workqueue.
	 * This workqueue can be used to offload tasks and/or as a timer when wanting to
	 * schedule functionality using the 'k_work' API.
	 */
	k_work_queue_init(&transport_queue);
	k_work_queue_start(&transport_queue, stack_area,
			   K_THREAD_STACK_SIZEOF(stack_area),
			   K_HIGHEST_APPLICATION_THREAD_PRIO,
			   NULL);

	err = mqtt_helper_init(&cfg);
	if (err) {
		LOG_ERR("mqtt_helper_init, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	err = bd_seq_settings_init();
	if (err != 0) {
		LOG_ERR("Could not initialize persistent Sparkplug bdSeq: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	/* Set initial state */
	smf_set_initial(SMF_CTX(&s_obj), &state[MQTT_DISCONNECTED]);

	while (!zbus_sub_wait(&transport, &chan, K_FOREVER)) {

		s_obj.chan = chan;

		if (&NETWORK_CHAN == chan) {

			err = zbus_chan_read(&NETWORK_CHAN, &status, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}

			s_obj.status = status;

			err = smf_run_state(SMF_CTX(&s_obj));
			if (err) {
				LOG_ERR("smf_run_state, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}
		}

		if (&PAYLOAD_CHAN == chan) {

			err = zbus_chan_read(&PAYLOAD_CHAN, &payload, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}

			s_obj.payload = payload;
			s_obj.sample_seen = true;

			/* Stockpile the sample; connect and dump once enough have been collected. */
			sample_buf_push(&payload);
			maybe_start_dump();
		}

		if (&TRANSPORT_PRIVATE_CHANNEL == chan) {

			struct transport_event event;

			err = zbus_chan_read(&TRANSPORT_PRIVATE_CHANNEL, &event, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}

			/* Process MQTT events and state messages in this thread. */
			switch (event.type) {
			case CONNECTED:
				smf_set_state(SMF_CTX(&s_obj), &state[MQTT_CONNECTED]);
				break;
			case DISCONNECTED:
				smf_set_state(SMF_CTX(&s_obj), &state[MQTT_DISCONNECTED]);
				break;
			case HOST_ONLINE:
				if (!s_obj.host_timestamp_seen ||
				    event.timestamp_ms >= s_obj.host_timestamp_ms) {
					s_obj.host_timestamp_ms = event.timestamp_ms;
					s_obj.host_timestamp_seen = true;
					s_obj.host_online = true;
					if (death_will_installed && bootstrap_connection_used) {
						(void)maybe_publish_birth(&s_obj);
					}
				}
				break;
			case HOST_OFFLINE:
				if (!s_obj.host_timestamp_seen ||
				    event.timestamp_ms >= s_obj.host_timestamp_ms) {
					s_obj.host_timestamp_ms = event.timestamp_ms;
					s_obj.host_timestamp_seen = true;
					if (s_obj.node_born) {
						(void)sparkplug_publish_death();
						s_obj.node_born = false;
					}
					s_obj.host_online = false;
					/* The stockpile is kept; retry after the timeout. */
					delay_next_connect = true;
					(void)mqtt_helper_disconnect();
				}
				break;
			case HOST_STATE_SUBSCRIBED:
				s_obj.host_state_subscribed = true;
				(void)maybe_publish_birth(&s_obj);
				break;
			case NODE_COMMAND_SUBSCRIBED:
				s_obj.node_command_subscribed = true;
				(void)maybe_publish_birth(&s_obj);
				break;
			case REBIRTH_REQUESTED:
				if (s_obj.host_online && s_obj.host_state_subscribed &&
				    s_obj.node_command_subscribed && s_obj.sample_seen) {
					s_obj.node_born = false;
					if (sparkplug_publish_birth(&s_obj.payload) == 0) {
						s_obj.node_born = true;
					}
				}
				break;
			default:
				LOG_WRN("Unknown MQTT event type: %d", event.type);
				break;
			}
		}
	}
}

K_THREAD_DEFINE(transport_task_id,
		CONFIG_MQTT_SAMPLE_TRANSPORT_THREAD_STACK_SIZE,
		transport_task, NULL, NULL, NULL, 3, 0, 0);