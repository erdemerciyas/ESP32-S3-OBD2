#include "connectivity.h"

#include "wifi_manager.h"
#include "bt_manager.h"
#include "usb_manager.h"
#include "app.h"

#include "esp_err.h"
#include "esp_log.h"
#include "settings.h"
#include "conn_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"

#include <string.h>

static const char *TAG = "connectivity";

static connection_type_t current_type = CONN_TYPE_NONE;
static connectivity_state_t conn_state = CONN_STATE_DISCONNECTED;
static char status_text[64] = "Not connected";
static uint8_t reconnect_attempts;
static SemaphoreHandle_t connectivity_mutex;
static SemaphoreHandle_t bt_op_mutex;
static volatile bool connectivity_user_busy;
static volatile bool connectivity_auto_active;

static bool connectivity_try_lock(void)
{
    if (connectivity_mutex == NULL) {
        connectivity_mutex = xSemaphoreCreateMutex();
    }
    return xSemaphoreTake(connectivity_mutex, 0) == pdTRUE;
}

static void connectivity_set_user_busy(bool busy)
{
    connectivity_user_busy = busy;
}

static bool connectivity_begin_bt_op(TickType_t wait_ticks)
{
    if (bt_op_mutex == NULL) {
        bt_op_mutex = xSemaphoreCreateMutex();
    }
    return xSemaphoreTake(bt_op_mutex, wait_ticks) == pdTRUE;
}

static void connectivity_end_bt_op(void)
{
    if (bt_op_mutex != NULL) {
        xSemaphoreGive(bt_op_mutex);
    }
    connectivity_auto_active = false;
}

void connectivity_bt_ui_begin(void)
{
    connectivity_set_user_busy(true);
    connectivity_auto_active = false;
    /* Must not block LVGL — full cancel runs on bt_ui_job worker. */
    bt_signal_cancel();
}

void connectivity_bt_ui_end(void)
{
    connectivity_set_user_busy(false);
    connectivity_auto_active = false;
}

extern app_settings_t g_settings;

__attribute__((weak)) void obd_service_on_disconnect(void) {}

__attribute__((weak)) esp_err_t obd_service_discover_supported_pids(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void bt_on_serial_ready(void);
void bt_on_serial_lost(void);

static void connectivity_set_state(connectivity_state_t state, const char *text)
{
    conn_state = state;
    if (text != NULL) {
        strncpy(status_text, text, sizeof(status_text) - 1);
        status_text[sizeof(status_text) - 1] = '\0';
    }
}

static void connectivity_lock(void)
{
    if (connectivity_mutex == NULL) {
        connectivity_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(connectivity_mutex, portMAX_DELAY);
}

static void connectivity_unlock(void)
{
    xSemaphoreGive(connectivity_mutex);
}

static bool connectivity_ensure_bt_worker(void)
{
    if (!bt_init_stack()) {
        ESP_LOGE(TAG, "BT worker failed to start");
        connectivity_set_state(CONN_STATE_ERROR, "BT worker failed");
        return false;
    }

    if (!bt_stack_is_ready()) {
        ESP_LOGI(TAG, "Warming up BLE stack before operation...");
        if (!bt_warmup_stack()) {
            const char *err = bt_get_last_error();
            ESP_LOGE(TAG, "BLE warmup failed: %s", err != NULL ? err : "unknown");
            connectivity_set_state(CONN_STATE_ERROR, "BLE stack not ready");
            conn_log_add("BLE warmup failed: %s", err != NULL ? err : "unknown");
            return false;
        }
    }

    return true;
}

static bool transport_link_up(connection_type_t type)
{
    switch (type) {
        case CONN_TYPE_WIFI:
            return wifi_link_up();
        case CONN_TYPE_USB:
            return usb_link_up();
        case CONN_TYPE_BLUETOOTH:
            if (!connectivity_ensure_bt_worker()) {
                return false;
            }
            if (connectivity_has_saved_bt_profile()) {
                return bt_connect_saved_profile();
            }
            return bt_link_up();
        default:
            return false;
    }
}

static bool transport_obd_ready(connection_type_t type)
{
    switch (type) {
        case CONN_TYPE_WIFI:
            return wifi_obd_session_ready();
        case CONN_TYPE_USB:
            return usb_obd_session_ready();
        case CONN_TYPE_BLUETOOTH:
            return bt_obd_session_ready();
        default:
            return false;
    }
}

static esp_err_t connectivity_bring_up(connection_type_t type)
{
    if (type == CONN_TYPE_BLUETOOTH && connectivity_has_saved_bt_profile()) {
        connectivity_set_state(CONN_STATE_ELM_INIT, "Auto-connecting...");
    } else {
        connectivity_set_state(CONN_STATE_ELM_INIT, "Connecting...");
    }

    if (!transport_link_up(type)) {
        connectivity_set_state(CONN_STATE_ERROR, "Connection failed");
        return ESP_FAIL;
    }

    current_type = type;
    connectivity_set_state(CONN_STATE_LINK_UP, "ELM327 init...");

    if (type == CONN_TYPE_BLUETOOTH) {
        if (bt_serial_ready()) {
            connectivity_set_state(CONN_STATE_LINK_UP, "BT linked");
            reconnect_attempts = 0;
            return ESP_OK;
        }
        connectivity_stop();
        connectivity_set_state(CONN_STATE_ERROR, "BT serial not ready");
        return ESP_FAIL;
    }

    if (!transport_obd_ready(type)) {
        if (type == CONN_TYPE_WIFI && wifi_tcp_ready()) {
            connectivity_set_state(CONN_STATE_LINK_UP, "TCP ready - ignition ON");
            reconnect_attempts = 0;
            return ESP_OK;
        }
        connectivity_stop();
        connectivity_set_state(CONN_STATE_ERROR, "OBD not ready");
        return ESP_FAIL;
    }

    connectivity_set_state(CONN_STATE_OBD_READY, "OBD ready");
    obd_service_discover_supported_pids();
    reconnect_attempts = 0;
    return ESP_OK;
}

bool connectivity_has_saved_bt_profile(void)
{
    return g_settings.bt_device_addr[0] != '\0';
}

bool connectivity_bt_auto_connect_allowed(void)
{
    if (g_settings.bt_device_addr[0] != '\0') {
        return true;
    }
    return !g_settings.bt_manual_mode;
}

bool connectivity_bt_auto_connect_pending(void)
{
    return connectivity_auto_active;
}

bool connectivity_is_user_busy(void)
{
    return connectivity_user_busy;
}

static bool connectivity_bt_boot_allowed(void)
{
    return connectivity_bt_auto_connect_allowed();
}

static bool connectivity_wifi_boot_allowed(void)
{
    if (!g_settings.wifi_manual_mode) {
        return true;
    }
    return g_settings.wifi_ssid[0] != '\0';
}

esp_err_t connectivity_start(connection_type_t type)
{
    connectivity_lock();
    ESP_LOGI(TAG, "Starting connectivity: %d", type);

    if (type == CONN_TYPE_BLUETOOTH && !connectivity_bt_boot_allowed()) {
        connectivity_set_state(CONN_STATE_DISCONNECTED, "BT: Scan to connect");
        connectivity_unlock();
        ESP_LOGI(TAG, "Manual BT: no saved adapter, skipping boot connect");
        return ESP_ERR_INVALID_STATE;
    }

    if (type == CONN_TYPE_WIFI && !connectivity_wifi_boot_allowed()) {
        connectivity_set_state(CONN_STATE_DISCONNECTED, "WiFi: Scan to connect");
        connectivity_unlock();
        ESP_LOGI(TAG, "Manual WiFi: no saved network, skipping boot connect");
        return ESP_ERR_INVALID_STATE;
    }

    connectivity_stop();
    connectivity_unlock();

    return connectivity_bring_up(type);
}

void connectivity_stop(void)
{
    switch (current_type) {
        case CONN_TYPE_WIFI:
            wifi_disconnect();
            break;
        case CONN_TYPE_BLUETOOTH:
            bt_disconnect();
            break;
        case CONN_TYPE_USB:
            usb_cdc_disconnect();
            break;
        default:
            break;
    }

    current_type = CONN_TYPE_NONE;
    connectivity_set_state(CONN_STATE_DISCONNECTED, "Not connected");
    obd_service_on_disconnect();
}

esp_err_t connectivity_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (conn_state != CONN_STATE_OBD_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    connectivity_lock();

    if (conn_state != CONN_STATE_OBD_READY) {
        connectivity_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;

    switch (current_type) {
        case CONN_TYPE_WIFI:
            err = wifi_send_cmd(cmd, len, resp, resp_len);
            if (err != ESP_OK && !wifi_is_connected()) {
                connectivity_set_state(CONN_STATE_ERROR, "WiFi koptu");
                obd_service_on_disconnect();
            }
            break;

        case CONN_TYPE_BLUETOOTH:
            err = bt_send_cmd(cmd, len, resp, resp_len);
            if (err != ESP_OK && !bt_serial_ready()) {
                connectivity_set_state(CONN_STATE_ERROR, "Bluetooth koptu");
                obd_service_on_disconnect();
            }
            break;

        case CONN_TYPE_USB:
            err = usb_send_cmd(cmd, len, resp, resp_len);
            if (err != ESP_OK && !usb_is_connected()) {
                connectivity_set_state(CONN_STATE_ERROR, "USB koptu");
                obd_service_on_disconnect();
            }
            break;

        default:
            err = ESP_ERR_INVALID_STATE;
            break;
    }

    connectivity_unlock();
    return err;
}

connection_type_t connectivity_get_current_type(void)
{
    return current_type;
}

bool connectivity_is_connected(void)
{
    return conn_state == CONN_STATE_OBD_READY;
}

connectivity_state_t connectivity_get_state(void)
{
    return conn_state;
}

const char *connectivity_get_status_text(void)
{
    return status_text;
}

static void connectivity_sync_bt_serial_locked(void)
{
    if (current_type != CONN_TYPE_BLUETOOTH) {
        current_type = CONN_TYPE_BLUETOOTH;
    }
    if (conn_state == CONN_STATE_DISCONNECTED || conn_state == CONN_STATE_ERROR) {
        connectivity_set_state(CONN_STATE_LINK_UP, "BT linked");
    }
}

void connectivity_sync_transport_state(void)
{
    if (!bt_serial_ready()) {
        return;
    }

    connectivity_lock();
    connectivity_sync_bt_serial_locked();
    connectivity_unlock();
}

void bt_on_serial_ready(void)
{
    connectivity_lock();
    connectivity_sync_bt_serial_locked();
    connectivity_unlock();
}

void bt_on_serial_lost(void)
{
    if (connectivity_user_busy) {
        return;
    }

    connectivity_lock();
    if (current_type == CONN_TYPE_BLUETOOTH &&
        conn_state != CONN_STATE_DISCONNECTED) {
        connectivity_set_state(CONN_STATE_DISCONNECTED, "Bluetooth disconnected");
        obd_service_on_disconnect();
    }
    connectivity_unlock();
}

static TaskHandle_t obd_probe_task_h;

static void obd_probe_task(void *arg)
{
    const bool caps_stack = (bool)(intptr_t)arg;

    if (connectivity_begin_bt_op(0)) {
        connectivity_promote_obd_if_ready();
        connectivity_end_bt_op();
    }

    obd_probe_task_h = NULL;
    if (caps_stack) {
        vTaskDeleteWithCaps(NULL);
    } else {
        vTaskDelete(NULL);
    }
}

static void connectivity_schedule_obd_probe(void)
{
    if (obd_probe_task_h != NULL || connectivity_is_connected() || !bt_serial_ready()) {
        return;
    }

    BaseType_t ok = xTaskCreateWithCaps(
        obd_probe_task, "obd_probe", 8192, (void *)(intptr_t)1, 3, &obd_probe_task_h,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok == pdPASS) {
        return;
    }

    ok = xTaskCreate(obd_probe_task, "obd_probe", 8192, (void *)(intptr_t)0, 3, &obd_probe_task_h);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "OBD probe task create failed");
        obd_probe_task_h = NULL;
    }
}

void connectivity_promote_obd_if_ready(void)
{
    if (connectivity_user_busy || !bt_serial_ready()) {
        return;
    }

    connectivity_lock();
    const bool already = (conn_state == CONN_STATE_OBD_READY);
    connectivity_unlock();
    if (already) {
        return;
    }

    if (!bt_obd_session_ready()) {
        return;
    }

    connectivity_lock();
    if (conn_state != CONN_STATE_OBD_READY) {
        connectivity_set_state(CONN_STATE_OBD_READY, "OBD ready");
        reconnect_attempts = 0;
    }
    connectivity_unlock();
    obd_service_discover_supported_pids();
}

esp_err_t connectivity_auto_reconnect(void)
{
    if (connectivity_user_busy) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!connectivity_begin_bt_op(0)) {
        ESP_LOGD(TAG, "Auto-reconnect skipped: BLE busy");
        return ESP_ERR_INVALID_STATE;
    }

    if (!connectivity_try_lock()) {
        connectivity_end_bt_op();
        ESP_LOGD(TAG, "Reconnect skipped: connectivity busy");
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_state == CONN_STATE_OBD_READY) {
        connectivity_unlock();
        connectivity_end_bt_op();
        return ESP_OK;
    }

    if (bt_serial_ready()) {
        connectivity_sync_bt_serial_locked();
        connectivity_unlock();
        connectivity_end_bt_op();
        return ESP_OK;
    }

    connection_type_t preferred = g_settings.preferred_connection;
    if (preferred == CONN_TYPE_NONE) {
        preferred = CONN_TYPE_BLUETOOTH;
    }

    if (preferred == CONN_TYPE_BLUETOOTH && !connectivity_bt_auto_connect_allowed()) {
        connectivity_unlock();
        connectivity_end_bt_op();
        return ESP_ERR_INVALID_STATE;
    }

    if (preferred == CONN_TYPE_WIFI && g_settings.wifi_manual_mode &&
        g_settings.wifi_ssid[0] == '\0') {
        connectivity_unlock();
        connectivity_end_bt_op();
        return ESP_ERR_INVALID_STATE;
    }

    reconnect_attempts++;
    connectivity_unlock();

    connectivity_auto_active = true;
    ESP_LOGI(TAG, "Auto-reconnect attempt %u", reconnect_attempts);

    if (connectivity_user_busy) {
        connectivity_end_bt_op();
        return ESP_ERR_INVALID_STATE;
    }

    if (!connectivity_try_lock()) {
        connectivity_end_bt_op();
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_state == CONN_STATE_OBD_READY || bt_serial_ready()) {
        if (bt_serial_ready()) {
            connectivity_sync_bt_serial_locked();
        }
        reconnect_attempts = 0;
        connectivity_unlock();
        connectivity_end_bt_op();
        return ESP_OK;
    }

    connectivity_stop();
    connectivity_unlock();

    esp_err_t err = connectivity_bring_up(preferred);
    if (err != ESP_OK) {
        if (reconnect_attempts >= 8) {
            reconnect_attempts = 0;
        }
        conn_log_add("Auto-reconnect failed (attempt %u)", reconnect_attempts);
        ESP_LOGW(TAG, "Reconnection attempt failed");
        connectivity_set_state(CONN_STATE_DISCONNECTED, "Auto-connect retry...");
        connectivity_end_bt_op();
        return ESP_ERR_NOT_FOUND;
    }

    reconnect_attempts = 0;
    connectivity_end_bt_op();
    return ESP_OK;
}

void connectivity_on_async_bt_result(bool ok)
{
    connectivity_auto_active = false;

    if (ok) {
        connectivity_lock();
        current_type = CONN_TYPE_BLUETOOTH;
        connectivity_sync_bt_serial_locked();
        connectivity_unlock();
        reconnect_attempts = 0;
        ESP_LOGI(TAG, "Async auto-connect linked");
        return;
    }

    reconnect_attempts++;
    if (reconnect_attempts >= 8) {
        reconnect_attempts = 0;
    }
    conn_log_add("Auto-connect failed (attempt %u)", reconnect_attempts);
    ESP_LOGW(TAG, "Async auto-connect failed");
    connectivity_set_state(CONN_STATE_DISCONNECTED, "Auto-connect retry...");
}

void connectivity_maintain_bt(void)
{
    if (connectivity_user_busy || !connectivity_ui_ready()) {
        return;
    }

    connectivity_sync_transport_state();

#if BT_BACKGROUND_AUTO_CONNECT
    if (bt_serial_ready()) {
        connectivity_auto_active = false;
        if (!connectivity_is_connected()) {
            connectivity_schedule_obd_probe();
        }
        return;
    }

    if (connectivity_auto_active) {
        return;
    }

    const bool want_bt = (g_settings.preferred_connection == CONN_TYPE_BLUETOOTH ||
                          g_settings.preferred_connection == CONN_TYPE_NONE);
    if (!want_bt || !connectivity_bt_auto_connect_allowed() ||
        !connectivity_has_saved_bt_profile()) {
        return;
    }

    if (bt_try_connect_saved_async()) {
        connectivity_auto_active = true;
        connectivity_set_state(CONN_STATE_ELM_INIT, "Auto-connecting...");
        ESP_LOGI(TAG, "Queued async auto-connect");
    }
#endif
}

static esp_err_t connectivity_finish_bt_session(void)
{
    current_type = CONN_TYPE_BLUETOOTH;

    if (!bt_serial_ready()) {
        connectivity_set_state(CONN_STATE_ERROR, "Bluetooth/ELM327 yok");
        return ESP_FAIL;
    }

    connectivity_set_state(CONN_STATE_LINK_UP, "BT linked");
    conn_log_add("BT serial ready — OBD probe in background");
    reconnect_attempts = 0;
    return ESP_OK;
}

static void connectivity_bt_prepare_session(void)
{
    connectivity_lock();
    if (current_type == CONN_TYPE_BLUETOOTH) {
        connectivity_stop();
    } else {
        bt_disconnect();
    }
    connectivity_unlock();
}

esp_err_t connectivity_bt_scan(bt_device_info_t *list, int max_count, int *found_count,
                             int duration_ms)
{
    if (list == NULL || found_count == NULL || max_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!connectivity_begin_bt_op(pdMS_TO_TICKS(60000))) {
        return ESP_ERR_TIMEOUT;
    }

    bt_request_cancel_operations();
    vTaskDelay(pdMS_TO_TICKS(300));

    *found_count = 0;

    if (!connectivity_ensure_bt_worker()) {
        connectivity_end_bt_op();
        return ESP_ERR_INVALID_STATE;
    }

    if (duration_ms <= 0) {
        duration_ms = BT_SCAN_DEFAULT_MS;
    }
    *found_count = bt_scan_devices(list, max_count, duration_ms);
    connectivity_end_bt_op();

    if (*found_count > 0) {
        connectivity_set_state(CONN_STATE_DISCONNECTED, "BT: tap device to connect");
        return ESP_OK;
    }

    const char *err = bt_get_last_error();
    if (err != NULL) {
        conn_log_add("BLE scan UI: %s", err);
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t connectivity_bt_connect_manual(const char *addr, const char *name, uint8_t addr_type)
{
    if (addr == NULL || addr[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!connectivity_begin_bt_op(pdMS_TO_TICKS(90000))) {
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Manual BT connect: %s (type=%u)", addr, addr_type);

    if (!connectivity_ensure_bt_worker()) {
        connectivity_end_bt_op();
        return ESP_ERR_INVALID_STATE;
    }

    bt_request_cancel_operations();
    vTaskDelay(pdMS_TO_TICKS(200));
    connectivity_bt_prepare_session();

    esp_err_t err = ESP_FAIL;
    g_settings.bt_addr_type = addr_type;

    if (bt_connect_to_addr(addr)) {
        bt_save_device(name != NULL && name[0] != '\0' ? name : addr, addr, addr_type);
        g_settings.preferred_connection = CONN_TYPE_BLUETOOTH;
        g_settings.bt_manual_mode = false;
        settings_save(&g_settings);

        connectivity_lock();
        err = connectivity_finish_bt_session();
        connectivity_unlock();
    } else {
        const char *hint = bt_get_last_fail_hint();
        conn_log_add("BT connect failed: %s", hint != NULL ? hint : "unknown");
    }

    connectivity_end_bt_op();
    return err;
}

esp_err_t connectivity_bt_enable_auto_mode(void)
{
    if (!connectivity_begin_bt_op(pdMS_TO_TICKS(90000))) {
        return ESP_ERR_TIMEOUT;
    }

    bt_request_cancel_operations();
    vTaskDelay(pdMS_TO_TICKS(300));

    if (!connectivity_ensure_bt_worker()) {
        connectivity_end_bt_op();
        return ESP_ERR_INVALID_STATE;
    }

    if (g_settings.bt_device_addr[0] == '\0') {
        connectivity_end_bt_op();
        conn_log_add("BT auto: no saved device");
        return ESP_ERR_NOT_FOUND;
    }

    connectivity_lock();
    g_settings.bt_manual_mode = false;
    settings_save(&g_settings);
    if (current_type == CONN_TYPE_BLUETOOTH) {
        connectivity_stop();
    } else {
        bt_disconnect();
    }
    connectivity_unlock();

    esp_err_t err = ESP_FAIL;
    if (bt_connect_saved_profile()) {
        g_settings.preferred_connection = CONN_TYPE_BLUETOOTH;
        settings_save(&g_settings);

        connectivity_lock();
        err = connectivity_finish_bt_session();
        connectivity_unlock();
        connectivity_schedule_obd_probe();
    } else {
        const char *hint = bt_get_last_fail_hint();
        conn_log_add("BT auto failed: %s", hint != NULL ? hint : "unknown");
    }

    connectivity_end_bt_op();
    return err;
}

esp_err_t connectivity_bt_disconnect(void)
{
    if (!connectivity_begin_bt_op(pdMS_TO_TICKS(10000))) {
        return ESP_ERR_TIMEOUT;
    }
    connectivity_lock();
    if (current_type == CONN_TYPE_BLUETOOTH) {
        connectivity_stop();
    } else {
        bt_disconnect();
        connectivity_set_state(CONN_STATE_DISCONNECTED, "Disconnected");
    }
    connectivity_unlock();
    connectivity_end_bt_op();
    return ESP_OK;
}

esp_err_t connectivity_bt_forget(void)
{
    if (!connectivity_begin_bt_op(pdMS_TO_TICKS(10000))) {
        return ESP_ERR_TIMEOUT;
    }
    connectivity_lock();
    connectivity_stop();
    bt_forget_saved();
    connectivity_set_state(CONN_STATE_DISCONNECTED, "Saved device cleared");
    connectivity_unlock();
    connectivity_end_bt_op();
    return ESP_OK;
}
