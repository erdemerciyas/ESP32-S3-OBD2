#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*elm327_response_cb_t)(const char *response, void *user_data);

typedef enum {
    ELM_STATE_IDLE = 0,
    ELM_STATE_INIT,
    ELM_STATE_READY,
    ELM_STATE_BUSY,
    ELM_STATE_ERROR,
} elm327_state_t;

void elm327_init(void);
void elm327_start(void);
void elm327_stop(void);

bool elm327_send_cmd(const char *cmd, elm327_response_cb_t cb, void *user_data, uint32_t timeout_ms);
bool elm327_send_cmd_prio(const char *cmd, elm327_response_cb_t cb, void *user_data,
                          uint32_t timeout_ms, bool high_priority);
bool elm327_send_cmd_sync(const char *cmd, char *response, size_t response_len, uint32_t timeout_ms);

elm327_state_t elm327_get_state(void);
bool elm327_is_ready(void);
bool elm327_is_busy(void);
uint32_t elm327_queue_depth(void);
bool elm327_can_queue(bool high_priority);

void elm327_on_rx_data(const uint8_t *data, size_t len);
