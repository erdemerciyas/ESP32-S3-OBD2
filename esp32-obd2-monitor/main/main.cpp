#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "app.h"
#include "settings.h"
#include "display.h"
#include "connectivity.h"
#include "obd_service.h"
#include "dashboard.h"
#include "gauge.h"

static const char *TAG = "main";

app_settings_t g_settings;

static void init_hardware(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Default event loop must be created before any component registers
     * esp_event_handler_instance_register() handlers (WiFi, IP, etc.). */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_LOGI(TAG, "Hardware init complete");
}

static void gauge_update_task(void *arg)
{
    obd_data_t data;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000 / GAUGE_UPDATE_RATE_HZ);

    while (1) {
        obd_service_get_data(&data);

        if (data.rpm_valid)        dashboard_set_rpm(data.rpm);
        if (data.speed_valid)       dashboard_set_speed(data.speed);
        if (data.coolant_valid)     dashboard_set_coolant(data.coolant_temp);
        if (data.throttle_valid)    dashboard_set_throttle(data.throttle_pos);
        if (data.fuel_valid)        dashboard_set_fuel(data.fuel_level);
        if (data.load_valid)        dashboard_set_load(data.engine_load);
        if (data.intake_valid)      dashboard_set_intake(data.intake_temp);
        if (data.maf_valid)         dashboard_set_maf(data.maf_rate);

        vTaskDelayUntil(&last_wake, period);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s", APP_NAME, APP_VERSION);

    init_hardware();

    /* Load settings from NVS (falls back to defaults if empty) */
    settings_load(&g_settings);

    /* Initialize display first so user sees something even if OBD fails */
    display_init();

    obd_service_init();

    /* Start connectivity (WiFi/BT/USB) */
    esp_err_t conn_err = connectivity_start(g_settings.preferred_connection);
    if (conn_err != ESP_OK) {
        ESP_LOGW(TAG, "Initial connectivity failed, will retry in background");
    }

    /* Run diagnostic task first (waits for connection) */
    xTaskCreate(obd_diagnostic_task, "obd_diagnostic", 8192, NULL, 6, NULL);

    /* Continuous OBD2 polling */
    xTaskCreate(obd_polling_task, "obd_poll", 4096, NULL, 5, NULL);

    /* UI gauge updates */
    xTaskCreate(gauge_update_task, "gauge_update", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "Application started successfully");
}
