#include "bt_manager.h"
#include "bt_elm327_profiles.h"
#include "elm327_session.h"
#include "app.h"
#include "settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "bt";

extern app_settings_t g_settings;

static bool obd_ready;
static bool serial_ready;
static volatile bool connect_failed;
static volatile bool op_abort;
static bt_fail_stage_t last_fail = BT_FAIL_NONE;

#if CONFIG_BT_NIMBLE_ENABLED

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"

#include "freertos/queue.h"
#include "host/ble_store.h"
#include "esp_bt.h"

extern void ble_store_config_init(void);

#define BT_RX_BUF_SIZE 512
#define BT_SCAN_DEFAULT_MS 10000
#define BT_AUTO_CONNECT_MAX_ATTEMPTS 12
#define BT_MAX_GATT_SVCS 16
#define BT_MAX_GATT_CHRS 24
#define BT_CMD_TASK_STACK  20480
#define BT_CMD_QUEUE_LEN   4

typedef enum {
    BT_CMD_SCAN = 1,
    BT_CMD_CONNECT,
    BT_CMD_DISCONNECT,
    BT_CMD_AUTO_CONNECT,
} bt_cmd_id_t;

typedef struct {
    bt_cmd_id_t id;
    SemaphoreHandle_t done;
    bool ok;
    int scan_duration_ms;
    ble_addr_t addr;
    char name[BT_DEVICE_NAME_MAX];
} bt_cmd_msg_t;

static bool stack_started;
static bool port_inited;
static volatile bool host_synced;
static bool cmd_worker_started;
static QueueHandle_t bt_cmd_q;
static TaskHandle_t bt_cmd_task_h;
static uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t tx_char_handle;
static uint16_t rx_char_handle;
static bool notify_enabled;

static uint8_t rx_buf[BT_RX_BUF_SIZE];
static size_t rx_len;
static SemaphoreHandle_t rx_mutex;
static SemaphoreHandle_t sync_sem;
static SemaphoreHandle_t connect_sem;
static SemaphoreHandle_t gatt_disc_sem;
static SemaphoreHandle_t scan_complete_sem;

static bt_device_info_t scan_buf[BT_SCAN_MAX_RESULTS];
static int scan_count;
static bool scan_done;

static bool bt_connect_auto_internal(void);
static bool bt_connect_addr_internal(const ble_addr_t *addr, const char *display_name);
static void bt_disconnect_internal(void);
static void bt_abort_pending_ops(void);

static void bt_addr_to_str(const ble_addr_t *addr, char *out, size_t out_len)
{
    if (out == NULL || out_len < BT_ADDR_STR_LEN || addr == NULL) {
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

static bool bt_str_to_addr(const char *str, ble_addr_t *out)
{
    if (str == NULL || out == NULL) {
        return false;
    }
    unsigned int b[6];
    if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[5], &b[4], &b[3], &b[2], &b[1], &b[0]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out->val[i] = (uint8_t)b[i];
    }
    out->type = g_settings.bt_addr_type;
    return true;
}

static void bt_flush_rx(void)
{
    if (rx_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        rx_len = 0;
        xSemaphoreGive(rx_mutex);
    }
}

static int bt_append_rx(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || rx_mutex == NULL) {
        return 0;
    }
    if (xSemaphoreTake(rx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return BLE_HS_EBUSY;
    }
    size_t space = BT_RX_BUF_SIZE - rx_len;
    if (len > space) {
        len = (uint16_t)space;
    }
    if (len > 0) {
        memcpy(rx_buf + rx_len, data, len);
        rx_len += len;
    }
    xSemaphoreGive(rx_mutex);
    return 0;
}

static int bt_elm_send(const char *data, size_t len)
{
    if (!serial_ready || conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        tx_char_handle == 0 || data == NULL || len == 0) {
        return -1;
    }

    int rc = ble_gattc_write_no_rsp_flat(conn_handle, tx_char_handle, data, (uint16_t)len);
    if (rc != 0) {
        rc = ble_gattc_write_flat(conn_handle, tx_char_handle, data, (uint16_t)len, NULL, NULL);
    }
    return (rc == 0) ? (int)len : -1;
}

static int bt_elm_recv(char *buf, size_t max_len, int timeout_ms)
{
    if (buf == NULL || max_len == 0 || rx_mutex == NULL) {
        return 0;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    size_t copied = 0;

    while (xTaskGetTickCount() < deadline) {
        if (xSemaphoreTake(rx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (rx_len > 0) {
                copied = rx_len;
                if (copied >= max_len) {
                    copied = max_len - 1;
                }
                memcpy(buf, rx_buf, copied);
                buf[copied] = '\0';
                rx_len = 0;
            }
            xSemaphoreGive(rx_mutex);
        }
        if (copied > 0) {
            return (int)copied;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return 0;
}


static uint16_t disc_svc_start;
static uint16_t disc_svc_end;
static uint16_t disc_found_chr;

typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
    uint16_t uuid16;
} bt_gatt_svc_entry_t;

typedef struct {
    uint16_t val_handle;
    uint16_t uuid16;
    uint8_t properties;
} bt_gatt_chr_entry_t;

static bt_gatt_svc_entry_t gatt_svc_list[BT_MAX_GATT_SVCS];
static int gatt_svc_list_count;
static bt_gatt_chr_entry_t gatt_chr_list[BT_MAX_GATT_CHRS];
static int gatt_chr_list_count;

static int bt_gatt_op_done_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                              void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error != NULL && error->status == BLE_HS_EDONE && gatt_disc_sem != NULL) {
        xSemaphoreGive(gatt_disc_sem);
    }
    return 0;
}

static int bt_svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_svc *service, void *arg)
{
    if (error != NULL && error->status == 0 && service != NULL) {
        disc_svc_start = service->start_handle;
        disc_svc_end = service->end_handle;
    }
    return bt_gatt_op_done_cb(conn_handle, error, arg);
}

static int bt_chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg)
{
    if (error != NULL && error->status == 0 && chr != NULL) {
        disc_found_chr = chr->val_handle;
    }
    return bt_gatt_op_done_cb(conn_handle, error, arg);
}

static uint16_t bt_uuid_to_u16(const ble_uuid_t *uuid)
{
    if (uuid == NULL) {
        return 0;
    }
    if (uuid->type == BLE_UUID_TYPE_16) {
        return ble_uuid_u16(uuid);
    }
    return 0;
}

static bool bt_is_standard_ble_svc(uint16_t uuid16)
{
    return uuid16 == 0x1800 || uuid16 == 0x1801 || uuid16 == 0x180A;
}

static int bt_all_svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *service, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error != NULL && error->status == 0 && service != NULL &&
        gatt_svc_list_count < BT_MAX_GATT_SVCS) {
        uint16_t uuid16 = bt_uuid_to_u16(&service->uuid.u);
        if (!bt_is_standard_ble_svc(uuid16)) {
            bt_gatt_svc_entry_t *entry = &gatt_svc_list[gatt_svc_list_count++];
            entry->start_handle = service->start_handle;
            entry->end_handle = service->end_handle;
            entry->uuid16 = uuid16;
        }
    }
    return bt_gatt_op_done_cb(conn_handle, error, arg);
}

static int bt_all_chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error != NULL && error->status == 0 && chr != NULL &&
        gatt_chr_list_count < BT_MAX_GATT_CHRS) {
        bt_gatt_chr_entry_t *entry = &gatt_chr_list[gatt_chr_list_count++];
        entry->val_handle = chr->val_handle;
        entry->uuid16 = bt_uuid_to_u16(&chr->uuid.u);
        entry->properties = chr->properties;
    }
    return bt_gatt_op_done_cb(conn_handle, error, arg);
}

static bool bt_gatt_wait_op(int rc, int timeout_ms)
{
    if (rc != 0) {
        return false;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() < deadline) {
        if (op_abort) {
            return false;
        }
        if (gatt_disc_sem != NULL &&
            xSemaphoreTake(gatt_disc_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            return true;
        }
    }
    return false;
}

static void bt_abort_pending_ops(void)
{
    op_abort = true;
    connect_failed = true;

    if (!stack_started) {
        return;
    }

    ble_gap_disc_cancel();
    ble_gap_conn_cancel();

    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }

    serial_ready = false;
    obd_ready = false;
    notify_enabled = false;
}

static bool bt_discover_svc_uuid(const ble_uuid128_t *uuid)
{
    disc_svc_start = 0;
    disc_svc_end = 0;
    if (gatt_disc_sem != NULL) {
        xSemaphoreTake(gatt_disc_sem, 0);
    }

    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, (const ble_uuid_t *)uuid,
                                        bt_svc_disc_cb, NULL);
    if (!bt_gatt_wait_op(rc, 5000) || disc_svc_start == 0) {
        return false;
    }
    return true;
}

static bool bt_discover_chr_uuid(const ble_uuid128_t *uuid, uint16_t *out_handle)
{
    disc_found_chr = 0;
    if (gatt_disc_sem != NULL) {
        xSemaphoreTake(gatt_disc_sem, 0);
    }

    int rc = ble_gattc_disc_chrs_by_uuid(conn_handle, disc_svc_start, disc_svc_end,
                                           (const ble_uuid_t *)uuid, bt_chr_disc_cb, NULL);
    if (!bt_gatt_wait_op(rc, 5000) || disc_found_chr == 0) {
        return false;
    }

    *out_handle = disc_found_chr;
    return true;
}

static bool bt_try_uuid_serial_pair(const ble_uuid128_t *svc_uuid,
                                    const ble_uuid128_t *tx_uuid,
                                    const ble_uuid128_t *rx_uuid,
                                    bool rx_optional,
                                    const char *label)
{
    uint16_t tx = 0;
    uint16_t rx = 0;

    if (!bt_discover_svc_uuid(svc_uuid) ||
        !bt_discover_chr_uuid(tx_uuid, &tx)) {
        return false;
    }

    if (rx_uuid != NULL && bt_discover_chr_uuid(rx_uuid, &rx) && rx != 0) {
        tx_char_handle = tx;
        rx_char_handle = rx;
        ESP_LOGI(TAG, "Serial profile: %s (TX=%u RX=%u)", label, tx, rx);
        return true;
    }

    if (rx_optional || rx_uuid == NULL) {
        tx_char_handle = tx;
        rx_char_handle = tx;
        ESP_LOGI(TAG, "Serial profile: %s (TX/RX=%u)", label, tx);
        return true;
    }

    return false;
}

static bool bt_discover_chrs_in_service(uint16_t start_handle, uint16_t end_handle)
{
    gatt_chr_list_count = 0;
    if (gatt_disc_sem != NULL) {
        xSemaphoreTake(gatt_disc_sem, 0);
    }

    int rc = ble_gattc_disc_all_chrs(conn_handle, start_handle, end_handle,
                                     bt_all_chr_disc_cb, NULL);
    return bt_gatt_wait_op(rc, 8000) && gatt_chr_list_count > 0;
}

static bool bt_pick_serial_from_chr_list(const char *label)
{
    uint16_t write_hdl = 0;
    uint16_t notify_hdl = 0;
    uint16_t indicate_hdl = 0;
    uint16_t read_hdl = 0;

    for (int i = 0; i < gatt_chr_list_count; i++) {
        const bt_gatt_chr_entry_t *chr = &gatt_chr_list[i];
        const uint8_t prop = chr->properties;

        if ((prop & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) != 0) {
            if (write_hdl == 0 || chr->uuid16 == 0xFFE1 || chr->uuid16 == 0xFFF1 ||
                chr->uuid16 == 0xABE1) {
                write_hdl = chr->val_handle;
            }
        }
        if ((prop & BLE_GATT_CHR_PROP_NOTIFY) != 0) {
            if (notify_hdl == 0 || chr->uuid16 == 0xFFE2 || chr->uuid16 == 0xFFF2) {
                notify_hdl = chr->val_handle;
            }
        }
        if ((prop & BLE_GATT_CHR_PROP_INDICATE) != 0 && indicate_hdl == 0) {
            indicate_hdl = chr->val_handle;
        }
        if ((prop & BLE_GATT_CHR_PROP_READ) != 0 && read_hdl == 0) {
            read_hdl = chr->val_handle;
        }
    }

    if (write_hdl == 0) {
        return false;
    }

    tx_char_handle = write_hdl;
    if (notify_hdl != 0 && notify_hdl != write_hdl) {
        rx_char_handle = notify_hdl;
    } else if (indicate_hdl != 0 && indicate_hdl != write_hdl) {
        rx_char_handle = indicate_hdl;
    } else if (read_hdl != 0 && read_hdl != write_hdl) {
        rx_char_handle = read_hdl;
    } else {
        rx_char_handle = write_hdl;
    }

    ESP_LOGI(TAG, "Generic serial profile: %s (TX=%u RX=%u)", label, tx_char_handle, rx_char_handle);
    return true;
}

static bool bt_discover_serial_profile_generic(void)
{
    gatt_svc_list_count = 0;
    if (gatt_disc_sem != NULL) {
        xSemaphoreTake(gatt_disc_sem, 0);
    }

    int rc = ble_gattc_disc_all_svcs(conn_handle, bt_all_svc_disc_cb, NULL);
    if (!bt_gatt_wait_op(rc, 10000) || gatt_svc_list_count == 0) {
        return false;
    }

    for (int i = 0; i < gatt_svc_list_count; i++) {
        char label[24];
        snprintf(label, sizeof(label), "svc 0x%04X", gatt_svc_list[i].uuid16);
        if (bt_discover_chrs_in_service(gatt_svc_list[i].start_handle,
                                        gatt_svc_list[i].end_handle) &&
            bt_pick_serial_from_chr_list(label)) {
            return true;
        }
    }

    return false;
}

static bool bt_discover_serial_profile(void)
{
    static const ble_uuid128_t nus_svc = BLE_UUID128_INIT(
        0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
    static const ble_uuid128_t nus_tx = BLE_UUID128_INIT(
        0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
    static const ble_uuid128_t nus_rx = BLE_UUID128_INIT(
        0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);
    static const ble_uuid128_t ffe0_svc = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe0, 0xff, 0x00, 0x00);
    static const ble_uuid128_t ffe1_chr = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe1, 0xff, 0x00, 0x00);
    static const ble_uuid128_t ffe2_chr = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe2, 0xff, 0x00, 0x00);
    static const ble_uuid128_t fff0_svc = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xf0, 0xff, 0x00, 0x00);
    static const ble_uuid128_t fff1_chr = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xf1, 0xff, 0x00, 0x00);
    static const ble_uuid128_t fff2_chr = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xf2, 0xff, 0x00, 0x00);
    static const ble_uuid128_t abe0_svc = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe0, 0xab, 0x00, 0x00);
    static const ble_uuid128_t abe1_chr = BLE_UUID128_INIT(
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xe1, 0xab, 0x00, 0x00);
    static const ble_uuid128_t issc_svc = BLE_UUID128_INIT(
        0xdf, 0x1a, 0xfc, 0x1c, 0xaf, 0xdf, 0xa9, 0x8f, 0xe5, 0x4a, 0x7d, 0xfe, 0x43, 0x53, 0x53, 0x49);
    static const ble_uuid128_t issc_tx = BLE_UUID128_INIT(
        0x41, 0x53, 0x53, 0x49, 0x7d, 0xfe, 0xe5, 0x4a, 0x8f, 0xa9, 0xdf, 0xaf, 0x1c, 0xfc, 0x1a, 0xdf);
    static const ble_uuid128_t issc_rx = BLE_UUID128_INIT(
        0x42, 0x53, 0x53, 0x49, 0x7d, 0xfe, 0xe5, 0x4a, 0x8f, 0xa9, 0xdf, 0xaf, 0x1c, 0xfc, 0x1a, 0xdf);

    tx_char_handle = 0;
    rx_char_handle = 0;

    if (bt_try_uuid_serial_pair(&nus_svc, &nus_tx, &nus_rx, false, "NUS")) {
        return true;
    }
    if (bt_try_uuid_serial_pair(&ffe0_svc, &ffe1_chr, &ffe2_chr, true, "FFE0")) {
        return true;
    }
    if (bt_try_uuid_serial_pair(&fff0_svc, &fff1_chr, &fff2_chr, true, "FFF0")) {
        return true;
    }
    if (bt_try_uuid_serial_pair(&abe0_svc, &abe1_chr, NULL, true, "ABE0")) {
        return true;
    }
    if (bt_try_uuid_serial_pair(&issc_svc, &issc_tx, &issc_rx, false, "ISSC")) {
        return true;
    }

    return bt_discover_serial_profile_generic();
}

static int bt_enable_notifications(void)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || rx_char_handle == 0) {
        return BLE_HS_EINVAL;
    }

    return ble_gattc_register_for_notification(conn_handle, rx_char_handle);
}

static void bt_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
    }
    host_synced = true;
    if (sync_sem != NULL) {
        xSemaphoreGive(sync_sem);
    }
}

static void bt_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
    host_synced = false;
    serial_ready = false;
    obd_ready = false;
    notify_enabled = false;
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

static int bt_scan_find_index(const char *addr_str)
{
    for (int i = 0; i < scan_count; i++) {
        if (strcmp(scan_buf[i].addr, addr_str) == 0) {
            return i;
        }
    }
    return -1;
}

static void bt_scan_apply_name(bt_device_info_t *entry, const struct ble_hs_adv_fields *fields)
{
    if (fields == NULL || fields->name == NULL || fields->name_len == 0) {
        return;
    }

    size_t copy = fields->name_len;
    if (copy >= sizeof(entry->name)) {
        copy = sizeof(entry->name) - 1;
    }
    memcpy(entry->name, fields->name, copy);
    entry->name[copy] = '\0';
}

static void bt_scan_upsert_device(const struct ble_gap_disc_desc *d)
{
    char addr_str[BT_ADDR_STR_LEN];
    bt_addr_to_str(&d->addr, addr_str, sizeof(addr_str));

    int idx = bt_scan_find_index(addr_str);
    if (idx < 0) {
        if (scan_count >= BT_SCAN_MAX_RESULTS) {
            return;
        }
        idx = scan_count++;
        memset(&scan_buf[idx], 0, sizeof(scan_buf[idx]));
        strncpy(scan_buf[idx].addr, addr_str, sizeof(scan_buf[idx].addr) - 1);
        scan_buf[idx].addr_type = d->addr.type;
        snprintf(scan_buf[idx].name, sizeof(scan_buf[idx].name), "BLE %s", addr_str);
    }

    if (d->rssi > scan_buf[idx].rssi) {
        scan_buf[idx].rssi = d->rssi;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    if (ble_hs_adv_parse_fields(&fields, d->data, d->length_data) == 0) {
        bt_scan_apply_name(&scan_buf[idx], &fields);
    }

    scan_buf[idx].is_obd_hint = bt_name_looks_like_elm327(scan_buf[idx].name);
}

static int bt_scan_cmp_devices(const void *a, const void *b)
{
    const bt_device_info_t *da = (const bt_device_info_t *)a;
    const bt_device_info_t *db = (const bt_device_info_t *)b;

    if (da->is_obd_hint != db->is_obd_hint) {
        return db->is_obd_hint - da->is_obd_hint;
    }
    return (int)db->rssi - (int)da->rssi;
}

static void bt_scan_sort_results(void)
{
    if (scan_count > 1) {
        qsort(scan_buf, (size_t)scan_count, sizeof(scan_buf[0]), bt_scan_cmp_devices);
    }
}

static int bt_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        bt_scan_upsert_device(&event->disc);
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan_done = true;
        if (scan_complete_sem != NULL) {
            xSemaphoreGive(scan_complete_sem);
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE connected, handle=%u", conn_handle);
        } else {
            ESP_LOGW(TAG, "BLE connect failed: %d", event->connect.status);
            connect_failed = true;
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            if (connect_sem != NULL) {
                xSemaphoreGive(connect_sem);
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        serial_ready = false;
        obd_ready = false;
        notify_enabled = false;
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.om != NULL && OS_MBUF_PKTLEN(event->notify_rx.om) > 0) {
            bt_append_rx(event->notify_rx.om->om_data,
                         OS_MBUF_PKTLEN(event->notify_rx.om));
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.cur_notify || event->subscribe.cur_indicate) {
            notify_enabled = true;
            serial_ready = true;
            ESP_LOGI(TAG, "BLE notify subscribed");
            if (connect_sem != NULL) {
                xSemaphoreGive(connect_sem);
            }
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %u", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void bt_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool bt_wait_sync(int timeout_ms)
{
    if (host_synced) {
        return true;
    }
    if (sync_sem == NULL) {
        return false;
    }
    /* Do not drain the semaphore — sync may already have fired before we wait */
    return xSemaphoreTake(sync_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE || host_synced;
}

static bool bt_ensure_stack(void)
{
    if (stack_started) {
        return true;
    }

    if (!port_inited) {
        rx_mutex = xSemaphoreCreateMutex();
        sync_sem = xSemaphoreCreateBinary();
        connect_sem = xSemaphoreCreateBinary();
        scan_complete_sem = xSemaphoreCreateBinary();
        if (!rx_mutex || !sync_sem || !connect_sem || !scan_complete_sem) {
            return false;
        }

        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        esp_err_t err = nimble_port_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
            return false;
        }

        ble_hs_cfg.reset_cb = bt_on_reset;
        ble_hs_cfg.sync_cb = bt_on_sync;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_store_config_init();

        nimble_port_freertos_init(bt_host_task);
        port_inited = true;
    }

    if (!bt_wait_sync(12000)) {
        ESP_LOGE(TAG, "NimBLE host sync timeout");
        return false;
    }

    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    stack_started = true;
    ESP_LOGI(TAG, "NimBLE stack ready");
    return true;
}

static int bt_run_scan(int duration_ms)
{
    if (!stack_started) {
        return BLE_HS_ENOTSUP;
    }

    bt_abort_pending_ops();
    vTaskDelay(pdMS_TO_TICKS(150));
    op_abort = false;
    connect_failed = false;

    scan_count = 0;
    scan_done = false;
    memset(scan_buf, 0, sizeof(scan_buf));

    if (scan_complete_sem != NULL) {
        xSemaphoreTake(scan_complete_sem, 0);
    }

    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.passive = 1;
    params.itvl = 0x0050;
    params.window = 0x0030;
    params.filter_duplicates = 0;

    int rc = ble_gap_disc(own_addr_type, duration_ms, &params, bt_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return rc;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms + 3000);
    while (!scan_done && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!scan_done) {
        ble_gap_disc_cancel();
        if (scan_complete_sem != NULL) {
            xSemaphoreTake(scan_complete_sem, pdMS_TO_TICKS(500));
        }
    }

    if (scan_done) {
        bt_scan_sort_results();
        ESP_LOGI(TAG, "BLE scan complete: %d device(s)", scan_count);
        return 0;
    }
    return BLE_HS_ETIMEOUT;
}

static void bt_cmd_worker(void *arg)
{
    (void)arg;

    bt_cmd_msg_t msg;
    for (;;) {
        if (xQueueReceive(bt_cmd_q, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!bt_ensure_stack()) {
            ESP_LOGE(TAG, "BT stack not ready");
            msg.ok = false;
            if (msg.done != NULL) {
                xSemaphoreGive(msg.done);
            }
            continue;
        }

        switch (msg.id) {
        case BT_CMD_SCAN:
            msg.ok = (bt_run_scan(msg.scan_duration_ms > 0 ? msg.scan_duration_ms
                                                           : BT_SCAN_DEFAULT_MS) == 0);
            break;
        case BT_CMD_CONNECT:
            msg.ok = bt_connect_addr_internal(&msg.addr,
                                              msg.name[0] != '\0' ? msg.name : NULL);
            break;
        case BT_CMD_DISCONNECT:
            bt_disconnect_internal();
            msg.ok = true;
            break;
        case BT_CMD_AUTO_CONNECT:
            msg.ok = bt_connect_auto_internal();
            break;
        default:
            msg.ok = false;
            break;
        }

        if (msg.done != NULL) {
            xSemaphoreGive(msg.done);
        }
    }
}

static void bt_start_cmd_worker(void)
{
    if (cmd_worker_started) {
        return;
    }

    bt_cmd_q = xQueueCreate(BT_CMD_QUEUE_LEN, sizeof(bt_cmd_msg_t));
    if (bt_cmd_q == NULL) {
        ESP_LOGE(TAG, "BT cmd queue create failed");
        return;
    }

    /* Internal RAM stack required for RF/cache freeze during BLE ops */
    if (xTaskCreatePinnedToCore(bt_cmd_worker, "bt_cmd", BT_CMD_TASK_STACK,
                                NULL, 6, &bt_cmd_task_h, 0) != pdPASS) {
        ESP_LOGE(TAG, "BT cmd worker create failed");
        return;
    }

    cmd_worker_started = true;
}

static bool bt_submit_cmd(bt_cmd_msg_t *msg, TickType_t wait_ticks)
{
    bt_start_cmd_worker();
    if (!cmd_worker_started || bt_cmd_q == NULL) {
        return false;
    }

    msg->done = xSemaphoreCreateBinary();
    if (msg->done == NULL) {
        return false;
    }

    if (xQueueSend(bt_cmd_q, msg, pdMS_TO_TICKS(2000)) != pdTRUE) {
        vSemaphoreDelete(msg->done);
        msg->done = NULL;
        return false;
    }

    const bool ok = (xSemaphoreTake(msg->done, wait_ticks) == pdTRUE) && msg->ok;
    vSemaphoreDelete(msg->done);
    msg->done = NULL;
    return ok;
}

static bool bt_finish_gatt_setup(void)
{
    if (gatt_disc_sem == NULL) {
        gatt_disc_sem = xSemaphoreCreateBinary();
    }

    ble_gattc_exchange_mtu(conn_handle, NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!bt_discover_serial_profile()) {
        last_fail = BT_FAIL_GATT;
        ESP_LOGW(TAG, "Serial GATT profile not found");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    if (bt_enable_notifications() != 0) {
        /* Some adapters expose write-only FFE1 — still usable for polling */
        serial_ready = true;
        notify_enabled = false;
        if (connect_sem != NULL) {
            xSemaphoreGive(connect_sem);
        }
        return true;
    }

    if (connect_sem != NULL &&
        xSemaphoreTake(connect_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
        return serial_ready;
    }

    serial_ready = true;
    return true;
}

static bool bt_connect_addr_internal(const ble_addr_t *addr, const char *display_name)
{
    if (!bt_ensure_stack() || addr == NULL) {
        return false;
    }

    bt_abort_pending_ops();
    vTaskDelay(pdMS_TO_TICKS(150));
    op_abort = false;
    connect_failed = false;
    serial_ready = false;
    obd_ready = false;
    notify_enabled = false;
    bt_flush_rx();

    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    struct ble_gap_conn_params params = {
        .scan_itvl = 0x0010,
        .scan_window = 0x0010,
        .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
        .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
        .latency = 0,
        .supervision_timeout = 400,
        .min_ce_len = 0,
        .max_ce_len = 0,
    };

    int rc = ble_gap_connect(own_addr_type, addr, 30000, &params, bt_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed: %d", rc);
        last_fail = BT_FAIL_CONNECT;
        return false;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BT_CONNECT_TIMEOUT_MS);
    while (conn_handle == BLE_HS_CONN_HANDLE_NONE && !connect_failed && !op_abort &&
           xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (connect_failed || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        last_fail = BT_FAIL_CONNECT;
        return false;
    }

    if (!bt_finish_gatt_setup()) {
        bt_disconnect_internal();
        return false;
    }

    char addr_str[BT_ADDR_STR_LEN];
    bt_addr_to_str(addr, addr_str, sizeof(addr_str));
    bt_save_device(display_name != NULL ? display_name : addr_str, addr_str, addr->type);
    last_fail = BT_FAIL_NONE;
    return true;
}

bool bt_init_stack(void)
{
    bt_start_cmd_worker();
    return cmd_worker_started;
}

void bt_prepare_for_operation(void)
{
    bt_abort_pending_ops();
}

void bt_shutdown_stack(void)
{
    bt_cmd_msg_t msg = {
        .id = BT_CMD_DISCONNECT,
    };
    bt_submit_cmd(&msg, pdMS_TO_TICKS(3000));
}

static bool bt_queue_connect(const ble_addr_t *addr, const char *display_name)
{
    if (addr == NULL) {
        return false;
    }

    bt_cmd_msg_t msg = {
        .id = BT_CMD_CONNECT,
        .addr = *addr,
    };
    if (display_name != NULL && display_name[0] != '\0') {
        strncpy(msg.name, display_name, sizeof(msg.name) - 1);
    }

    return bt_submit_cmd(&msg, pdMS_TO_TICKS(BT_CONNECT_TIMEOUT_MS + 10000));
}

bool bt_link_up(void)
{
    if (g_settings.bt_manual_mode && g_settings.bt_device_addr[0] == '\0') {
        ESP_LOGI(TAG, "Manual BT: no saved adapter");
        return false;
    }

    if (g_settings.bt_device_addr[0] != '\0') {
        ble_addr_t addr;
        if (bt_str_to_addr(g_settings.bt_device_addr, &addr)) {
            if (bt_queue_connect(&addr, g_settings.bt_device_name)) {
                return true;
            }
            ESP_LOGW(TAG, "Saved adapter connect failed, falling back to scan");
        }
    }

    return bt_connect_auto();
}

bool bt_connect_to_addr(const char *addr_str)
{
    ble_addr_t addr;
    if (!bt_str_to_addr(addr_str, &addr)) {
        return false;
    }
    return bt_queue_connect(&addr, NULL);
}

bool bt_connect_auto(void)
{
    bt_cmd_msg_t msg = {
        .id = BT_CMD_AUTO_CONNECT,
    };
    return bt_submit_cmd(&msg, pdMS_TO_TICKS(BT_SCAN_DEFAULT_MS + BT_CONNECT_TIMEOUT_MS + 15000));
}

static bool bt_connect_auto_internal(void)
{
    if (!stack_started) {
        return false;
    }

    if (g_settings.bt_device_addr[0] != '\0') {
        ble_addr_t addr;
        if (bt_str_to_addr(g_settings.bt_device_addr, &addr)) {
            if (bt_connect_addr_internal(&addr, g_settings.bt_device_name)) {
                return true;
            }
        }
    }

    if (bt_run_scan(BT_SCAN_DEFAULT_MS) != 0) {
        last_fail = BT_FAIL_SCAN;
        return false;
    }

    if (scan_count == 0) {
        last_fail = BT_FAIL_SCAN;
        return false;
    }

    bt_scan_sort_results();

    int attempts = scan_count;
    if (attempts > BT_AUTO_CONNECT_MAX_ATTEMPTS) {
        attempts = BT_AUTO_CONNECT_MAX_ATTEMPTS;
    }

    for (int i = 0; i < attempts; i++) {
        ble_addr_t addr;
        if (!bt_str_to_addr(scan_buf[i].addr, &addr)) {
            continue;
        }
        g_settings.bt_addr_type = scan_buf[i].addr_type;
        ESP_LOGI(TAG, "Auto-connect [%d/%d]: %s (%s)%s",
                 i + 1, attempts, scan_buf[i].name, scan_buf[i].addr,
                 scan_buf[i].is_obd_hint ? " [OBD]" : "");
        if (bt_connect_addr_internal(&addr, scan_buf[i].name)) {
            g_settings.bt_manual_mode = false;
            settings_save(&g_settings);
            return true;
        }
    }

    last_fail = BT_FAIL_SCAN;
    return false;
}

int bt_scan_devices(bt_device_info_t *list, int max_count, int duration_ms)
{
    if (list == NULL || max_count <= 0) {
        return 0;
    }

    if (duration_ms <= 0) {
        duration_ms = BT_SCAN_DEFAULT_MS;
    }

    bt_cmd_msg_t msg = {
        .id = BT_CMD_SCAN,
        .scan_duration_ms = duration_ms,
    };

    if (!bt_submit_cmd(&msg, pdMS_TO_TICKS(duration_ms + 35000))) {
        last_fail = BT_FAIL_SCAN;
        return 0;
    }

    int copy = scan_count;
    if (copy > max_count) {
        copy = max_count;
    }
    memcpy(list, scan_buf, (size_t)copy * sizeof(bt_device_info_t));
    return copy;
}

void bt_forget_saved(void)
{
    g_settings.bt_device_name[0] = '\0';
    g_settings.bt_device_addr[0] = '\0';
    g_settings.bt_manual_mode = true;
    settings_save(&g_settings);
    bt_disconnect();
}

static void bt_disconnect_internal(void)
{
    obd_ready = false;
    serial_ready = false;
    notify_enabled = false;
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    bt_flush_rx();
}

void bt_disconnect(void)
{
    if (bt_cmd_task_h != NULL && xTaskGetCurrentTaskHandle() == bt_cmd_task_h) {
        bt_disconnect_internal();
        return;
    }

    bt_cmd_msg_t msg = {
        .id = BT_CMD_DISCONNECT,
    };
    bt_submit_cmd(&msg, pdMS_TO_TICKS(3000));
}

bool bt_is_connected(void)
{
    return conn_handle != BLE_HS_CONN_HANDLE_NONE && serial_ready && obd_ready;
}

bool bt_serial_ready(void)
{
    return conn_handle != BLE_HS_CONN_HANDLE_NONE && serial_ready;
}

bool bt_obd_session_ready(void)
{
    if (!serial_ready) {
        return false;
    }

    bt_flush_rx();

    if (!elm327_probe_adapter(bt_elm_send, bt_elm_recv)) {
        ESP_LOGW(TAG, "BT ELM327 probe failed");
        last_fail = BT_FAIL_ELM;
        return false;
    }

    if (!elm327_init_session(bt_elm_send, bt_elm_recv)) {
        last_fail = BT_FAIL_ELM;
        return false;
    }

    obd_ready = elm327_probe_obd_ready(bt_elm_send, bt_elm_recv);
    if (obd_ready) {
        ESP_LOGI(TAG, "BT OBD session ready");
        last_fail = BT_FAIL_NONE;
    } else {
        last_fail = BT_FAIL_OBD;
    }
    return obd_ready;
}

esp_err_t bt_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (resp_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*resp_len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (cmd == NULL || resp == NULL || len < 1) {
        *resp_len = 0;
        return ESP_ERR_INVALID_ARG;
    }

    if (!serial_ready || !obd_ready) {
        *resp_len = 0;
        return ESP_ERR_INVALID_STATE;
    }

    char cmd_str[64];
    int cmd_len;
    if (len == 1) {
        cmd_len = snprintf(cmd_str, sizeof(cmd_str), "%02X\r", cmd[0]);
    } else {
        cmd_len = snprintf(cmd_str, sizeof(cmd_str), "%02X%02X\r", cmd[0], cmd[1]);
    }

    bt_flush_rx();
    if (bt_elm_send(cmd_str, (size_t)cmd_len) < 0) {
        *resp_len = 0;
        return ESP_FAIL;
    }

    int total = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        int n = bt_elm_recv((char *)resp + total, *resp_len - (size_t)total - 1, 400);
        if (n > 0) {
            total += n;
            resp[total] = '\0';
            if (resp[total - 1] == '>' || (len >= 2 && strstr((char *)resp, "41") != NULL)) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    if (total == 0) {
        *resp_len = 0;
        return ESP_ERR_TIMEOUT;
    }

    resp[total] = '\0';
    *resp_len = (size_t)total;
    return ESP_OK;
}

bool bt_connect(void)
{
    return bt_link_up();
}

void bt_save_device(const char *name, const char *addr_str, uint8_t addr_type)
{
    if (addr_str != NULL) {
        strncpy(g_settings.bt_device_addr, addr_str, sizeof(g_settings.bt_device_addr) - 1);
        g_settings.bt_device_addr[sizeof(g_settings.bt_device_addr) - 1] = '\0';
    }
    if (name != NULL && name[0] != '\0') {
        strncpy(g_settings.bt_device_name, name, sizeof(g_settings.bt_device_name) - 1);
        g_settings.bt_device_name[sizeof(g_settings.bt_device_name) - 1] = '\0';
    } else if (addr_str != NULL) {
        strncpy(g_settings.bt_device_name, addr_str, sizeof(g_settings.bt_device_name) - 1);
        g_settings.bt_device_name[sizeof(g_settings.bt_device_name) - 1] = '\0';
    }
    g_settings.bt_addr_type = addr_type;
    settings_save(&g_settings);
}

bt_fail_stage_t bt_get_last_fail_stage(void)
{
    return last_fail;
}

const char *bt_get_last_fail_hint(void)
{
    switch (last_fail) {
        case BT_FAIL_SCAN:
            return "Adapter not found - enable OBDII pairing";
        case BT_FAIL_CONNECT:
            return "Bluetooth connection failed";
        case BT_FAIL_GATT:
            return "Serial profile not found (BLE adapter required)";
        case BT_FAIL_ELM:
            return "ELM327 not responding";
        case BT_FAIL_OBD:
            return "ELM327 ready - turn ignition ON";
        default:
            return "Connection failed";
    }
}

#elif CONFIG_BT_CLASSIC_ENABLED && CONFIG_BT_SPP_ENABLED

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"

#define SPP_RX_BUF_SIZE 512
#define BT_SCAN_MS 5000

static bool bt_initialized;
static bool spp_connected;
static uint32_t spp_handle;
static uint8_t spp_rx_buf[SPP_RX_BUF_SIZE];
static size_t spp_rx_len;
static SemaphoreHandle_t spp_rx_mutex;
static SemaphoreHandle_t spp_connect_sem;

static bt_device_info_t classic_scan_buf[BT_SCAN_MAX_RESULTS];
static int classic_scan_count;
static bool classic_scan_done;

static void bt_classic_addr_to_str(const esp_bd_addr_t addr, char *out, size_t out_len)
{
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static bool bt_classic_str_to_addr(const char *str, esp_bd_addr_t out)
{
    unsigned int b[6];
    if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)b[i];
    }
    return true;
}

static int bt_classic_elm_send(const char *data, size_t len)
{
    if (!spp_connected || data == NULL || len == 0) {
        return -1;
    }
    if (esp_spp_write(spp_handle, (int)len, (uint8_t *)data) == (int32_t)len) {
        return (int)len;
    }
    return -1;
}

static int bt_classic_elm_recv(char *buf, size_t max_len, int timeout_ms)
{
    if (buf == NULL || max_len == 0 || spp_rx_mutex == NULL) {
        return 0;
    }
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    size_t copied = 0;
    while (xTaskGetTickCount() < deadline) {
        if (xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (spp_rx_len > 0) {
                copied = spp_rx_len;
                if (copied >= max_len) {
                    copied = max_len - 1;
                }
                memcpy(buf, spp_rx_buf, copied);
                buf[copied] = '\0';
                spp_rx_len = 0;
            }
            xSemaphoreGive(spp_rx_mutex);
        }
        if (copied > 0) {
            return (int)copied;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return 0;
}

static void bt_classic_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_DISC_RES_EVT && classic_scan_count < BT_SCAN_MAX_RESULTS) {
        bt_device_info_t *entry = &classic_scan_buf[classic_scan_count];
        memset(entry, 0, sizeof(*entry));
        bt_classic_addr_to_str(param->disc_res.bda, entry->addr, sizeof(entry->addr));
        entry->rssi = param->disc_res.rssi;
        if (param->disc_res.prop & ESP_BT_GAP_DEV_PROP_BDNAME) {
            strncpy(entry->name, (const char *)param->disc_res.bdname, sizeof(entry->name) - 1);
        } else {
            snprintf(entry->name, sizeof(entry->name), "Device %d", classic_scan_count + 1);
        }
        entry->is_obd_hint = bt_name_looks_like_elm327(entry->name);
        classic_scan_count++;
    } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT &&
               param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        classic_scan_done = true;
    }
}

static void bt_classic_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_OPEN_EVT:
        if (param->open.status == ESP_SPP_SUCCESS) {
            spp_connected = true;
            spp_handle = param->open.handle;
            serial_ready = true;
            ESP_LOGI(TAG, "Classic SPP connected");
        } else {
            connect_failed = true;
        }
        if (spp_connect_sem) {
            xSemaphoreGive(spp_connect_sem);
        }
        break;
    case ESP_SPP_CLOSE_EVT:
        spp_connected = false;
        serial_ready = false;
        obd_ready = false;
        spp_handle = 0;
        break;
    case ESP_SPP_DATA_IND_EVT:
        if (param->data_ind.len > 0 && spp_rx_mutex &&
            xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            size_t to_copy = param->data_ind.len;
            if (to_copy > SPP_RX_BUF_SIZE) {
                to_copy = SPP_RX_BUF_SIZE;
            }
            memcpy(spp_rx_buf, param->data_ind.data, to_copy);
            spp_rx_len = to_copy;
            xSemaphoreGive(spp_rx_mutex);
        }
        break;
    default:
        break;
    }
}

static bool bt_classic_init(void)
{
    if (bt_initialized) {
        return true;
    }
    spp_rx_mutex = xSemaphoreCreateMutex();
    spp_connect_sem = xSemaphoreCreateBinary();
    if (!spp_rx_mutex || !spp_connect_sem) {
        return false;
    }

    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&cfg) != ESP_OK ||
        esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK ||
        esp_bluedroid_init() != ESP_OK ||
        esp_bluedroid_enable() != ESP_OK) {
        return false;
    }

    esp_bt_gap_register_callback(bt_classic_gap_cb);
    esp_spp_register_callback(bt_classic_spp_cb);
    esp_spp_init(ESP_SPP_MODE_CB);
    bt_initialized = true;
    return true;
}

static bool bt_classic_connect_addr(const esp_bd_addr_t addr, const char *name)
{
    if (!bt_classic_init()) {
        return false;
    }
    connect_failed = false;
    serial_ready = false;
    obd_ready = false;
    spp_connected = false;

    xSemaphoreTake(spp_connect_sem, 0);
    esp_spp_connect(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER, 0, addr);
    if (xSemaphoreTake(spp_connect_sem, pdMS_TO_TICKS(BT_CONNECT_TIMEOUT_MS)) != pdTRUE ||
        !spp_connected) {
        last_fail = BT_FAIL_CONNECT;
        return false;
    }

    char addr_str[BT_ADDR_STR_LEN];
    bt_classic_addr_to_str(addr, addr_str, sizeof(addr_str));
    bt_save_device(name != NULL ? name : addr_str, addr_str, 0);
    last_fail = BT_FAIL_NONE;
    return true;
}

bool bt_init_stack(void) { return bt_classic_init(); }
void bt_shutdown_stack(void) { bt_disconnect(); }
void bt_prepare_for_operation(void) {}

bool bt_link_up(void)
{
    if (g_settings.bt_manual_mode && g_settings.bt_device_addr[0] == '\0') {
        return false;
    }
    if (g_settings.bt_device_addr[0] != '\0') {
        esp_bd_addr_t addr;
        if (bt_classic_str_to_addr(g_settings.bt_device_addr, addr)) {
            return bt_classic_connect_addr(addr, g_settings.bt_device_name);
        }
    }
    return bt_connect_auto();
}

bool bt_connect_to_addr(const char *addr_str)
{
    esp_bd_addr_t addr;
    if (!bt_classic_str_to_addr(addr_str, addr)) {
        return false;
    }
    return bt_classic_connect_addr(addr, NULL);
}

bool bt_connect_auto(void)
{
    if (g_settings.bt_device_addr[0] != '\0') {
        esp_bd_addr_t addr;
        if (bt_classic_str_to_addr(g_settings.bt_device_addr, addr) &&
            bt_classic_connect_addr(addr, g_settings.bt_device_name)) {
            return true;
        }
    }

    classic_scan_count = 0;
    classic_scan_done = false;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, BT_SCAN_MS / 1000, 0);
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BT_SCAN_MS + 2000);
    while (!classic_scan_done && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    esp_bt_gap_cancel_discovery();

    for (int i = 0; i < classic_scan_count; i++) {
        if (!classic_scan_buf[i].is_obd_hint) {
            continue;
        }
        esp_bd_addr_t addr;
        if (!bt_classic_str_to_addr(classic_scan_buf[i].addr, addr)) {
            continue;
        }
        if (bt_classic_connect_addr(addr, classic_scan_buf[i].name)) {
            g_settings.bt_manual_mode = false;
            app_settings_save();
            return true;
        }
    }
    last_fail = BT_FAIL_SCAN;
    return false;
}

int bt_scan_devices(bt_device_info_t *list, int max_count, int duration_ms)
{
    if (list == NULL || max_count <= 0 || !bt_classic_init()) {
        return 0;
    }
    classic_scan_count = 0;
    classic_scan_done = false;
    int sec = duration_ms > 0 ? duration_ms / 1000 : BT_SCAN_MS / 1000;
    if (sec < 3) {
        sec = 3;
    }
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, sec, 0);
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(sec * 1000 + 2000);
    while (!classic_scan_done && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    esp_bt_gap_cancel_discovery();
    int copy = classic_scan_count;
    if (copy > max_count) {
        copy = max_count;
    }
    memcpy(list, classic_scan_buf, (size_t)copy * sizeof(bt_device_info_t));
    return copy;
}

void bt_forget_saved(void)
{
    g_settings.bt_device_name[0] = '\0';
    g_settings.bt_device_addr[0] = '\0';
    g_settings.bt_manual_mode = true;
    app_settings_save();
    bt_disconnect();
}

void bt_disconnect(void)
{
    if (spp_connected) {
        esp_spp_disconnect(spp_handle);
    }
    spp_connected = false;
    serial_ready = false;
    obd_ready = false;
    spp_handle = 0;
    spp_rx_len = 0;
}

bool bt_is_connected(void) { return spp_connected && serial_ready && obd_ready; }
bool bt_serial_ready(void) { return spp_connected && serial_ready; }

bool bt_obd_session_ready(void)
{
    if (!serial_ready) {
        return false;
    }
    if (!elm327_probe_adapter(bt_classic_elm_send, bt_classic_elm_recv)) {
        last_fail = BT_FAIL_ELM;
        return false;
    }
    if (!elm327_init_session(bt_classic_elm_send, bt_classic_elm_recv)) {
        last_fail = BT_FAIL_ELM;
        return false;
    }
    obd_ready = elm327_probe_obd_ready(bt_classic_elm_send, bt_classic_elm_recv);
    last_fail = obd_ready ? BT_FAIL_NONE : BT_FAIL_OBD;
    return obd_ready;
}

esp_err_t bt_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (!serial_ready || !obd_ready || resp_len == NULL || *resp_len == 0) {
        if (resp_len) {
            *resp_len = 0;
        }
        return ESP_ERR_INVALID_STATE;
    }
    char cmd_str[64];
    int cmd_len = (len == 1) ? snprintf(cmd_str, sizeof(cmd_str), "%02X\r", cmd[0])
                             : snprintf(cmd_str, sizeof(cmd_str), "%02X%02X\r", cmd[0], cmd[1]);
    if (bt_classic_elm_send(cmd_str, (size_t)cmd_len) < 0) {
        *resp_len = 0;
        return ESP_FAIL;
    }
    int total = bt_classic_elm_recv((char *)resp, *resp_len - 1, 1200);
    if (total <= 0) {
        *resp_len = 0;
        return ESP_ERR_TIMEOUT;
    }
    resp[total] = '\0';
    *resp_len = (size_t)total;
    return ESP_OK;
}

bool bt_connect(void) { return bt_link_up(); }

void bt_save_device(const char *name, const char *addr_str, uint8_t addr_type)
{
    if (addr_str != NULL) {
        strncpy(g_settings.bt_device_addr, addr_str, sizeof(g_settings.bt_device_addr) - 1);
        g_settings.bt_device_addr[sizeof(g_settings.bt_device_addr) - 1] = '\0';
    }
    if (name != NULL && name[0] != '\0') {
        strncpy(g_settings.bt_device_name, name, sizeof(g_settings.bt_device_name) - 1);
        g_settings.bt_device_name[sizeof(g_settings.bt_device_name) - 1] = '\0';
    } else if (addr_str != NULL) {
        strncpy(g_settings.bt_device_name, addr_str, sizeof(g_settings.bt_device_name) - 1);
        g_settings.bt_device_name[sizeof(g_settings.bt_device_name) - 1] = '\0';
    }
    g_settings.bt_addr_type = addr_type;
    settings_save(&g_settings);
}

bt_fail_stage_t bt_get_last_fail_stage(void) { return last_fail; }
const char *bt_get_last_fail_hint(void)
{
    switch (last_fail) {
        case BT_FAIL_SCAN: return "OBDII adapter not found";
        case BT_FAIL_CONNECT: return "SPP connection failed";
        case BT_FAIL_OBD: return "ELM327 ready - turn ignition ON";
        default: return "Connection failed";
    }
}

#else

bool bt_init_stack(void) { return false; }
void bt_shutdown_stack(void) {}
void bt_prepare_for_operation(void) {}
bool bt_link_up(void) { return false; }
bool bt_obd_session_ready(void) { return false; }
bool bt_connect_to_addr(const char *addr_str) { (void)addr_str; return false; }
bool bt_connect_auto(void) { return false; }
int bt_scan_devices(bt_device_info_t *list, int max_count, int duration_ms)
{
    (void)list; (void)max_count; (void)duration_ms;
    return 0;
}
void bt_forget_saved(void) {}
void bt_disconnect(void) {}
bool bt_is_connected(void) { return false; }
bool bt_serial_ready(void) { return false; }
esp_err_t bt_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    (void)cmd; (void)len; (void)resp; (void)resp_len;
    return ESP_ERR_NOT_SUPPORTED;
}
bool bt_connect(void) { return false; }
void bt_save_device(const char *name, const char *addr_str, uint8_t addr_type)
{
    (void)name;
    (void)addr_str;
    (void)addr_type;
}
bt_fail_stage_t bt_get_last_fail_stage(void) { return BT_FAIL_NONE; }
const char *bt_get_last_fail_hint(void) { return "Bluetooth not supported"; }

#endif
