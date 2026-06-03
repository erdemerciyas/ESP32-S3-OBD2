#include "app.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "app";

extern app_settings_t g_settings;

void app_settings_init(void)
{
    ESP_LOGI(TAG, "Initializing app settings...");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased, doing so...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "App settings initialized");
}

void app_settings_save(void)
{
    ESP_LOGI(TAG, "Saving app settings...");
    extern esp_err_t settings_save(const app_settings_t *settings);
    esp_err_t err = settings_save(&g_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings: %s", esp_err_to_name(err));
    }
}
