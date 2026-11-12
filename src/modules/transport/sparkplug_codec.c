/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Encodes the project's Sparkplug B messages using the Protobuf wire format.
 * The schema is Eclipse Tahu's sparkplug_b.proto; only Payload and numeric
 * Metric fields used by this application are encoded here.
 */

#include "sparkplug_codec.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

/* Sparkplug DataType enum values from sparkplug_b.proto. */
#define SPARKPLUG_TYPE_INT64 4U
#define SPARKPLUG_TYPE_UINT32 7U
#define SPARKPLUG_TYPE_DOUBLE 10U
#define SPARKPLUG_TYPE_BOOLEAN 11U

/* Protobuf field numbers from sparkplug_b.proto. */
#define PAYLOAD_FIELD_METRICS 2U
#define PAYLOAD_FIELD_SEQUENCE 3U
#define PAYLOAD_FIELD_TIMESTAMP 1U
#define METRIC_FIELD_NAME 1U
#define METRIC_FIELD_ALIAS 2U
#define METRIC_FIELD_TIMESTAMP 3U
#define METRIC_FIELD_DATATYPE 4U
#define METRIC_FIELD_IS_NULL 7U
#define METRIC_FIELD_INT_VALUE 10U
#define METRIC_FIELD_LONG_VALUE 11U
#define METRIC_FIELD_DOUBLE_VALUE 13U
#define METRIC_FIELD_BOOLEAN_VALUE 14U

enum protobuf_wire_type {
	PROTOBUF_WIRE_VARINT = 0,
	PROTOBUF_WIRE_FIXED64 = 1,
	PROTOBUF_WIRE_LENGTH_DELIMITED = 2,
};

struct protobuf_writer {
	uint8_t *buffer;
	size_t capacity;
	size_t length;
};

static int append_byte(struct protobuf_writer *writer, uint8_t value)
{
	if (writer->length >= writer->capacity) {
		return -EMSGSIZE;
	}

	writer->buffer[writer->length++] = value;
	return 0;
}

static int append_varint(struct protobuf_writer *writer, uint64_t value)
{
	int err;

	while (value >= 0x80U) {
		err = append_byte(writer, (uint8_t)(value | 0x80U));
		if (err != 0) {
			return err;
		}
		value >>= 7;
	}

	return append_byte(writer, (uint8_t)value);
}

static int append_key(struct protobuf_writer *writer, uint32_t field,
		      enum protobuf_wire_type wire_type)
{
	return append_varint(writer, ((uint64_t)field << 3) | (uint32_t)wire_type);
}

static int append_uint_field(struct protobuf_writer *writer, uint32_t field,
			     uint64_t value)
{
	int err = append_key(writer, field, PROTOBUF_WIRE_VARINT);

	return err == 0 ? append_varint(writer, value) : err;
}

static int append_bytes_field(struct protobuf_writer *writer, uint32_t field,
			      const uint8_t *data, size_t size)
{
	int err = append_key(writer, field, PROTOBUF_WIRE_LENGTH_DELIMITED);

	if (err == 0) {
		err = append_varint(writer, size);
	}
	if (err != 0) {
		return err;
	}
	if (size > writer->capacity - writer->length) {
		return -EMSGSIZE;
	}

	memcpy(&writer->buffer[writer->length], data, size);
	writer->length += size;
	return 0;
}

static int append_double_field(struct protobuf_writer *writer, uint32_t field,
			       double value)
{
	uint64_t bits;
	int err = append_key(writer, field, PROTOBUF_WIRE_FIXED64);

	if (err != 0) {
		return err;
	}
	if (sizeof(bits) != sizeof(value) || sizeof(bits) > writer->capacity - writer->length) {
		return -EMSGSIZE;
	}

	memcpy(&bits, &value, sizeof(bits));
	bits = sys_cpu_to_le64(bits);
	memcpy(&writer->buffer[writer->length], &bits, sizeof(bits));
	writer->length += sizeof(bits);
	return 0;
}

static int append_metric(struct protobuf_writer *payload, const uint8_t *metric,
			 size_t metric_length)
{
	return append_bytes_field(payload, PAYLOAD_FIELD_METRICS, metric, metric_length);
}

static int append_bd_seq_metric(struct protobuf_writer *payload, uint8_t bd_seq,
				uint64_t timestamp_ms)
{
	uint8_t metric_buffer[32];
	struct protobuf_writer metric = {
		.buffer = metric_buffer,
		.capacity = sizeof(metric_buffer),
	};
	int err;

	err = append_bytes_field(&metric, METRIC_FIELD_NAME,
				 (const uint8_t *)"bdSeq", sizeof("bdSeq") - 1);
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_TIMESTAMP, timestamp_ms);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_DATATYPE, SPARKPLUG_TYPE_INT64);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_LONG_VALUE, bd_seq);
	}
	if (err != 0) {
		return err;
	}

	return append_metric(payload, metric.buffer, metric.length);
}

static int append_birth_double_metric(struct protobuf_writer *payload, const char *name,
				      uint64_t alias, double value,
				      uint64_t timestamp_ms)
{
	uint8_t metric_buffer[64];
	struct protobuf_writer metric = {
		.buffer = metric_buffer,
		.capacity = sizeof(metric_buffer),
	};
	int err;

	err = append_bytes_field(&metric, METRIC_FIELD_NAME,
				 (const uint8_t *)name, strlen(name));
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_ALIAS, alias);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_TIMESTAMP, timestamp_ms);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_DATATYPE, SPARKPLUG_TYPE_DOUBLE);
	}
	if (err == 0) {
		err = append_double_field(&metric, METRIC_FIELD_DOUBLE_VALUE, value);
	}
	if (err != 0) {
		return err;
	}

	return append_metric(payload, metric.buffer, metric.length);
}

static int append_birth_uptime_metric(struct protobuf_writer *payload, uint32_t value,
				      uint64_t timestamp_ms)
{
	uint8_t metric_buffer[40];
	struct protobuf_writer metric = {
		.buffer = metric_buffer,
		.capacity = sizeof(metric_buffer),
	};
	int err = append_bytes_field(&metric, METRIC_FIELD_NAME,
				     (const uint8_t *)"uptime_ms",
				     sizeof("uptime_ms") - 1U);

	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_ALIAS, 5U);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_TIMESTAMP, timestamp_ms);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_DATATYPE, SPARKPLUG_TYPE_UINT32);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_INT_VALUE, value);
	}
	if (err != 0) {
		return err;
	}

	return append_metric(payload, metric.buffer, metric.length);
}

static int append_rebirth_control_metric(struct protobuf_writer *payload,
					 uint64_t timestamp_ms)
{
	uint8_t metric_buffer[48];
	struct protobuf_writer metric = {
		.buffer = metric_buffer,
		.capacity = sizeof(metric_buffer),
	};
	int err = append_bytes_field(&metric, METRIC_FIELD_NAME,
				     (const uint8_t *)"Node Control/Rebirth",
				     sizeof("Node Control/Rebirth") - 1U);

	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_TIMESTAMP, timestamp_ms);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_DATATYPE,
					SPARKPLUG_TYPE_BOOLEAN);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_BOOLEAN_VALUE, 0U);
	}
	if (err != 0) {
		return err;
	}

	return append_metric(payload, metric.buffer, metric.length);
}

static int append_double_metric(struct protobuf_writer *payload, uint64_t alias,
				 double value, uint64_t timestamp_ms)
{
	uint8_t metric_buffer[24];
	struct protobuf_writer metric = {
		.buffer = metric_buffer,
		.capacity = sizeof(metric_buffer),
	};
	int err = append_uint_field(&metric, METRIC_FIELD_ALIAS, alias);

	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_TIMESTAMP, timestamp_ms);
	}
	if (err == 0) {
		err = append_double_field(&metric, METRIC_FIELD_DOUBLE_VALUE, value);
	}
	if (err != 0) {
		return err;
	}

	return append_metric(payload, metric.buffer, metric.length);
}

static int append_uptime_metric(struct protobuf_writer *payload, uint32_t uptime_ms,
				uint64_t timestamp_ms)
{
	uint8_t metric_buffer[24];
	struct protobuf_writer metric = {
		.buffer = metric_buffer,
		.capacity = sizeof(metric_buffer),
	};
	int err = append_uint_field(&metric, METRIC_FIELD_ALIAS, 5U);

	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_TIMESTAMP, timestamp_ms);
	}
	if (err == 0) {
		err = append_uint_field(&metric, METRIC_FIELD_INT_VALUE, uptime_ms);
	}
	if (err != 0) {
		return err;
	}

	return append_metric(payload, metric.buffer, metric.length);
}

int sparkplug_encode_birth(uint8_t *buffer, size_t capacity, size_t *length,
			   uint8_t bd_seq, const struct payload *sample,
			   uint64_t timestamp_ms)
{
	struct protobuf_writer payload = {
		.buffer = buffer,
		.capacity = capacity,
	};
	int err;

	if (buffer == NULL || length == NULL || sample == NULL) {
		return -EINVAL;
	}

	err = append_uint_field(&payload, PAYLOAD_FIELD_TIMESTAMP, timestamp_ms);
	if (err == 0) {
		err = append_bd_seq_metric(&payload, bd_seq, timestamp_ms);
	}
	if (err == 0) {
		err = append_birth_double_metric(&payload, "temperature_c", 1U,
						 sample->temperature_c, sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_birth_double_metric(&payload, "humidity_pct", 2U,
						 sample->humidity_pct, sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_birth_double_metric(&payload, "pressure_kpa", 3U,
						 sample->pressure_kpa, sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_birth_double_metric(&payload, "gas_ohm", 4U,
						 sample->gas_ohm, sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_birth_uptime_metric(&payload, sample->uptime_ms,
						 sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_rebirth_control_metric(&payload, timestamp_ms);
	}
	if (err == 0) {
		err = append_uint_field(&payload, PAYLOAD_FIELD_SEQUENCE, 0U);
	}
	if (err == 0) {
		*length = payload.length;
	}

	return err;
}

int sparkplug_encode_data(uint8_t *buffer, size_t capacity, size_t *length,
			  const struct payload *sample, uint8_t sequence)
{
	struct protobuf_writer payload = {
		.buffer = buffer,
		.capacity = capacity,
	};
	int err;

	if (buffer == NULL || length == NULL || sample == NULL) {
		return -EINVAL;
	}

	err = append_uint_field(&payload, PAYLOAD_FIELD_TIMESTAMP, sample->timestamp_ms);
	if (err == 0) {
		err = append_double_metric(&payload, 1U, sample->temperature_c,
					   sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_double_metric(&payload, 2U, sample->humidity_pct,
					   sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_double_metric(&payload, 3U, sample->pressure_kpa,
					   sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_double_metric(&payload, 4U, sample->gas_ohm,
					   sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_uptime_metric(&payload, sample->uptime_ms,
					   sample->timestamp_ms);
	}
	if (err == 0) {
		err = append_uint_field(&payload, PAYLOAD_FIELD_SEQUENCE, sequence);
	}
	if (err == 0) {
		*length = payload.length;
	}

	return err;
}

int sparkplug_encode_death(uint8_t *buffer, size_t capacity, size_t *length,
			   uint8_t bd_seq, uint64_t timestamp_ms)
{
	struct protobuf_writer payload = {
		.buffer = buffer,
		.capacity = capacity,
	};
	int err;

	if (buffer == NULL || length == NULL) {
		return -EINVAL;
	}

	err = append_uint_field(&payload, PAYLOAD_FIELD_TIMESTAMP, timestamp_ms);
	if (err == 0) {
		err = append_bd_seq_metric(&payload, bd_seq, timestamp_ms);
	}
	if (err == 0) {
		*length = payload.length;
	}

	return err;
}

static int protobuf_read_varint(const uint8_t *buffer, size_t length, size_t *position,
				uint64_t *value)
{
	*value = 0;
	for (unsigned int shift = 0; shift < 70; shift += 7) {
		uint8_t byte;

		if (*position >= length) {
			return -EINVAL;
		}
		byte = buffer[(*position)++];
		if (shift == 63 && (byte & 0xfeU) != 0) {
			return -EINVAL;
		}
		*value |= (uint64_t)(byte & 0x7fU) << shift;
		if ((byte & 0x80U) == 0) {
			return 0;
		}
	}
	return -EINVAL;
}

static int protobuf_skip(const uint8_t *buffer, size_t length, size_t *position,
			uint8_t wire_type)
{
	uint64_t field_length;

	switch (wire_type) {
	case PROTOBUF_WIRE_VARINT:
		return protobuf_read_varint(buffer, length, position, &field_length);
	case PROTOBUF_WIRE_FIXED64:
		if (length - *position < 8U) {
			return -EINVAL;
		}
		*position += 8U;
		return 0;
	case PROTOBUF_WIRE_LENGTH_DELIMITED:
		if (protobuf_read_varint(buffer, length, position, &field_length) != 0 ||
		    field_length > length - *position) {
			return -EINVAL;
		}
		*position += (size_t)field_length;
		return 0;
	case 5U:
		if (length - *position < 4U) {
			return -EINVAL;
		}
		*position += 4U;
		return 0;
	default:
		return -EINVAL;
	}
}

static int metric_is_rebirth(const uint8_t *buffer, size_t length, bool *is_rebirth)
{
	size_t position = 0;
	bool name_matches = false;
	bool value_is_true = false;

	while (position < length) {
		uint64_t key;
		uint32_t field;
		uint8_t wire;

		if (protobuf_read_varint(buffer, length, &position, &key) != 0 || key == 0) {
			return -EINVAL;
		}
		field = (uint32_t)(key >> 3);
		wire = (uint8_t)(key & 7U);
		if (field == METRIC_FIELD_NAME && wire == PROTOBUF_WIRE_LENGTH_DELIMITED) {
			uint64_t name_length;

			if (protobuf_read_varint(buffer, length, &position, &name_length) != 0 ||
			    name_length > length - position) {
				return -EINVAL;
			}
			name_matches = name_length == sizeof("Node Control/Rebirth") - 1U &&
				memcmp(buffer + position, "Node Control/Rebirth",
				       sizeof("Node Control/Rebirth") - 1U) == 0;
			position += (size_t)name_length;
		} else if (field == 14U && wire == PROTOBUF_WIRE_VARINT) {
			uint64_t value;

			if (protobuf_read_varint(buffer, length, &position, &value) != 0) {
				return -EINVAL;
			}
			value_is_true = value != 0;
		} else if (protobuf_skip(buffer, length, &position, wire) != 0) {
			return -EINVAL;
		}
	}

	*is_rebirth = name_matches && value_is_true;
	return 0;
}

int sparkplug_decode_rebirth(const uint8_t *buffer, size_t length, bool *requested)
{
	size_t position = 0;

	if (buffer == NULL || requested == NULL) {
		return -EINVAL;
	}
	*requested = false;

	while (position < length) {
		uint64_t key;
		uint32_t field;
		uint8_t wire;

		if (protobuf_read_varint(buffer, length, &position, &key) != 0 || key == 0) {
			return -EINVAL;
		}
		field = (uint32_t)(key >> 3);
		wire = (uint8_t)(key & 7U);
		if (field == PAYLOAD_FIELD_METRICS && wire == PROTOBUF_WIRE_LENGTH_DELIMITED) {
			uint64_t metric_length;
			bool is_rebirth;

			if (protobuf_read_varint(buffer, length, &position, &metric_length) != 0 ||
			    metric_length > length - position ||
			    metric_is_rebirth(buffer + position, (size_t)metric_length,
					     &is_rebirth) != 0) {
				return -EINVAL;
			}
			*requested = *requested || is_rebirth;
			position += (size_t)metric_length;
		} else if (protobuf_skip(buffer, length, &position, wire) != 0) {
			return -EINVAL;
		}
	}

	return 0;
}
