#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "app.h"

#define BT_SCAN_MAX_RESULTS   16

typedef struct {
    char name[BT_DEVICE_NAME_MAX];
    char addr[BT_ADDR_STR_LEN];
    uint8_t addr_type;
    int8_t rssi;
    bool is_obd_hint;
} bt_device_info_t;

typedef enum {
    BT_FAIL_NONE = 0,
    BT_FAIL_SCAN,
    BT_FAIL_CONNECT,
    BT_FAIL_GATT,
    BT_FAIL_ELM,
    BT_FAIL_OBD,
} bt_fail_stage_t;

bool bt_init_stack(void);
void bt_shutdown_stack(void);

bool bt_link_up(void);
bool bt_obd_session_ready(void);
bool bt_connect_to_addr(const char *addr_str);
bool bt_connect_auto(void);
int bt_scan_devices(bt_device_info_t *list, int max_count, int duration_ms);
void bt_forget_saved(void);
void bt_disconnect(void);

bool bt_is_connected(void);
bool bt_serial_ready(void);
esp_err_t bt_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);

bt_fail_stage_t bt_get_last_fail_stage(void);
const char *bt_get_last_fail_hint(void);

/** Legacy entry points used by connectivity FSM */
bool bt_connect(void);
void bt_save_device(const char *name, const char *addr_str, uint8_t addr_type);
