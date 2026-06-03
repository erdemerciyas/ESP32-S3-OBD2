#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "app.h"

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "obd2_settings";

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

    nvs_close(nvs);

    ESP_LOGI(TAG, "Settings loaded from NVS");
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
    nvs_set_u8(nvs, "theme", (uint8_t)settings->theme);
    nvs_set_u8(nvs, "haptic", settings->haptic_enabled ? 1 : 0);
    nvs_set_u8(nvs, "sound", settings->sound_enabled ? 1 : 0);
    nvs_set_u8(nvs, "bright", settings->brightness);

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
