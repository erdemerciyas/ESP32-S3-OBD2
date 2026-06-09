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
/** Called from bt_manager after a queued async connect completes. */
void connectivity_on_async_bt_result(bool ok);
void connectivity_maintain_bt(void);

/** Implemented in display.c — false until splash finishes. */
bool connectivity_ui_ready(void);
/** Fast FSM/metadata sync only — safe from UI thread. */
void connectivity_sync_transport_state(void);
/** ELM327/OBD probe — call from background tasks only (blocks 10–30 s). */
void connectivity_promote_obd_if_ready(void);
bool connectivity_bt_auto_connect_allowed(void);
bool connectivity_has_saved_bt_profile(void);
bool connectivity_bt_auto_connect_pending(void);
/** True while Settings BLE scan/connect job owns the radio. */
bool connectivity_is_user_busy(void);
/** UI scan/connect owns BLE — pauses background auto-reconnect. */
void connectivity_bt_ui_begin(void);
void connectivity_bt_ui_end(void);
esp_err_t connectivity_bt_scan(bt_device_info_t *list, int max_count, int *found_count,
                             int duration_ms);
esp_err_t connectivity_bt_connect_manual(const char *addr, const char *name, uint8_t addr_type);
esp_err_t connectivity_bt_enable_auto_mode(void);
esp_err_t connectivity_bt_disconnect(void);
esp_err_t connectivity_bt_forget(void);

#ifdef __cplusplus
}
#endif
