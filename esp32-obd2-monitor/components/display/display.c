#include "display.h"
#include "connectivity.h"
#include "lvgl_driver.h"
#include "dashboard.h"
#include "splash.h"
#include "styles.h"
#include "app.h"
#include "haptic.h"
#include "telemetry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern app_settings_t g_settings;

static const char *TAG = "display";

static bool live_updates_enabled;

bool display_live_updates_enabled(void)
{
    return live_updates_enabled;
}

bool connectivity_ui_ready(void)
{
    return live_updates_enabled;
}

void display_set_live_updates(bool enable)
{
    live_updates_enabled = enable;
}

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");

    live_updates_enabled = false;

    lvgl_init();
    ESP_LOGI(TAG, "LVGL init done, setting backlight...");
    lvgl_set_backlight(g_settings.brightness);

    telemetry_init();
    haptic_init();

    ESP_LOGI(TAG, "Starting LVGL handler task...");
    lvgl_start();

    /* Give handler task a moment to start before we hold the lock */
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Creating dashboard UI under lock...");
    if (lvgl_lock(-1)) {
        ESP_LOGI(TAG, "Lock acquired, initializing styles...");
        styles_init(THEME_DARK);

        ESP_LOGI(TAG, "Creating all dashboard screens...");
        dashboard_init();

        ESP_LOGI(TAG, "Running splash animation...");
        splash_run_boot_animation(dashboard_get_main_screen());

        dashboard_finish_boot_screen();

        ESP_LOGI(TAG, "Switching to main dashboard...");
        lv_scr_load(dashboard_get_main_screen());

        /* DO NOT call lv_refr_now() here — handler is on core 1 and may be
         * blocked waiting for this lock. lv_scr_load triggers a repaint
         * internally; forcing lv_refr_now with handler blocked causes a
         * deadlock that manifests as a crash after splash. */
        live_updates_enabled = true;

        /* Brief yield so handler can process the screen load */
        lvgl_unlock();

        /* Give handler a chance to render the new screen */
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "Display ready, splash done");
    } else {
        ESP_LOGE(TAG, "Failed to acquire lvgl lock - display may be unstable");
    }

    ESP_LOGI(TAG, "Display initialized");
}

void display_set_brightness(uint8_t brightness)
{
    lvgl_set_backlight(brightness);
}
