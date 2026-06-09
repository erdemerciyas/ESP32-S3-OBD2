#include "connectivity.h"

#include "wifi_manager.h"
#include "bt_manager.h"
#include "usb_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "app.h"
#include "settings.h"
#include "conn_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "connectivity";

static connection_type_t current_type = CONN_TYPE_NONE;
static connectivity_state_t conn_state = CONN_STATE_DISCONNECTED;
static char status_text[64] = "Not connected";
static uint8_t reconnect_attempts;
static SemaphoreHandle_t connectivity_mutex;
static volatile bool connectivity_user_busy;

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

extern app_settings_t g_settings;

__attribute__((weak)) void obd_service_on_disconnect(void) {}

__attribute__((weak)) esp_err_t obd_service_discover_supported_pids(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

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
            return connectivity_ensure_bt_worker() && bt_link_up();
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
    connectivity_set_state(CONN_STATE_DISCONNECTED, "Connecting...");

    if (!transport_link_up(type)) {
        connectivity_set_state(CONN_STATE_ERROR, "Connection failed");
        return ESP_FAIL;
    }

    current_type = type;
    connectivity_set_state(CONN_STATE_LINK_UP, "ELM327 init...");

    if (!transport_obd_ready(type)) {
        if (type == CONN_TYPE_WIFI && wifi_tcp_ready()) {
            connectivity_set_state(CONN_STATE_LINK_UP, "TCP ready - ignition ON");
            reconnect_attempts = 0;
            return ESP_OK;
        }
        if (type == CONN_TYPE_BLUETOOTH && bt_serial_ready()) {
            connectivity_set_state(CONN_STATE_LINK_UP, "BT ready - ignition ON");
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

bool connectivity_bt_auto_connect_allowed(void)
{
    if (g_settings.bt_device_addr[0] != '\0') {
        return true;
    }
    return !g_settings.bt_manual_mode;
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
            if (err != ESP_OK && !bt_is_connected()) {
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

void connectivity_sync_transport_state(void)
{
    if (connectivity_user_busy) {
        return;
    }

    if (!bt_serial_ready()) {
        return;
    }

    connectivity_lock();

    if (current_type != CONN_TYPE_BLUETOOTH) {
        current_type = CONN_TYPE_BLUETOOTH;
        if (conn_state == CONN_STATE_DISCONNECTED || conn_state == CONN_STATE_ERROR) {
            connectivity_set_state(CONN_STATE_LINK_UP, "BT linked");
        }
    } else if (conn_state == CONN_STATE_DISCONNECTED || conn_state == CONN_STATE_ERROR) {
        connectivity_set_state(CONN_STATE_LINK_UP, "BT ready - ignition ON");
    }

    connectivity_unlock();
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

    if (!connectivity_try_lock()) {
        ESP_LOGD(TAG, "Reconnect skipped: connectivity busy");
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_state == CONN_STATE_OBD_READY) {
        connectivity_unlock();
        return ESP_OK;
    }

    connection_type_t preferred = g_settings.preferred_connection;
    if (preferred == CONN_TYPE_NONE) {
        preferred = CONN_TYPE_BLUETOOTH;
    }

    if (preferred == CONN_TYPE_BLUETOOTH && !connectivity_bt_auto_connect_allowed()) {
        connectivity_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (preferred == CONN_TYPE_WIFI && g_settings.wifi_manual_mode &&
        g_settings.wifi_ssid[0] == '\0') {
        connectivity_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (reconnect_attempts >= 5) {
        reconnect_attempts = 0;
        connectivity_unlock();
        ESP_LOGW(TAG, "Reconnect backoff reset");
        return ESP_ERR_TIMEOUT;
    }

    reconnect_attempts++;
    uint32_t delay_ms = 1000U << (reconnect_attempts > 4 ? 4 : reconnect_attempts);
    connectivity_unlock();

    ESP_LOGI(TAG, "Auto-reconnect attempt %u (backoff %lu ms)",
             reconnect_attempts, (unsigned long)delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    if (connectivity_user_busy) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!connectivity_try_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (conn_state == CONN_STATE_OBD_READY) {
        connectivity_unlock();
        return ESP_OK;
    }

    connectivity_stop();
    connectivity_unlock();

    esp_err_t err = connectivity_bring_up(preferred);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reconnection attempt failed");
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

static esp_err_t connectivity_finish_bt_session(void)
{
    current_type = CONN_TYPE_BLUETOOTH;

    if (!bt_serial_ready()) {
        connectivity_set_state(CONN_STATE_ERROR, "Bluetooth/ELM327 yok");
        return ESP_FAIL;
    }

    connectivity_set_state(CONN_STATE_LINK_UP, "ELM327 BT ready");

    if (bt_obd_session_ready()) {
        connectivity_set_state(CONN_STATE_OBD_READY, "OBD ready");
        conn_log_add("OBD READY (BT, RPM response received)");
        obd_service_discover_supported_pids();
        reconnect_attempts = 0;
        return ESP_OK;
    }

    connectivity_set_state(CONN_STATE_LINK_UP, "BT ready - ignition ON");
    conn_log_add("BT OK but no OBD response - ignition may be OFF");
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

    *found_count = 0;
    connectivity_set_user_busy(true);
    bt_request_cancel_operations();

    if (!connectivity_ensure_bt_worker()) {
        connectivity_set_user_busy(false);
        return ESP_ERR_INVALID_STATE;
    }

    if (duration_ms <= 0) {
        duration_ms = BT_SCAN_DEFAULT_MS;
    }
    *found_count = bt_scan_devices(list, max_count, duration_ms);
    connectivity_set_user_busy(false);

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

    ESP_LOGI(TAG, "Manual BT connect: %s (type=%u)", addr, addr_type);
    connectivity_set_user_busy(true);

    if (!connectivity_ensure_bt_worker()) {
        connectivity_set_user_busy(false);
        return ESP_ERR_INVALID_STATE;
    }

    bt_request_cancel_operations();
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

    connectivity_set_user_busy(false);
    return err;
}

esp_err_t connectivity_bt_enable_auto_mode(void)
{
    connectivity_set_user_busy(true);

    if (!connectivity_ensure_bt_worker()) {
        connectivity_set_user_busy(false);
        return ESP_ERR_INVALID_STATE;
    }

    bt_prepare_for_operation();

    connectivity_lock();
    g_settings.bt_manual_mode = false;
    settings_save(&g_settings);
    connectivity_stop();
    connectivity_unlock();

    esp_err_t err = ESP_FAIL;
    if (bt_connect_auto()) {
        g_settings.preferred_connection = CONN_TYPE_BLUETOOTH;
        settings_save(&g_settings);

        connectivity_lock();
        err = connectivity_finish_bt_session();
        connectivity_unlock();
    } else {
        const char *hint = bt_get_last_fail_hint();
        conn_log_add("BT auto failed: %s", hint != NULL ? hint : "unknown");
    }

    connectivity_set_user_busy(false);
    return err;
}

esp_err_t connectivity_bt_disconnect(void)
{
    connectivity_set_user_busy(true);
    connectivity_lock();
    if (current_type == CONN_TYPE_BLUETOOTH) {
        connectivity_stop();
    } else {
        bt_disconnect();
        connectivity_set_state(CONN_STATE_DISCONNECTED, "Disconnected");
    }
    connectivity_unlock();
    connectivity_set_user_busy(false);
    return ESP_OK;
}

esp_err_t connectivity_bt_forget(void)
{
    connectivity_set_user_busy(true);
    connectivity_lock();
    connectivity_stop();
    bt_forget_saved();
    connectivity_set_state(CONN_STATE_DISCONNECTED, "Saved device cleared");
    connectivity_unlock();
    connectivity_set_user_busy(false);
    return ESP_OK;
}
