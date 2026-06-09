#include "haptic.h"
#include "lvgl_driver.h"
#include "board_config.h"
#include "app.h"

extern app_settings_t g_settings;
#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "haptic";

typedef enum {
    HAPTIC_PHASE_IDLE = 0,
    HAPTIC_PHASE_PULSE,
    HAPTIC_PHASE_ALERT_GAP,
    HAPTIC_PHASE_ALERT_PULSE2,
} haptic_phase_t;

static esp_timer_handle_t haptic_timer;
static haptic_phase_t haptic_phase;

static void buzzer_set(bool on)
{
    esp_io_expander_handle_t exp = board_expander_get();
    if (exp == NULL) {
        return;
    }
    esp_io_expander_set_level(exp, BOARD_EXIO_BUZZER, on ? 1 : 0);
}

static void haptic_timer_cb(void *arg)
{
    (void)arg;

    switch (haptic_phase) {
        case HAPTIC_PHASE_PULSE:
            buzzer_set(false);
            haptic_phase = HAPTIC_PHASE_IDLE;
            break;
        case HAPTIC_PHASE_ALERT_GAP:
            buzzer_set(true);
            haptic_phase = HAPTIC_PHASE_ALERT_PULSE2;
            esp_timer_start_once(haptic_timer, 100 * 1000);
            break;
        case HAPTIC_PHASE_ALERT_PULSE2:
            buzzer_set(false);
            haptic_phase = HAPTIC_PHASE_IDLE;
            break;
        default:
            buzzer_set(false);
            haptic_phase = HAPTIC_PHASE_IDLE;
            break;
    }
}

void haptic_init(void)
{
    if (haptic_timer != NULL) {
        return;
    }

    const esp_timer_create_args_t args = {
        .callback = haptic_timer_cb,
        .name = "haptic",
    };
    if (esp_timer_create(&args, &haptic_timer) != ESP_OK) {
        ESP_LOGW(TAG, "Haptic timer create failed");
        return;
    }

    ESP_LOGI(TAG, "Haptic ready");
}

void haptic_pulse_ms(uint16_t duration_ms)
{
    if (!g_settings.haptic_enabled || haptic_timer == NULL || duration_ms == 0) {
        return;
    }

    esp_timer_stop(haptic_timer);
    haptic_phase = HAPTIC_PHASE_PULSE;
    buzzer_set(true);
    esp_timer_start_once(haptic_timer, (uint64_t)duration_ms * 1000ULL);
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
    if (haptic_timer == NULL) {
        return;
    }

    esp_timer_stop(haptic_timer);
    haptic_phase = HAPTIC_PHASE_ALERT_GAP;
    buzzer_set(true);
    esp_timer_start_once(haptic_timer, 100 * 1000);
}
