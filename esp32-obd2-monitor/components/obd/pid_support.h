#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mode 01 supported-PID bitmap (0100 / 0120 / 0140). */
void pid_support_reset(void);

/** Parse a Mode 01 supported-PID response (41 xx + 4 data bytes). */
void pid_support_merge_block(uint8_t query_pid, const uint8_t *data, size_t data_len);

bool pid_support_is_known(void);
bool pid_support_is_supported(uint8_t pid);
bool pid_support_should_poll(uint8_t pid);

/** Mark PID as supported after a successful live read (fallback when bitmap query fails). */
void pid_support_mark_supported(uint8_t pid);

/** Mark PID as unsupported after repeated poll failures. */
void pid_support_mark_unsupported(uint8_t pid);

#ifdef __cplusplus
}
#endif
