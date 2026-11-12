/*
 * Sparkplug B subset encoder for the Thingy:91 X metrics used in this project.
 * Field numbers and types follow Eclipse Tahu's sparkplug_b.proto.
 */

#ifndef MQTT_SAMPLE_SPARKPLUG_CODEC_H_
#define MQTT_SAMPLE_SPARKPLUG_CODEC_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "message_channel.h"

#define SPARKPLUG_PAYLOAD_MAX_SIZE 384

int sparkplug_encode_birth(uint8_t *buffer, size_t capacity, size_t *length,
			   uint8_t bd_seq, const struct payload *sample,
			   uint64_t timestamp_ms);
int sparkplug_encode_data(uint8_t *buffer, size_t capacity, size_t *length,
			  const struct payload *sample, uint8_t sequence);
int sparkplug_encode_death(uint8_t *buffer, size_t capacity, size_t *length,
			   uint8_t bd_seq, uint64_t timestamp_ms);
int sparkplug_decode_rebirth(const uint8_t *buffer, size_t length, bool *requested);

#endif /* MQTT_SAMPLE_SPARKPLUG_CODEC_H_ */
