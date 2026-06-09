#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "app.h"
#include "settings.h"
#include "conn_log.h"
#include "display.h"
#include "connectivity.h"
#include "bt_manager.h"
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

static void obd_polling_task(void *arg)
{
    (void)arg;
    while (1) {
        if (connectivity_is_connected()) {
            obd_service_poll_fast();
        }
        vTaskDelay(pdMS_TO_TICKS(OBD2_FAST_POLL_MS));
    }
}

static void obd_slow_poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));
    while (1) {
        if (connectivity_is_connected()) {
            obd_service_poll_slow();
        }
        vTaskDelay(pdMS_TO_TICKS(OBD2_SLOW_POLL_MS));
    }
}

static void obd_dtc_poll_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10000));
    while (1) {
        if (connectivity_is_connected()) {
            uint8_t dtc_codes[10];
            size_t dtc_count = 0;
            obd_service_get_dtc_codes(dtc_codes, sizeof(dtc_codes) / 2, &dtc_count);
        }
        vTaskDelay(pdMS_TO_TICKS(OBD2_DTC_POLL_MS));
    }
}

static void connectivity_reconnect_task(void *arg)
{
    (void)arg;

    /* Let display finish splash; first retry sooner when a profile is saved */
    vTaskDelay(pdMS_TO_TICKS(3000));

    while (1) {
        const bool want_bt = (g_settings.preferred_connection == CONN_TYPE_BLUETOOTH ||
                              g_settings.preferred_connection == CONN_TYPE_NONE);
        const bool saved_bt = want_bt && g_settings.bt_device_addr[0] != '\0';
        const TickType_t interval = pdMS_TO_TICKS(saved_bt ? 5000 : 15000);

        if (connectivity_is_connected()) {
            vTaskDelay(interval);
            continue;
        }

        if (bt_serial_ready()) {
            connectivity_sync_transport_state();
            if (!connectivity_is_connected()) {
                connectivity_promote_obd_if_ready();
            }
            vTaskDelay(interval);
            continue;
        }

        if (want_bt && connectivity_bt_auto_connect_allowed()) {
            ESP_LOGI(TAG, "OBD adapter not connected, retrying...");
            connectivity_auto_reconnect();
        }

        vTaskDelay(interval);
    }
}

static void obd_diagnostic_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 30 && !connectivity_is_connected(); i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (connectivity_is_connected()) {
        ESP_LOGI(TAG, "OBD diagnostic: reading DTC codes");
        uint8_t dtc_codes[10];
        size_t dtc_count = 0;
        obd_service_get_dtc_codes(dtc_codes, sizeof(dtc_codes) / 2, &dtc_count);
    } else {
        ESP_LOGW(TAG, "OBD diagnostic skipped: not connected");
    }
    vTaskDelete(NULL);
}

static void gauge_update_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000 / GAUGE_UPDATE_RATE_HZ);

    while (1) {
        display_update_gauges();
        vTaskDelayUntil(&last_wake, period);
    }
}

static SemaphoreHandle_t display_ready_sem;
static bool display_task_uses_caps;

static void connectivity_boot_task(void *arg)
{
    (void)arg;
    /* Wait for splash; BLE stack is already warmed up in app_main */
    vTaskDelay(pdMS_TO_TICKS(4000));

    connection_type_t boot_type = g_settings.preferred_connection;
    if (boot_type == CONN_TYPE_NONE) {
        boot_type = CONN_TYPE_BLUETOOTH;
    }

    esp_err_t conn_err = connectivity_start(boot_type);
    if (conn_err != ESP_OK && boot_type == CONN_TYPE_BLUETOOTH &&
        g_settings.bt_device_addr[0] != '\0') {
        ESP_LOGW(TAG, "Initial BT connect failed — reconnect task will retry");
    } else if (conn_err != ESP_OK) {
        ESP_LOGW(TAG, "Initial connectivity skipped/failed (manual mode or no adapter)");
    }
    vTaskDelete(NULL);
}

static void display_init_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "display_init task started (int_free=%u spiram_free=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    display_init();
    xSemaphoreGive(display_ready_sem);
    if (display_task_uses_caps) {
        vTaskDeleteWithCaps(NULL);
    } else {
        vTaskDelete(NULL);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s", APP_NAME, APP_VERSION);

    init_hardware();

    /* Load settings from NVS (falls back to defaults if empty) */
    settings_load(&g_settings);

    /* Connection diagnostics log — dump previous boot's reason to serial */
    conn_log_init();
    conn_log_dump();

    /* NimBLE NPL mutex alloc needs internal RAM; after LVGL only ~67KB remains and
     * nimble_port_init() asserts. Bring up BLE before display (~126KB free). */
    bt_set_rf_allowed(true);
    ESP_LOGI(TAG, "Early BLE init (int_free=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (!bt_init_stack() || !bt_warmup_stack()) {
        const char *err = bt_get_last_error();
        ESP_LOGW(TAG, "Early BLE init failed: %s", err != NULL ? err : "unknown");
    } else {
        ESP_LOGI(TAG, "BLE stack ready (int_free=%u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    /* Display init uses a lot of stack (LVGL + dashboard UI) — prefer PSRAM stack */
    display_ready_sem = xSemaphoreCreateBinary();
    display_task_uses_caps = false;

    ESP_LOGI(TAG, "Creating display_init task (int_free=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    BaseType_t disp_ok = xTaskCreateWithCaps(
        display_init_task, "display_init", 40960, NULL, 5, NULL,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (disp_ok == pdPASS) {
        display_task_uses_caps = true;
    } else {
        ESP_LOGW(TAG, "display_init PSRAM stack failed (int_free=%u), retry internal 32KB",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        disp_ok = xTaskCreate(display_init_task, "display_init", 32768, NULL, 5, NULL);
    }
    if (disp_ok != pdPASS) {
        ESP_LOGE(TAG, "display_init task create failed — cannot start UI");
    } else if (xSemaphoreTake(display_ready_sem, pdMS_TO_TICKS(90000)) != pdTRUE) {
        ESP_LOGE(TAG, "display_init timed out — UI may be unavailable");
    }
    vSemaphoreDelete(display_ready_sem);
    display_ready_sem = NULL;

    obd_service_init();

    /* UI + gauge tasks first; BT scan/connect runs in background (can take 30+ s) */
    xTaskCreate(gauge_update_task, "gauge_update", 12288, NULL, 4, NULL);
    xTaskCreate(connectivity_boot_task, "conn_boot", 12288, NULL, 4, NULL);

    /* Auto-reconnect when adapter drops or is not yet available */
    xTaskCreate(connectivity_reconnect_task, "conn_reconnect", 12288, NULL, 5, NULL);

    /* Run diagnostic task first (waits for connection) */
    xTaskCreate(obd_diagnostic_task, "obd_diagnostic", 8192, NULL, 6, NULL);

    /* Fast OBD polling: RPM / speed / throttle */
    xTaskCreate(obd_polling_task, "obd_fast", 4096, NULL, 6, NULL);
    xTaskCreate(obd_slow_poll_task, "obd_slow", 4096, NULL, 4, NULL);
    xTaskCreate(obd_dtc_poll_task, "obd_dtc", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Application started successfully");
}
