#include "connectivity.h"
#include "wifi_manager.h"
#include "bt_manager.h"
#include "usb_manager.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app.h"
#include "conn_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "connectivity";

static connection_type_t current_type = CONN_TYPE_NONE;
static connectivity_state_t conn_state = CONN_STATE_DISCONNECTED;
static char status_text[64] = "Bağlantı yok";
static uint8_t reconnect_attempts;
static SemaphoreHandle_t connectivity_mutex;
static volatile bool connectivity_user_wifi_busy;

static bool connectivity_try_lock(void)
{
    if (connectivity_mutex == NULL) {
        connectivity_mutex = xSemaphoreCreateMutex();
    }
    return xSemaphoreTake(connectivity_mutex, 0) == pdTRUE;
}

static void connectivity_set_user_wifi_busy(bool busy)
{
    connectivity_user_wifi_busy = busy;
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

static bool transport_link_up(connection_type_t type)
{
    switch (type) {
        case CONN_TYPE_WIFI:
            return wifi_link_up();
        case CONN_TYPE_USB:
            return usb_link_up();
        case CONN_TYPE_BLUETOOTH:
            return bt_connect();
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
        default:
            return false;
    }
}

static esp_err_t connectivity_bring_up(connection_type_t type)
{
    connectivity_set_state(CONN_STATE_DISCONNECTED, "Bağlanıyor...");

    if (!transport_link_up(type)) {
        connectivity_set_state(CONN_STATE_ERROR, "Bağlantı başarısız");
        return ESP_FAIL;
    }

    current_type = type;
    connectivity_set_state(CONN_STATE_LINK_UP, "ELM327 başlatılıyor...");

    if (!transport_obd_ready(type)) {
        if (type == CONN_TYPE_WIFI && wifi_tcp_ready()) {
            connectivity_set_state(CONN_STATE_LINK_UP, "TCP hazir, kontak acin");
            reconnect_attempts = 0;
            return ESP_OK;
        }
        connectivity_stop();
        connectivity_set_state(CONN_STATE_ERROR, "OBD hazır değil");
        return ESP_FAIL;
    }

    connectivity_set_state(CONN_STATE_OBD_READY, "OBD hazır");
    obd_service_discover_supported_pids();
    reconnect_attempts = 0;
    return ESP_OK;
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

    if (type == CONN_TYPE_WIFI && !connectivity_wifi_boot_allowed()) {
        connectivity_set_state(CONN_STATE_DISCONNECTED, "WiFi: Tara ile seçin");
        connectivity_unlock();
        ESP_LOGI(TAG, "Manual WiFi: no saved network, skipping boot connect");
        return ESP_ERR_INVALID_STATE;
    }

    connectivity_stop();
    connectivity_unlock();

    esp_err_t result = connectivity_bring_up(type);
    return result;
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
    connectivity_set_state(CONN_STATE_DISCONNECTED, "Bağlantı yok");
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

esp_err_t connectivity_auto_reconnect(void)
{
    if (connectivity_user_wifi_busy) {
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

    if (g_settings.preferred_connection == CONN_TYPE_WIFI &&
        g_settings.wifi_manual_mode &&
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
    connection_type_t preferred = g_settings.preferred_connection;
    if (preferred == CONN_TYPE_NONE) {
        preferred = CONN_TYPE_WIFI;
    }
    connectivity_unlock();

    ESP_LOGI(TAG, "Auto-reconnect attempt %u (backoff %lu ms)", reconnect_attempts, (unsigned long)delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    if (connectivity_user_wifi_busy) {
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

    if (err != ESP_OK && preferred != CONN_TYPE_WIFI && !connectivity_user_wifi_busy) {
        connectivity_stop();
        err = connectivity_bring_up(CONN_TYPE_WIFI);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "All reconnection attempts failed");
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t connectivity_wifi_scan(wifi_ap_info_t *list, int max_count, int *found_count)
{
    if (list == NULL || found_count == NULL || max_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    connectivity_set_user_wifi_busy(true);
    *found_count = wifi_scan_all_networks(list, max_count);
    connectivity_set_user_wifi_busy(false);

    return (*found_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t connectivity_finish_wifi_session(void)
{
    current_type = CONN_TYPE_WIFI;

    if (!wifi_tcp_ready()) {
        connectivity_set_state(CONN_STATE_ERROR, "TCP/ELM327 yok");
        return ESP_FAIL;
    }

    connectivity_set_state(CONN_STATE_LINK_UP, "ELM327 TCP hazir");

    if (wifi_obd_session_ready()) {
        connectivity_set_state(CONN_STATE_OBD_READY, "OBD hazır");
        conn_log_add("OBD HAZIR (RPM yaniti alindi)");
        obd_service_discover_supported_pids();
        reconnect_attempts = 0;
        return ESP_OK;
    }

    /* TCP OK but no RPM (ignition off) — still useful for user */
    connectivity_set_state(CONN_STATE_LINK_UP, "TCP hazir, kontak acin");
    conn_log_add("TCP OK ama OBD yaniti yok — kontak kapali olabilir");
    reconnect_attempts = 0;
    return ESP_OK;
}

esp_err_t connectivity_wifi_connect_manual(const char *ssid, wifi_auth_mode_t authmode)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Manual WiFi connect: %s", ssid);
    connectivity_set_user_wifi_busy(true);

    connectivity_lock();
    connectivity_stop();
    connectivity_unlock();

    esp_err_t err = ESP_FAIL;
    if (wifi_connect_manual_network(ssid, authmode)) {
        connectivity_lock();
        err = connectivity_finish_wifi_session();
        connectivity_unlock();
    } else if (wifi_is_ap_connected() && !wifi_tcp_ready()) {
        connectivity_lock();
        connectivity_set_state(CONN_STATE_LINK_UP, "WiFi var, TCP yok");
        connectivity_unlock();
        err = ESP_FAIL;
    }

    connectivity_set_user_wifi_busy(false);
    return err;
}

esp_err_t connectivity_wifi_enable_auto_mode(void)
{
    connectivity_set_user_wifi_busy(true);

    connectivity_lock();
    wifi_clear_manual_network();
    connectivity_stop();
    connectivity_unlock();

    esp_err_t err = ESP_FAIL;
    if (wifi_connect_to_obd_adapter()) {
        connectivity_lock();
        err = connectivity_finish_wifi_session();
        connectivity_unlock();
    } else if (wifi_is_ap_connected() && !wifi_tcp_ready()) {
        connectivity_lock();
        connectivity_set_state(CONN_STATE_LINK_UP, "WiFi var, TCP yok");
        connectivity_unlock();
    }

    connectivity_set_user_wifi_busy(false);
    return err;
}
