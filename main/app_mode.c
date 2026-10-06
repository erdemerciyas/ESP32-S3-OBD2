#include "app_mode.h"
#include "obd_link.h"
#include "nav_service.h"
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

static const char *mode_name(app_mode_t m)
{
    return m == APP_MODE_NAV ? "NAV" : "OBD";
}

/* Durdurma işlemleri bloklar (WiFi stop ~11 sn, NimBLE deinit); sıra önemli:
 * önce eski radyo tamamen kapanır, sonra yenisi açılır. */
static void mode_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (s_cur != s_target) {
            app_mode_t to = s_target;
            if (to == APP_MODE_NAV) {
                obd_link_suspend();
                nav_service_start();
            } else {
                nav_service_stop();
                obd_link_resume();
            }
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
        if (nvs_get_u8(h, NVS_KEY_MODE, &v) == ESP_OK && v <= APP_MODE_NAV) {
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
    if (s_cur == APP_MODE_NAV) {
        obd_link_suspend();   /* henüz başlamadı: yalnız işaretler */
        nav_service_start();
    } else {
        obd_link_start();
    }
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
