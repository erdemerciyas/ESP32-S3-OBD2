#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*ble_obd_rx_cb_t)(const uint8_t *data, size_t len);

typedef enum {
    BLE_OBD_IDLE = 0,
    BLE_OBD_SCANNING,
    BLE_OBD_CONNECTING,
    BLE_OBD_CONNECTED,
    BLE_OBD_DISCONNECTED,
} ble_obd_state_t;

void ble_obd_init(void);
void ble_obd_start(void);
void ble_obd_stop(void);
void ble_obd_scan(void);
void ble_obd_connect_saved(void);

bool ble_obd_send(const uint8_t *data, size_t len);
bool ble_obd_is_connected(void);
ble_obd_state_t ble_obd_get_state(void);

void ble_obd_set_rx_callback(ble_obd_rx_cb_t cb);
