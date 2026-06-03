#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONN_LOG_MAX_ENTRIES 20
#define CONN_LOG_MSG_LEN     96

/* Load persisted entries from NVS (call once after nvs_flash_init). */
void conn_log_init(void);

/* Append a connection diagnostic entry. Also prints via ESP_LOG and
 * persists the ring buffer to NVS so it survives a reboot. */
void conn_log_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Print all stored entries to the serial console (ESP_LOG). */
void conn_log_dump(void);

/* Number of stored entries (0..CONN_LOG_MAX_ENTRIES). */
int conn_log_count(void);

/* Get entry by display index (0 = oldest). Returns NULL if out of range.
 * If uptime_s_out is non-NULL it receives the device uptime at log time. */
const char *conn_log_entry(int idx, uint32_t *uptime_s_out, uint32_t *seq_out);

/* Erase all entries (RAM + NVS). */
void conn_log_clear(void);

#ifdef __cplusplus
}
#endif
