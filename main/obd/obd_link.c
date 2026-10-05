#include "obd_link.h"
#include "ble_obd.h"
#include "wifi_obd.h"
#include "vehicle_data.h"
#include "app_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "obd_link";
static const char *NVS_NS = "obd_link";
static const char *NVS_KEY_TYPE = "type";

#define SWITCH_TASK_STACK 4096
#define SWITCH_TASK_PRIO  3

#ifdef CONFIG_OBD_LINK_DEFAULT_WIFI
static volatile obd_link_type_t s_type = OBD_LINK_WIFI;
#else
static volatile obd_link_type_t s_type = OBD_LINK_BLE;
#endif
static volatile bool s_switching;

static const char *type_name(obd_link_type_t type)
{
    return type == OBD_LINK_WIFI ? "WiFi" : "BLE";
}

static void start_type(obd_link_type_t type)
{
    if (type == OBD_LINK_WIFI) {
        wifi_obd_start();
    } else {
        ble_obd_start();
    }
}

void obd_link_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, NVS_KEY_TYPE, &v) == ESP_OK && v <= OBD_LINK_WIFI) {
            s_type = (obd_link_type_t)v;
        }
        nvs_close(h);
    }
    app_log_info(TAG, "Transport: %s", type_name(s_type));
    ble_obd_init();   /* yalnız zamanlayıcılar; yığın start'ta açılır */
}

void obd_link_start(void)
{
    start_type(s_type);
}

void obd_link_rescan(void)
{
    if (s_switching) {
        return;
    }
    if (s_type == OBD_LINK_WIFI) {
        wifi_obd_rescan();
    } else {
        ble_obd_scan();
    }
}

bool obd_link_send(const uint8_t *data, size_t len)
{
    return s_type == OBD_LINK_WIFI ? wifi_obd_send(data, len) : ble_obd_send(data, len);
}

bool obd_link_is_connected(void)
{
    if (s_switching) {
        return false;
    }
    return s_type == OBD_LINK_WIFI ? wifi_obd_is_connected() : ble_obd_is_connected();
}

void obd_link_set_rx_callback(obd_link_rx_cb_t cb)
{
    /* Yalnızca işaretçi saklar; ikisine de verilir. */
    ble_obd_set_rx_callback(cb);
    wifi_obd_set_rx_callback(cb);
}

obd_link_type_t obd_link_get_type(void)
{
    return s_type;
}

/* Durdurma blokladığı için (NimBLE deinit, WiFi taraması) ayrı görevde. */
static void switch_task(void *arg)
{
    obd_link_type_t to = (obd_link_type_t)(intptr_t)arg;
    if (to == OBD_LINK_WIFI) {
        ble_obd_stop();
    } else {
        wifi_obd_stop();
    }
    vehicle_data_set_adapter("", "");
    vehicle_data_set_state(OBD_STATE_DISCONNECTED, to == OBD_LINK_WIFI ? "Searching WiFi..."
                                                                       : "Searching BLE...");
    start_type(to);
    app_log_info(TAG, "Transport switched to %s", type_name(to));
    s_switching = false;
    vTaskDelete(NULL);
}

void obd_link_switch(obd_link_type_t type)
{
    if (type == s_type || s_switching) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_TYPE, (uint8_t)type);
        nvs_commit(h);
        nvs_close(h);
    }

    s_switching = true;
    s_type = type;   /* UI hemen yeni seçimi göstersin */
    vehicle_data_set_state(OBD_STATE_DISCONNECTED, "Switching link...");
    if (xTaskCreate(switch_task, "link_sw", SWITCH_TASK_STACK, (void *)(intptr_t)type,
                    SWITCH_TASK_PRIO, NULL) != pdPASS) {
        app_log_error(TAG, "Switch task create failed");
        s_switching = false;
    }
}
