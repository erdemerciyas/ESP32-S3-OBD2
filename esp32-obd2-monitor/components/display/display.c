#include "display.h"
#include "lvgl_driver.h"
#include "dashboard.h"
#include "splash.h"
#include "styles.h"
#include "app.h"
#include "haptic.h"
#include "telemetry.h"
#include "esp_log.h"

extern app_settings_t g_settings;

static const char *TAG = "display";

static bool live_updates_enabled;

bool display_live_updates_enabled(void)
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
    lvgl_set_backlight(g_settings.brightness);

    telemetry_init();
    haptic_init();

    lvgl_start();

    if (lvgl_lock(-1)) {
        styles_init(THEME_DARK);
        dashboard_init();
        splash_run_boot_animation(dashboard_get_main_screen());
        dashboard_finish_boot_screen();
        lv_scr_load(dashboard_get_main_screen());
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);
        live_updates_enabled = true;
        lvgl_unlock();
    }

    ESP_LOGI(TAG, "Display initialized");
}

void display_set_brightness(uint8_t brightness)
{
    lvgl_set_backlight(brightness);
}
