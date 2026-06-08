#include "bt_settings_ui.h"
#include "styles.h"
#include "board_config.h"
#include "connectivity.h"
#include "bt_manager.h"
#include "bt_elm327_profiles.h"
#include "telemetry.h"
#include "lvgl_driver.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "app.h"
#include "conn_log.h"
#include <string.h>
#include "haptic.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BT_UI_HEADER_H   52
#define BT_UI_FOOTER_H   56
#define BT_UI_PAD        16
#define BT_UI_STATUS_H   88
#define BT_UI_BTN_ROW_Y  (BT_UI_HEADER_H + BT_UI_STATUS_H + 8)
#define BT_UI_BTN_ROW2_Y (BT_UI_BTN_ROW_Y + 40)
#define BT_UI_LIST_TOP   (BT_UI_BTN_ROW2_Y + 44)
#define BT_UI_LIST_H     (UI_SCREEN_H - BT_UI_LIST_TOP - BT_UI_FOOTER_H - 8)
#define BT_UI_DEVICE_ROW_H 52
#define BT_UI_JOB_STACK      12288
#define BT_UI_LIST_MAX_ROWS  32

static const char *TAG = "bt_ui";

static lv_obj_t *status_saved_lbl;
static lv_obj_t *status_obd_lbl;
static lv_obj_t *status_msg_lbl;
static lv_obj_t *status_bt_icon;
static lv_obj_t *device_list;
static lv_obj_t *scan_btn;
static lv_obj_t *auto_btn;
static lv_obj_t *forget_btn;
static lv_obj_t *disconnect_btn;

static bt_device_info_t scan_results[BT_SCAN_MAX_RESULTS];
static int scan_result_count;
static volatile bool worker_busy;
static lv_timer_t *progress_timer;
static lv_timer_t *deferred_scan_timer;

typedef enum {
    BT_UI_JOB_SCAN = 0,
    BT_UI_JOB_AUTO,
    BT_UI_JOB_CONNECT,
    BT_UI_JOB_FORGET,
    BT_UI_JOB_DISCONNECT,
} bt_ui_job_kind_t;

typedef struct {
    bt_ui_job_kind_t kind;
    char addr[BT_ADDR_STR_LEN];
    char name[BT_DEVICE_NAME_MAX];
    uint8_t addr_type;
    int device_count;
    esp_err_t result;
} bt_ui_job_t;

static bt_device_info_t g_bt_scan_work_buf[BT_SCAN_MAX_RESULTS];

static lv_obj_t *bt_ui_btn_content(lv_obj_t *btn, const char *icon_sym, const char *text,
                                   lv_color_t text_color)
{
    lv_obj_t *row = lv_obj_create(btn);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    if (icon_sym != NULL && icon_sym[0] != '\0') {
        lv_obj_t *icon = ui_icon_create(row, icon_sym, UI_FONT_ICON);
        lv_obj_set_style_text_color(icon, text_color, 0);
    }
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(lbl, text_color, 0);
    return lbl;
}

void bt_settings_ui_sync_conn_ind(ui_conn_ind_level_t level)
{
    if (status_bt_icon != NULL) {
        ui_conn_ind_apply(status_bt_icon, level);
    }
}

static void bt_ui_update_status_labels(void)
{
    char saved_buf[96];
    if (g_settings.bt_device_addr[0] != '\0') {
        if (g_settings.bt_device_name[0] != '\0') {
            snprintf(saved_buf, sizeof(saved_buf), "Saved: %s  %s",
                     g_settings.bt_device_name, g_settings.bt_device_addr);
        } else {
            snprintf(saved_buf, sizeof(saved_buf), "Saved: %s", g_settings.bt_device_addr);
        }
    } else if (!g_settings.bt_manual_mode) {
        snprintf(saved_buf, sizeof(saved_buf), "Saved: Auto scan");
    } else {
        snprintf(saved_buf, sizeof(saved_buf), "Saved: Scan to select");
    }
    lv_label_set_text(status_saved_lbl, saved_buf);

    switch (connectivity_get_state()) {
        case CONN_STATE_OBD_READY:
            lv_label_set_text(status_obd_lbl, "OBD: Ready - connected");
            lv_obj_set_style_text_color(status_obd_lbl, color_success, 0);
            break;
        case CONN_STATE_LINK_UP:
        case CONN_STATE_ELM_INIT:
            lv_label_set_text(status_obd_lbl, "OBD: ELM327 initializing");
            lv_obj_set_style_text_color(status_obd_lbl, color_warning, 0);
            break;
        case CONN_STATE_ERROR:
            lv_label_set_text(status_obd_lbl, "OBD: Error");
            lv_obj_set_style_text_color(status_obd_lbl, color_danger, 0);
            break;
        default:
            if (bt_serial_ready()) {
                lv_label_set_text(status_obd_lbl, "OBD: BT OK, no ECU");
                lv_obj_set_style_text_color(status_obd_lbl, color_warning, 0);
            } else {
                lv_label_set_text(status_obd_lbl, "OBD: Not connected");
                lv_obj_set_style_text_color(status_obd_lbl, color_text_dim, 0);
            }
            break;
    }
}

static void bt_ui_set_message(const char *text)
{
    lv_label_set_text(status_msg_lbl, text);
}

static void bt_ui_progress_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!worker_busy) {
        return;
    }

    switch (connectivity_get_state()) {
        case CONN_STATE_OBD_READY:
            bt_ui_set_message("OBD ready - connected");
            break;
        case CONN_STATE_LINK_UP:
        case CONN_STATE_ELM_INIT:
            bt_ui_set_message("BT OK - ELM327 initializing...");
            break;
        default:
            if (bt_serial_ready()) {
                bt_ui_set_message("Bluetooth connected - waiting for OBD...");
            } else {
                bt_ui_set_message("Connecting to adapter...");
            }
            break;
    }
    bt_ui_update_status_labels();
}

static void bt_ui_progress_start(void)
{
    if (progress_timer == NULL) {
        progress_timer = lv_timer_create(bt_ui_progress_timer_cb, 1000, NULL);
    }
    lv_timer_resume(progress_timer);
}

static void bt_ui_progress_stop(void)
{
    if (progress_timer != NULL) {
        lv_timer_pause(progress_timer);
    }
}

static void bt_ui_set_buttons_enabled(bool enabled)
{
    lv_obj_t *buttons[] = { scan_btn, auto_btn, forget_btn, disconnect_btn };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (buttons[i] == NULL) {
            continue;
        }
        if (enabled) {
            lv_obj_remove_state(buttons[i], LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
        }
    }
}

static void bt_ui_device_click_cb(lv_event_t *e);
static void bt_ui_trigger_scan(const char *status_msg);

static void bt_ui_populate_device_list(const bt_device_info_t *devices, int count)
{
    lv_obj_clean(device_list);
    scan_result_count = count;
    if (count > BT_SCAN_MAX_RESULTS) {
        count = BT_SCAN_MAX_RESULTS;
    }
    memcpy(scan_results, devices, (size_t)count * sizeof(bt_device_info_t));

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(device_list);
        lv_label_set_text(empty, "No Bluetooth devices found");
        lv_obj_set_style_text_color(empty, color_text_dim, 0);
        lv_obj_set_style_text_font(empty, UI_FONT_MD, 0);
        return;
    }

    int show = count;
    if (show > BT_UI_LIST_MAX_ROWS) {
        show = BT_UI_LIST_MAX_ROWS;
    }

    for (int i = 0; i < show; i++) {
        lv_obj_t *btn = lv_btn_create(device_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, BT_UI_DEVICE_ROW_H);
        lv_obj_add_style(btn, style_get_btn_secondary(), 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, bt_ui_device_click_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);

        if (g_settings.bt_device_addr[0] != '\0' &&
            strcmp(g_settings.bt_device_addr, devices[i].addr) == 0) {
            lv_obj_set_style_border_color(btn, color_accent, 0);
            lv_obj_set_style_border_width(btn, 2, 0);
        }

        lv_obj_t *row = lv_obj_create(btn);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

        ui_icon_create(row, LV_SYMBOL_BLUETOOTH, UI_FONT_ICON);
        char line[96];
        const char *label = devices[i].name[0] ? devices[i].name : "Unknown";
        if (devices[i].is_obd_hint) {
            snprintf(line, sizeof(line), "[OBD] %s  %s  %d dBm",
                     label, devices[i].addr, devices[i].rssi);
        } else {
            snprintf(line, sizeof(line), "%s  %s  %d dBm",
                     label, devices[i].addr, devices[i].rssi);
        }
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, line);
        lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, devices[i].is_obd_hint ? color_accent : color_text, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void bt_ui_job_finished_cb(void *user_data)
{
    bt_ui_job_t *job = (bt_ui_job_t *)user_data;
    if (job == NULL) {
        worker_busy = false;
        bt_ui_set_buttons_enabled(true);
        return;
    }

    switch (job->kind) {
        case BT_UI_JOB_SCAN: {
            bt_ui_populate_device_list(g_bt_scan_work_buf, job->device_count);
            char scan_msg[72];
            if (job->device_count > 0) {
                if (job->device_count > BT_UI_LIST_MAX_ROWS) {
                    snprintf(scan_msg, sizeof(scan_msg),
                             "%d found, showing strongest %d — tap to connect",
                             job->device_count, BT_UI_LIST_MAX_ROWS);
                } else {
                    snprintf(scan_msg, sizeof(scan_msg),
                             "%d BLE device(s) — tap to connect",
                             job->device_count);
                }
            } else {
                const char *err = bt_get_last_error();
                if (err != NULL) {
                    snprintf(scan_msg, sizeof(scan_msg), "Scan failed: %s", err);
                } else if (!bt_stack_is_ready()) {
                    snprintf(scan_msg, sizeof(scan_msg), "BLE stack not ready — retry Scan");
                } else {
                    snprintf(scan_msg, sizeof(scan_msg), "No BLE devices nearby");
                }
                const int log_n = conn_log_count();
                if (log_n > 0) {
                    uint32_t up_s = 0;
                    uint32_t seq = 0;
                    const char *last = conn_log_entry(log_n - 1, &up_s, &seq);
                    if (last != NULL && strstr(last, "BLE scan") != NULL) {
                        snprintf(scan_msg, sizeof(scan_msg), "%s", last);
                    }
                }
            }
            bt_ui_set_message(scan_msg);
            break;
        }
        case BT_UI_JOB_AUTO:
            if (job->result == ESP_OK) {
                bt_ui_set_message(connectivity_get_state() == CONN_STATE_OBD_READY ?
                                  "Auto-connect successful" :
                                  "BT OK - turn ignition ON");
            } else {
                bt_ui_set_message(bt_get_last_fail_hint());
            }
            break;
        case BT_UI_JOB_CONNECT:
            if (job->result == ESP_OK) {
                bt_ui_set_message(connectivity_get_state() == CONN_STATE_OBD_READY ?
                                  "Connected and saved" :
                                  "BT OK - turn ignition ON");
            } else {
                bt_ui_set_message(bt_get_last_fail_hint());
            }
            break;
        case BT_UI_JOB_FORGET:
            bt_ui_populate_device_list(NULL, 0);
            bt_ui_set_message("Saved adapter forgotten");
            break;
        case BT_UI_JOB_DISCONNECT:
            bt_ui_set_message("Disconnected");
            break;
        default:
            break;
    }

    bt_ui_progress_stop();
    bt_ui_update_status_labels();
    bt_ui_set_buttons_enabled(true);
    worker_busy = false;
    free(job);
}

static void bt_ui_worker_task(void *arg)
{
    bt_ui_job_t *job = (bt_ui_job_t *)arg;

    switch (job->kind) {
        case BT_UI_JOB_SCAN:
            job->device_count = 0;
            job->result = connectivity_bt_scan(g_bt_scan_work_buf, BT_SCAN_MAX_RESULTS,
                                               &job->device_count);
            if (job->result == ESP_OK || job->device_count > 0) {
                job->result = ESP_OK;
            }
            break;
        case BT_UI_JOB_AUTO:
            job->result = connectivity_bt_enable_auto_mode();
            break;
        case BT_UI_JOB_CONNECT:
            job->result = connectivity_bt_connect_manual(job->addr, job->name, job->addr_type);
            break;
        case BT_UI_JOB_FORGET:
            job->result = connectivity_bt_forget();
            break;
        case BT_UI_JOB_DISCONNECT:
            job->result = connectivity_bt_disconnect();
            break;
        default:
            job->result = ESP_ERR_INVALID_ARG;
            break;
    }

    lv_async_call(bt_ui_job_finished_cb, job);
    vTaskDelete(NULL);
}

static void bt_ui_start_job(bt_ui_job_t *job, bool show_progress)
{
    if (worker_busy) {
        bt_ui_set_message("Busy, please wait...");
        free(job);
        return;
    }

    worker_busy = true;
    bt_ui_set_buttons_enabled(false);

    /* Internal RAM stack only — PSRAM stack + BLE wait can crash/cache-fault */
    BaseType_t created = xTaskCreate(bt_ui_worker_task, "bt_ui_job", BT_UI_JOB_STACK, job, 5, NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to start BT UI worker task");
        worker_busy = false;
        bt_ui_set_buttons_enabled(true);
        bt_ui_set_message("Failed to start connection task");
        free(job);
        return;
    }

    if (show_progress) {
        bt_ui_progress_start();
    }
}

static void bt_ui_device_click_cb(lv_event_t *e)
{
    if (worker_busy) {
        bt_ui_set_message("Busy...");
        return;
    }

    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= scan_result_count) {
        return;
    }

    bt_ui_job_t *job = calloc(1, sizeof(bt_ui_job_t));
    if (job == NULL) {
        bt_ui_set_message("Out of memory");
        return;
    }

    job->kind = BT_UI_JOB_CONNECT;
    strncpy(job->addr, scan_results[index].addr, sizeof(job->addr) - 1);
    strncpy(job->name, scan_results[index].name, sizeof(job->name) - 1);
    job->addr_type = scan_results[index].addr_type;

    char msg[96];
    snprintf(msg, sizeof(msg), "Connecting: %s...", scan_results[index].name);
    bt_ui_set_message(msg);
    haptic_click();
    bt_ui_start_job(job, true);
}

static void bt_ui_deferred_scan_cb(lv_timer_t *t)
{
    (void)t;
    if (deferred_scan_timer != NULL) {
        lv_timer_pause(deferred_scan_timer);
    }
    if (!bt_rf_allowed()) {
        bt_ui_set_message("UI not ready — tap Scan");
        return;
    }
    bt_ui_trigger_scan("Scanning all nearby BLE devices...");
}

void bt_settings_ui_on_screen_shown(void)
{
    if (device_list == NULL || worker_busy) {
        return;
    }

    if (deferred_scan_timer == NULL) {
        deferred_scan_timer = lv_timer_create(bt_ui_deferred_scan_cb, 1200, NULL);
        lv_timer_set_repeat_count(deferred_scan_timer, 1);
    } else {
        lv_timer_reset(deferred_scan_timer);
        lv_timer_resume(deferred_scan_timer);
    }
}

static void bt_ui_trigger_scan(const char *status_msg)
{
    if (worker_busy) {
        return;
    }

    bt_ui_job_t *job = calloc(1, sizeof(bt_ui_job_t));
    if (job == NULL) {
        bt_ui_set_message("Out of memory");
        return;
    }

    job->kind = BT_UI_JOB_SCAN;
    bt_ui_set_message(status_msg != NULL ? status_msg : "Scanning all nearby BLE (20s)...");
    bt_ui_start_job(job, false);
}

static void bt_ui_scan_btn_cb(lv_event_t *e)
{
    (void)e;
    bt_ui_trigger_scan("Scanning all nearby BLE (20s)...");
}

static void bt_ui_auto_btn_cb(lv_event_t *e)
{
    (void)e;
    if (worker_busy) {
        return;
    }

    bt_ui_job_t *job = calloc(1, sizeof(bt_ui_job_t));
    if (job == NULL) {
        return;
    }

    job->kind = BT_UI_JOB_AUTO;
    bt_ui_set_message("Trying auto-connect...");
    bt_ui_start_job(job, true);
}

static void bt_ui_forget_btn_cb(lv_event_t *e)
{
    (void)e;
    if (worker_busy) {
        return;
    }

    bt_ui_job_t *job = calloc(1, sizeof(bt_ui_job_t));
    if (job == NULL) {
        return;
    }

    job->kind = BT_UI_JOB_FORGET;
    bt_ui_set_message("Clearing saved device...");
    bt_ui_start_job(job, false);
}

static void bt_ui_disconnect_btn_cb(lv_event_t *e)
{
    (void)e;
    if (worker_busy) {
        return;
    }

    bt_ui_job_t *job = calloc(1, sizeof(bt_ui_job_t));
    if (job == NULL) {
        return;
    }

    job->kind = BT_UI_JOB_DISCONNECT;
    bt_ui_set_message("Disconnecting...");
    bt_ui_start_job(job, false);
}

void bt_settings_ui_create(lv_obj_t *screen)
{
    const int content_w = UI_SCREEN_W - (BT_UI_PAD * 2);
    const int btn_w = (content_w - 8) / 2;

    lv_obj_t *status_card = lv_obj_create(screen);
    lv_obj_set_pos(status_card, BT_UI_PAD, BT_UI_HEADER_H + 6);
    lv_obj_set_size(status_card, content_w, BT_UI_STATUS_H);
    lv_obj_add_style(status_card, style_get_card(), 0);
    lv_obj_remove_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    status_bt_icon = ui_conn_ind_create(status_card, -8, 6);

    status_saved_lbl = lv_label_create(status_card);
    lv_obj_set_pos(status_saved_lbl, 12, 10);
    lv_obj_set_width(status_saved_lbl, content_w - 48);
    lv_obj_set_style_text_font(status_saved_lbl, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(status_saved_lbl, color_text, 0);
    lv_label_set_long_mode(status_saved_lbl, LV_LABEL_LONG_DOT);

    status_obd_lbl = lv_label_create(status_card);
    lv_obj_set_pos(status_obd_lbl, 12, 32);
    lv_obj_set_style_text_font(status_obd_lbl, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(status_obd_lbl, color_text_dim, 0);

    status_msg_lbl = lv_label_create(status_card);
    lv_obj_set_pos(status_msg_lbl, 12, 54);
    lv_obj_set_width(status_msg_lbl, content_w - 24);
    lv_obj_set_style_text_font(status_msg_lbl, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(status_msg_lbl, color_accent, 0);
    lv_label_set_long_mode(status_msg_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(status_msg_lbl, "Lists every nearby BLE device (scroll down)");

    scan_btn = lv_btn_create(screen);
    lv_obj_set_size(scan_btn, btn_w, 34);
    lv_obj_set_pos(scan_btn, BT_UI_PAD, BT_UI_BTN_ROW_Y);
    lv_obj_add_style(scan_btn, style_get_btn_primary(), 0);
    lv_obj_add_event_cb(scan_btn, bt_ui_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    bt_ui_btn_content(scan_btn, LV_SYMBOL_REFRESH, "Scan", color_bg_dark);

    auto_btn = lv_btn_create(screen);
    lv_obj_set_size(auto_btn, btn_w, 34);
    lv_obj_set_pos(auto_btn, BT_UI_PAD + btn_w + 8, BT_UI_BTN_ROW_Y);
    lv_obj_add_style(auto_btn, style_get_btn_secondary(), 0);
    lv_obj_add_event_cb(auto_btn, bt_ui_auto_btn_cb, LV_EVENT_CLICKED, NULL);
    bt_ui_btn_content(auto_btn, LV_SYMBOL_BLUETOOTH, "Auto", color_primary);

    forget_btn = lv_btn_create(screen);
    lv_obj_set_size(forget_btn, btn_w, 34);
    lv_obj_set_pos(forget_btn, BT_UI_PAD, BT_UI_BTN_ROW2_Y);
    lv_obj_add_style(forget_btn, style_get_btn_secondary(), 0);
    lv_obj_add_event_cb(forget_btn, bt_ui_forget_btn_cb, LV_EVENT_CLICKED, NULL);
    bt_ui_btn_content(forget_btn, LV_SYMBOL_TRASH, "Forget", color_text);

    disconnect_btn = lv_btn_create(screen);
    lv_obj_set_size(disconnect_btn, btn_w, 34);
    lv_obj_set_pos(disconnect_btn, BT_UI_PAD + btn_w + 8, BT_UI_BTN_ROW2_Y);
    lv_obj_add_style(disconnect_btn, style_get_btn_secondary(), 0);
    lv_obj_add_event_cb(disconnect_btn, bt_ui_disconnect_btn_cb, LV_EVENT_CLICKED, NULL);
    bt_ui_btn_content(disconnect_btn, LV_SYMBOL_CLOSE, "Disconnect", color_text);

    device_list = lv_obj_create(screen);
    lv_obj_set_pos(device_list, BT_UI_PAD, BT_UI_LIST_TOP);
    lv_obj_set_size(device_list, content_w, BT_UI_LIST_H);
    lv_obj_add_style(device_list, style_get_card(), 0);
    lv_obj_set_flex_flow(device_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(device_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(device_list, 6, 0);
    lv_obj_set_style_pad_all(device_list, 8, 0);
    lv_obj_add_flag(device_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(device_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(device_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bt_settings_ui_refresh();
}

void bt_settings_ui_refresh(void)
{
    if (status_saved_lbl == NULL) {
        return;
    }

    if (!lvgl_lock(0)) {
        return;
    }

    telemetry_snapshot_t snap;
    telemetry_get_snapshot(&snap);
    bt_settings_ui_sync_conn_ind(
        ui_conn_ind_level_from(snap.bt_linked, snap.bt_serial_up, snap.conn_state));
    bt_ui_update_status_labels();
    lvgl_unlock();
}
