#include "display.h"
#include "lvgl_driver.h"
#include "dashboard.h"
#include "esp_log.h"

static const char *TAG = "display";

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");

    lvgl_init();

    dashboard_init();

    ESP_LOGI(TAG, "Display initialized");
}

void display_set_brightness(uint8_t brightness)
{
    ESP_LOGI(TAG, "Setting brightness: %d%%", brightness);
}
