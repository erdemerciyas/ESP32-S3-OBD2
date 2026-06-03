#include "connectivity.h"
#include "wifi_manager.h"
#include "bt_manager.h"
#include "usb_manager.h"
#include "esp_log.h"
#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "connectivity";

static connection_type_t current_type = CONN_TYPE_NONE;
static bool is_connected = false;
static SemaphoreHandle_t connectivity_mutex;

extern app_settings_t g_settings;

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

esp_err_t connectivity_start(connection_type_t type)
{
    connectivity_lock();

    ESP_LOGI(TAG, "Starting connectivity: %d", type);

    connectivity_stop();

    switch (type) {
        case CONN_TYPE_WIFI:
            is_connected = wifi_connect_to_obd_adapter();
            if (is_connected) {
                current_type = CONN_TYPE_WIFI;
            }
            break;

        case CONN_TYPE_BLUETOOTH:
            is_connected = bt_connect();
            if (is_connected) {
                current_type = CONN_TYPE_BLUETOOTH;
            }
            break;

        case CONN_TYPE_USB:
            is_connected = usb_cdc_connect();
            if (is_connected) {
                current_type = CONN_TYPE_USB;
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown connection type: %d", type);
            connectivity_unlock();
            return ESP_ERR_INVALID_ARG;
    }

    if (is_connected) {
        ESP_LOGI(TAG, "Connected via: %d", current_type);
    }

    esp_err_t result = is_connected ? ESP_OK : ESP_FAIL;
    connectivity_unlock();
    return result;
}

void connectivity_stop(void)
{
    ESP_LOGI(TAG, "Stopping connectivity...");

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
    is_connected = false;
}

esp_err_t connectivity_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (!connectivity_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    connectivity_lock();

    if (!connectivity_is_connected()) {
        connectivity_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;

    switch (current_type) {
        case CONN_TYPE_WIFI:
            err = wifi_send_cmd(cmd, len, resp, resp_len);
            if (err != ESP_OK && !wifi_is_connected()) {
                is_connected = false;
            }
            break;

        case CONN_TYPE_BLUETOOTH:
            err = bt_send_cmd(cmd, len, resp, resp_len);
            break;

        case CONN_TYPE_USB:
            err = usb_send_cmd(cmd, len, resp, resp_len);
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
    switch (current_type) {
        case CONN_TYPE_WIFI:
            return wifi_is_connected();
        default:
            return is_connected;
    }
}

esp_err_t connectivity_auto_reconnect(void)
{
    connectivity_lock();

    ESP_LOGI(TAG, "Attempting auto-reconnect...");

    connection_type_t preferred = g_settings.preferred_connection;
    if (preferred == CONN_TYPE_NONE) {
        preferred = CONN_TYPE_WIFI;
    }

    esp_err_t err = ESP_FAIL;

    connectivity_stop();

    switch (preferred) {
        case CONN_TYPE_WIFI:
            is_connected = wifi_connect_to_obd_adapter();
            if (is_connected) {
                current_type = CONN_TYPE_WIFI;
                err = ESP_OK;
            }
            break;
        case CONN_TYPE_USB:
            is_connected = usb_cdc_connect();
            if (is_connected) {
                current_type = CONN_TYPE_USB;
                err = ESP_OK;
            }
            break;
        case CONN_TYPE_BLUETOOTH:
            is_connected = bt_connect();
            if (is_connected) {
                current_type = CONN_TYPE_BLUETOOTH;
                err = ESP_OK;
            }
            break;
        default:
            break;
    }

    if (err != ESP_OK && preferred != CONN_TYPE_WIFI) {
        is_connected = wifi_connect_to_obd_adapter();
        if (is_connected) {
            current_type = CONN_TYPE_WIFI;
            err = ESP_OK;
        }
    }

    connectivity_unlock();

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

    connectivity_lock();
    *found_count = wifi_scan_all_networks(list, max_count);
    connectivity_unlock();

    return (*found_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t connectivity_wifi_connect_manual(const char *ssid, wifi_auth_mode_t authmode)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    connectivity_lock();
    connectivity_stop();

    is_connected = wifi_connect_manual_network(ssid, authmode);
    if (is_connected) {
        current_type = CONN_TYPE_WIFI;
    }

    esp_err_t err = is_connected ? ESP_OK : ESP_FAIL;
    connectivity_unlock();
    return err;
}

esp_err_t connectivity_wifi_enable_auto_mode(void)
{
    connectivity_lock();
    wifi_clear_manual_network();
    connectivity_stop();

    is_connected = wifi_connect_to_obd_adapter();
    if (is_connected) {
        current_type = CONN_TYPE_WIFI;
    }

    esp_err_t err = is_connected ? ESP_OK : ESP_FAIL;
    connectivity_unlock();
    return err;
}
