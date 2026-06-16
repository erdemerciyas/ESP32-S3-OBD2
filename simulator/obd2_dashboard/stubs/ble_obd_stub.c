#include "ble_obd.h"
#include "demo_feed.h"

static ble_obd_state_t s_state = BLE_OBD_IDLE;

void ble_obd_init(void)
{
}

void ble_obd_start(void)
{
}

void ble_obd_stop(void)
{
}

void ble_obd_scan(void)
{
    s_state = BLE_OBD_SCANNING;
    demo_feed_start_connect();
}

void ble_obd_connect_saved(void)
{
    ble_obd_scan();
}

bool ble_obd_send(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return s_state == BLE_OBD_CONNECTED;
}

bool ble_obd_is_connected(void)
{
    return s_state == BLE_OBD_CONNECTED;
}

ble_obd_state_t ble_obd_get_state(void)
{
    return s_state;
}

void ble_obd_set_rx_callback(ble_obd_rx_cb_t cb)
{
    (void)cb;
}
