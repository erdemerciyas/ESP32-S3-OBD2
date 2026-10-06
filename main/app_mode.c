#include "app_mode.h"
#include "obd_link.h"
#include "nav_service.h"
#include "roll_feed.h"
#include "app_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "app_mode";
static const char *NVS_NS = "app";
static const char *NVS_KEY_MODE = "mode";

#define MODE_TASK_STACK 4096
#define MODE_TASK_PRIO  3

static volatile app_mode_t s_target = APP_MODE_OBD;   /* istenen */
static volatile app_mode_t s_cur = APP_MODE_OBD;       /* radyonun gerçek durumu */
static TaskHandle_t s_task;
static bool s_nav_on;   /* telefon servisi açık (NAV ve ROLL ortak) */

static const char *mode_name(app_mode_t m)
{
    return m == APP_MODE_NAV ? "NAV" : m == APP_MODE_ROLL ? "ROLL" : "OBD";
}

static const char *mode_tag(app_mode_t m)
{
    return m == APP_MODE_NAV ? "nav" : m == APP_MODE_ROLL ? "roll" : "obd";
}

/* Durdurma işlemleri bloklar (WiFi stop ~11 sn, NimBLE deinit); sıra önemli:
 * önce gereksiz radyo kapanır, sonra gerekenler açılır. NAV ↔ ROLL geçişinde
 * telefon bağlantısı kopmaz. */
static void apply_radios(app_mode_t to, bool boot)
{
    bool want_nav = to != APP_MODE_OBD;
    bool want_obd = to == APP_MODE_OBD ||
                    (to == APP_MODE_ROLL && obd_link_get_type() == OBD_LINK_BLE);

    roll_feed_set_active(to == APP_MODE_ROLL);
    if (!want_obd) {
        obd_link_suspend();   /* açılışta henüz başlamadı: yalnız işaretler */
    }
    if (!want_nav && s_nav_on) {
        nav_service_stop();
        s_nav_on = false;
    }
    nav_service_set_mode(mode_tag(to));
    if (want_nav && !s_nav_on) {
        nav_service_start();
        s_nav_on = true;
    }
    if (want_obd) {
        if (boot) {
            obd_link_start();
        } else {
            obd_link_resume();
        }
    }
}

static void mode_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (s_cur != s_target) {
            app_mode_t to = s_target;
            apply_radios(to, false);
            s_cur = to;
            app_log_info(TAG, "Mode: %s", mode_name(to));
        }
    }
}

void app_mode_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, NVS_KEY_MODE, &v) == ESP_OK && v <= APP_MODE_ROLL) {
            s_target = (app_mode_t)v;
        }
        nvs_close(h);
    }
    s_cur = s_target;
    nav_service_init();
    if (xTaskCreate(mode_task, "app_mode", MODE_TASK_STACK, NULL, MODE_TASK_PRIO,
                    &s_task) != pdPASS) {
        app_log_error(TAG, "Mode task create failed");
    }
    app_log_info(TAG, "Boot mode: %s", mode_name(s_cur));
}

void app_mode_start(void)
{
    apply_radios(s_cur, true);
}

void app_mode_set(app_mode_t mode)
{
    if (mode == s_target) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_MODE, (uint8_t)mode);
        nvs_commit(h);
        nvs_close(h);
    }
    s_target = mode;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

app_mode_t app_mode_get(void)
{
    return s_target;
}

bool app_mode_is_switching(void)
{
    return s_cur != s_target;
}
