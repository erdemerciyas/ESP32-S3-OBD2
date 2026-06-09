#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "app.h"
#include "settings.h"

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "obd2_settings";
#define SETTINGS_SCHEMA_VERSION 5

static const uint32_t k_default_gauge_colors[APP_GAUGE_COUNT] = {
    0xF08A1C, 0xFFB44A, 0xB14A2A, 0x4AD6C2, 0xFFB44A,
    0x4AD6C2, 0xF08A1C, 0xF08A1C, 0x4AD6C2, 0xB14A2A,
};

void settings_init_gauge_defaults(app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    if (settings->max_rpm < 2000 || settings->max_rpm > 9000) {
        settings->max_rpm = 6500;
    }
    if (settings->max_speed < 40 || settings->max_speed > 320) {
        settings->max_speed = 180;
    }

    bool order_valid = true;
    bool seen[APP_GAUGE_COUNT] = {false};
    for (int i = 0; i < APP_GAUGE_COUNT; i++) {
        if (settings->gauge_order[i] >= APP_GAUGE_COUNT) {
            order_valid = false;
            break;
        }
        if (seen[settings->gauge_order[i]]) {
            order_valid = false;
            break;
        }
        seen[settings->gauge_order[i]] = true;
    }
    if (!order_valid) {
        for (int i = 0; i < APP_GAUGE_COUNT; i++) {
            settings->gauge_order[i] = (uint8_t)i;
        }
    }

    for (int i = 0; i < APP_GAUGE_COUNT; i++) {
        if (settings->gauge_colors[i] == 0) {
            settings->gauge_colors[i] = k_default_gauge_colors[i];
        }
    }
}

static void settings_apply_defaults(app_settings_t *settings)
{
    if (settings->preferred_connection == CONN_TYPE_NONE) {
        settings->preferred_connection = CONN_TYPE_BLUETOOTH;
    }

    if (settings->obd_adapter_port == 0) {
        settings->obd_adapter_port = OBD2_DEFAULT_ADAPTER_PORT;
    }

    if (settings->obd_adapter_ip[0] == '\0') {
        strncpy(settings->obd_adapter_ip, OBD2_DEFAULT_ADAPTER_IP,
                sizeof(settings->obd_adapter_ip) - 1);
    }

    if (settings->wifi_password[0] == '\0') {
        strncpy(settings->wifi_password, OBD2_DEFAULT_WIFI_PASSWORD,
                sizeof(settings->wifi_password) - 1);
    }

    if (settings->brightness == 0) {
        settings->brightness = 70;
    }

    settings->theme = THEME_DARK;

    if (settings->default_gauge >= APP_GAUGE_COUNT) {
        settings->default_gauge = 0;
    }

    settings_init_gauge_defaults(settings);
}

esp_err_t settings_load(app_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s, using defaults", esp_err_to_name(err));
        settings_apply_defaults(settings);
        return err;
    }

    size_t len;
    esp_err_t read_err;

    len = sizeof(settings->wifi_ssid);
    read_err = nvs_get_str(nvs, "wifi_ssid", settings->wifi_ssid, &len);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read wifi_ssid: %s", esp_err_to_name(read_err));
    }

    len = sizeof(settings->wifi_password);
    read_err = nvs_get_str(nvs, "wifi_pass", settings->wifi_password, &len);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read wifi_pass: %s", esp_err_to_name(read_err));
    }

    len = sizeof(settings->obd_adapter_ip);
    read_err = nvs_get_str(nvs, "obd_ip", settings->obd_adapter_ip, &len);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read obd_ip: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u16(nvs, "obd_port", &settings->obd_adapter_port);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read obd_port: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(nvs, "conn_type", (uint8_t *)&settings->preferred_connection);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read conn_type: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(nvs, "theme", (uint8_t *)&settings->theme);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read theme: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(nvs, "haptic", (uint8_t *)&settings->haptic_enabled);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read haptic: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(nvs, "sound", (uint8_t *)&settings->sound_enabled);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read sound: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(nvs, "bright", &settings->brightness);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read bright: %s", esp_err_to_name(read_err));
    }

    uint8_t manual = 0;
    read_err = nvs_get_u8(nvs, "wifi_manual", &manual);
    if (read_err == ESP_OK) {
        settings->wifi_manual_mode = (manual != 0);
    } else if (read_err == ESP_ERR_NVS_NOT_FOUND) {
        settings->wifi_manual_mode = true;
    }

    read_err = nvs_get_u8(nvs, "wifi_auth", &settings->wifi_authmode);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read wifi_auth: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(nvs, "def_gauge", &settings->default_gauge);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read def_gauge: %s", esp_err_to_name(read_err));
    }

    len = sizeof(settings->bt_device_name);
    read_err = nvs_get_str(nvs, "bt_name", settings->bt_device_name, &len);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read bt_name: %s", esp_err_to_name(read_err));
    }

    len = sizeof(settings->bt_device_addr);
    read_err = nvs_get_str(nvs, "bt_addr", settings->bt_device_addr, &len);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read bt_addr: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(nvs, "bt_addr_type", &settings->bt_addr_type);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read bt_addr_type: %s", esp_err_to_name(read_err));
    }

    uint8_t bt_manual = 0;
    read_err = nvs_get_u8(nvs, "bt_manual", &bt_manual);
    if (read_err == ESP_OK) {
        settings->bt_manual_mode = (bt_manual != 0);
    } else if (read_err == ESP_ERR_NVS_NOT_FOUND) {
        settings->bt_manual_mode = true;
    }

    read_err = nvs_get_u16(nvs, "max_rpm", &settings->max_rpm);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read max_rpm: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u16(nvs, "max_speed", &settings->max_speed);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read max_speed: %s", esp_err_to_name(read_err));
    }

    len = sizeof(settings->gauge_colors);
    read_err = nvs_get_blob(nvs, "gauge_clr", settings->gauge_colors, &len);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read gauge_clr: %s", esp_err_to_name(read_err));
    }

    len = sizeof(settings->gauge_order);
    read_err = nvs_get_blob(nvs, "gauge_ord", settings->gauge_order, &len);
    if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read gauge_ord: %s", esp_err_to_name(read_err));
    }

    uint8_t schema = 0;
    bool persist_migration = false;
    read_err = nvs_get_u8(nvs, "schema", &schema);
    if (read_err == ESP_OK && schema < SETTINGS_SCHEMA_VERSION) {
        ESP_LOGI(TAG, "NVS schema %u -> %u (defaults applied)", schema, SETTINGS_SCHEMA_VERSION);
        if (schema < 3 && settings->wifi_ssid[0] != '\0') {
            settings->wifi_manual_mode = true;
            persist_migration = true;
            ESP_LOGI(TAG, "Migrated saved SSID to manual-only WiFi mode");
        }
        if (schema < 4) {
            settings->preferred_connection = CONN_TYPE_BLUETOOTH;
            settings->bt_manual_mode = true;
            persist_migration = true;
            ESP_LOGI(TAG, "Migrated default transport to Bluetooth");
        }
        if (schema < 5) {
            settings->theme = THEME_DARK;
            settings->max_rpm = 6500;
            settings->max_speed = 180;
            for (int i = 0; i < APP_GAUGE_COUNT; i++) {
                settings->gauge_colors[i] = k_default_gauge_colors[i];
                settings->gauge_order[i] = (uint8_t)i;
            }
            persist_migration = true;
            ESP_LOGI(TAG, "Migrated gauge limits/order/colors and dark-only theme");
        }
    }

    nvs_close(nvs);

    settings_apply_defaults(settings);

    if (settings->bt_device_addr[0] != '\0') {
        settings->bt_manual_mode = false;
    }

    if (persist_migration) {
        settings_save(settings);
    }

    ESP_LOGI(TAG, "Settings loaded (conn=%d, bt=%s, manual_bt=%d, gauge=%u, rpm=%u, km=%u)",
             settings->preferred_connection,
             settings->bt_device_addr[0] ? settings->bt_device_addr : "(tara)",
             settings->bt_manual_mode,
             settings->default_gauge,
             settings->max_rpm,
             settings->max_speed);
    return ESP_OK;
}

esp_err_t settings_save(const app_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(nvs, "wifi_ssid", settings->wifi_ssid);
    nvs_set_str(nvs, "wifi_pass", settings->wifi_password);
    nvs_set_str(nvs, "obd_ip", settings->obd_adapter_ip);
    nvs_set_u16(nvs, "obd_port", settings->obd_adapter_port);
    nvs_set_u8(nvs, "conn_type", (uint8_t)settings->preferred_connection);
    nvs_set_u8(nvs, "theme", THEME_DARK);
    nvs_set_u8(nvs, "haptic", settings->haptic_enabled ? 1 : 0);
    nvs_set_u8(nvs, "sound", settings->sound_enabled ? 1 : 0);
    nvs_set_u8(nvs, "bright", settings->brightness);
    nvs_set_u8(nvs, "wifi_manual", settings->wifi_manual_mode ? 1 : 0);
    nvs_set_u8(nvs, "wifi_auth", settings->wifi_authmode);
    nvs_set_u8(nvs, "def_gauge", settings->default_gauge);
    nvs_set_str(nvs, "bt_name", settings->bt_device_name);
    nvs_set_str(nvs, "bt_addr", settings->bt_device_addr);
    nvs_set_u8(nvs, "bt_addr_type", settings->bt_addr_type);
    nvs_set_u8(nvs, "bt_manual", settings->bt_manual_mode ? 1 : 0);
    nvs_set_u16(nvs, "max_rpm", settings->max_rpm);
    nvs_set_u16(nvs, "max_speed", settings->max_speed);
    nvs_set_blob(nvs, "gauge_clr", settings->gauge_colors, sizeof(settings->gauge_colors));
    nvs_set_blob(nvs, "gauge_ord", settings->gauge_order, sizeof(settings->gauge_order));
    nvs_set_u8(nvs, "schema", SETTINGS_SCHEMA_VERSION);

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }

    nvs_close(nvs);

    ESP_LOGI(TAG, "Settings saved to NVS");
    return err;
}

void settings_reset(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed during reset: %s", esp_err_to_name(err));
        return;
    }

    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Settings reset to defaults");
}
