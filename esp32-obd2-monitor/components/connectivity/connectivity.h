#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app.h"
#include "bt_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONN_STATE_DISCONNECTED = 0,
    CONN_STATE_LINK_UP,
    CONN_STATE_ELM_INIT,
    CONN_STATE_OBD_READY,
    CONN_STATE_ERROR
} connectivity_state_t;

esp_err_t connectivity_start(connection_type_t type);
void connectivity_stop(void);
esp_err_t connectivity_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
connection_type_t connectivity_get_current_type(void);
bool connectivity_is_connected(void);
connectivity_state_t connectivity_get_state(void);
const char *connectivity_get_status_text(void);
esp_err_t connectivity_auto_reconnect(void);
esp_err_t connectivity_bt_scan(bt_device_info_t *list, int max_count, int *found_count);
esp_err_t connectivity_bt_connect_manual(const char *addr, const char *name, uint8_t addr_type);
esp_err_t connectivity_bt_enable_auto_mode(void);
esp_err_t connectivity_bt_disconnect(void);
esp_err_t connectivity_bt_forget(void);

#ifdef __cplusplus
}
#endif
