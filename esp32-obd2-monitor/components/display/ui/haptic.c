#include "haptic.h"
#include "lvgl_driver.h"
#include "board_config.h"
#include "app.h"

extern app_settings_t g_settings;
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "haptic";

static void buzzer_set(bool on)
{
    esp_io_expander_handle_t exp = board_expander_get();
    if (exp == NULL) {
        return;
    }
    esp_io_expander_set_level(exp, BOARD_EXIO_BUZZER, on ? 1 : 0);
}

void haptic_init(void)
{
    ESP_LOGI(TAG, "Haptic ready");
}

void haptic_pulse_ms(uint16_t duration_ms)
{
    if (!g_settings.haptic_enabled) {
        return;
    }
    buzzer_set(true);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_set(false);
}

void haptic_click(void)
{
    haptic_pulse_ms(35);
}

void haptic_alert(void)
{
    if (!g_settings.haptic_enabled && !g_settings.sound_enabled) {
        return;
    }
    haptic_pulse_ms(100);
    vTaskDelay(pdMS_TO_TICKS(60));
    haptic_pulse_ms(100);
}
