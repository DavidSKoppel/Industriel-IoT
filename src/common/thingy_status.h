/* SPDX-License-Identifier: LicenseRef-Nordic-5-Clause */

#ifndef THINGY_STATUS_H_
#define THINGY_STATUS_H_

#include <zephyr/sys/util.h>

#define THINGY_STATUS_ENABLED \
	(IS_ENABLED(CONFIG_BOARD_THINGY91X) && IS_ENABLED(CONFIG_MQTT_SAMPLE_LED))

enum thingy_status {
	THINGY_CONNECTING,
	THINGY_CONNECTED,
	THINGY_SENDING,
	THINGY_SENT,
};

#if THINGY_STATUS_ENABLED
void thingy_status_set(enum thingy_status status);
void thingy_status_error(void);
void thingy_status_replay(void);
#else
static inline void thingy_status_set(enum thingy_status status) { ARG_UNUSED(status); }
static inline void thingy_status_error(void) {}
static inline void thingy_status_replay(void) {}
#endif

#endif
