#include "connectivity.h"
#include "wifi_manager.h"
#include "bt_manager.h"
#include "usb_manager.h"
#include "esp_log.h"
#include "app.h"

static const char *TAG = "connectivity";

static connection_type_t current_type = CONN_TYPE_NONE;
static bool is_connected = false;

extern app_settings_t g_settings;

esp_err_t connectivity_start(connection_type_t type)
{
    ESP_LOGI(TAG, "Starting connectivity: %d", type);

    connectivity_stop();

    switch (type) {
        case CONN_TYPE_WIFI:
            is_connected = wifi_connect(g_settings.wifi_ssid, g_settings.wifi_password);
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
            return ESP_ERR_INVALID_ARG;
    }

    if (is_connected) {
        ESP_LOGI(TAG, "Connected via: %d", current_type);
    }

    return is_connected ? ESP_OK : ESP_FAIL;
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
    if (!is_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (current_type) {
        case CONN_TYPE_WIFI:
            return wifi_send_cmd(cmd, len, resp, resp_len);

        case CONN_TYPE_BLUETOOTH:
            return bt_send_cmd(cmd, len, resp, resp_len);

        case CONN_TYPE_USB:
            return usb_send_cmd(cmd, len, resp, resp_len);

        default:
            return ESP_ERR_INVALID_STATE;
    }
}

connection_type_t connectivity_get_current_type(void)
{
    return current_type;
}

bool connectivity_is_connected(void)
{
    return is_connected;
}

esp_err_t connectivity_auto_reconnect(void)
{
    ESP_LOGI(TAG, "Attempting auto-reconnect...");

    esp_err_t err;

    err = connectivity_start(CONN_TYPE_USB);
    if (err == ESP_OK) return ESP_OK;

    err = connectivity_start(CONN_TYPE_WIFI);
    if (err == ESP_OK) return ESP_OK;

    err = connectivity_start(CONN_TYPE_BLUETOOTH);
    if (err == ESP_OK) return ESP_OK;

    ESP_LOGW(TAG, "All reconnection attempts failed");
    return ESP_ERR_NOT_FOUND;
}
