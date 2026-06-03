#include "wifi_settings_ui.h"
#include "styles.h"
#include "board_config.h"
#include "connectivity.h"
#include "wifi_manager.h"
#include "telemetry.h"
#include "lvgl_driver.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "app.h"
#include "haptic.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIFI_UI_HEADER_H   52
#define WIFI_UI_FOOTER_H   56
#define WIFI_UI_PAD        16
#define WIFI_UI_STATUS_H   88
#define WIFI_UI_BTN_ROW_Y  (WIFI_UI_HEADER_H + WIFI_UI_STATUS_H + 8)
#define WIFI_UI_LIST_TOP   (WIFI_UI_BTN_ROW_Y + 44)
#define WIFI_UI_LIST_H     (UI_SCREEN_H - WIFI_UI_LIST_TOP - WIFI_UI_FOOTER_H - 8)
#define WIFI_UI_NETWORK_ROW_H 48
#define WIFI_UI_JOB_STACK  16384

static const char *TAG = "wifi_ui";

static lv_obj_t *status_saved_lbl;
static lv_obj_t *status_obd_lbl;
static lv_obj_t *status_msg_lbl;
static lv_obj_t *status_wifi_icon;
static lv_obj_t *network_list;
static lv_obj_t *scan_btn;
static lv_obj_t *auto_btn;

static wifi_ap_info_t scan_results[WIFI_SCAN_MAX_RESULTS];
static int scan_result_count;
static volatile bool worker_busy;
static lv_timer_t *progress_timer;

typedef struct {
    char ssid[32];
    wifi_auth_mode_t authmode;
    bool auto_mode;
    bool scan_only;
    wifi_ap_info_t networks[WIFI_SCAN_MAX_RESULTS];
    int network_count;
    esp_err_t result;
} wifi_ui_job_t;

static lv_obj_t *wifi_ui_btn_content(lv_obj_t *btn, const char *icon_sym, const char *text,
                                     lv_color_t text_color)
{
    lv_obj_t *row = lv_obj_create(btn);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *icon = ui_icon_create(row, icon_sym, UI_FONT_ICON);
    lv_obj_set_style_text_color(icon, text_color, 0);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(lbl, text_color, 0);
    return lbl;
}

void wifi_settings_ui_sync_wifi_ind(ui_wifi_ind_level_t level)
{
    if (status_wifi_icon != NULL) {
        ui_wifi_ind_apply(status_wifi_icon, level);
    }
}

static void wifi_ui_update_status_labels(void)
{
    char saved_buf[96];
    if (g_settings.wifi_ssid[0] != '\0') {
        if (g_settings.obd_adapter_port != 0 && g_settings.obd_adapter_ip[0] != '\0') {
            snprintf(saved_buf, sizeof(saved_buf), "Kayıtlı: %s  %s:%u",
                     g_settings.wifi_ssid,
                     g_settings.obd_adapter_ip,
                     g_settings.obd_adapter_port);
        } else {
            snprintf(saved_buf, sizeof(saved_buf), "Kayıtlı: %s", g_settings.wifi_ssid);
        }
    } else if (!g_settings.wifi_manual_mode) {
        snprintf(saved_buf, sizeof(saved_buf), "Kayıtlı: Otomatik tarama");
    } else {
        snprintf(saved_buf, sizeof(saved_buf), "Kayıtlı: — Tara ile seçin");
    }
    lv_label_set_text(status_saved_lbl, saved_buf);

    switch (connectivity_get_state()) {
        case CONN_STATE_OBD_READY:
            lv_label_set_text(status_obd_lbl, "OBD: Hazır");
            lv_obj_set_style_text_color(status_obd_lbl, color_success, 0);
            break;
        case CONN_STATE_LINK_UP:
        case CONN_STATE_ELM_INIT:
            lv_label_set_text(status_obd_lbl, "OBD: ELM327 başlatılıyor");
            lv_obj_set_style_text_color(status_obd_lbl, color_warning, 0);
            break;
        case CONN_STATE_ERROR:
            lv_label_set_text(status_obd_lbl, "OBD: Hata");
            lv_obj_set_style_text_color(status_obd_lbl, color_danger, 0);
            break;
        default:
            if (wifi_is_ap_connected()) {
                lv_label_set_text(status_obd_lbl, "OBD: WiFi var, TCP yok");
                lv_obj_set_style_text_color(status_obd_lbl, color_warning, 0);
            } else {
                lv_label_set_text(status_obd_lbl, "OBD: Bağlı değil");
                lv_obj_set_style_text_color(status_obd_lbl, color_text_dim, 0);
            }
            break;
    }
}

static void wifi_ui_set_message(const char *text)
{
    lv_label_set_text(status_msg_lbl, text);
}

static void wifi_ui_progress_timer_cb(lv_timer_t *t)
{
    (void)t;

    if (!worker_busy) {
        return;
    }

    switch (connectivity_get_state()) {
        case CONN_STATE_OBD_READY:
            wifi_ui_set_message("OBD hazır — bağlanıldı");
            break;
        case CONN_STATE_LINK_UP:
        case CONN_STATE_ELM_INIT:
            wifi_ui_set_message("WiFi OK — ELM327/TCP aranıyor...");
            break;
        default:
            if (wifi_is_ap_connected()) {
                wifi_ui_set_message("WiFi bağlandı — TCP aranıyor...");
            } else {
                wifi_ui_set_message("WiFi ağına katılınıyor...");
            }
            break;
    }
    wifi_ui_update_status_labels();
}

static void wifi_ui_progress_start(void)
{
    if (progress_timer == NULL) {
        progress_timer = lv_timer_create(wifi_ui_progress_timer_cb, 1000, NULL);
    }
    lv_timer_resume(progress_timer);
}

static void wifi_ui_progress_stop(void)
{
    if (progress_timer != NULL) {
        lv_timer_pause(progress_timer);
    }
}

static const char *wifi_ui_fail_hint(void)
{
    if (wifi_is_ap_connected() && wifi_tcp_ready()) {
        return "TCP var; OBD yok — kontağı açın";
    }
    if (wifi_is_ap_connected()) {
        return "WiFi var, TCP yok: TELEFONU adaptörden ayırın (tek bağlantı)";
    }
    switch (wifi_get_last_fail_stage()) {
        case WIFI_FAIL_TCP:
            return "WiFi var, TCP yok: telefonu ayırın + kontak açık";
        case WIFI_FAIL_ASSOC:
        default:
            return "WiFi'ye katılamadı: telefonu adaptörden ayırıp tekrar deneyin";
    }
}

static void wifi_ui_set_buttons_enabled(bool enabled)
{
    if (enabled) {
        lv_obj_remove_state(scan_btn, LV_STATE_DISABLED);
        lv_obj_remove_state(auto_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
        lv_obj_add_state(auto_btn, LV_STATE_DISABLED);
    }
}

static void wifi_ui_network_click_cb(lv_event_t *e);

static void wifi_ui_populate_network_list(const wifi_ap_info_t *networks, int count)
{
    lv_obj_clean(network_list);
    scan_result_count = count;
    if (count > WIFI_SCAN_MAX_RESULTS) {
        count = WIFI_SCAN_MAX_RESULTS;
    }
    memcpy(scan_results, networks, (size_t)count * sizeof(wifi_ap_info_t));

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(network_list);
        lv_label_set_text(empty, "Ağ bulunamadı");
        lv_obj_set_style_text_color(empty, color_text_dim, 0);
        lv_obj_set_style_text_font(empty, UI_FONT_MD, 0);
        return;
    }

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_btn_create(network_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, WIFI_UI_NETWORK_ROW_H);
        lv_obj_add_style(btn, style_get_btn_secondary(), 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, wifi_ui_network_click_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);

        if (g_settings.wifi_ssid[0] != '\0' &&
            strcmp(g_settings.wifi_ssid, networks[i].ssid) == 0) {
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

        ui_icon_create(row, LV_SYMBOL_WIFI, UI_FONT_ICON);
        char line[64];
        snprintf(line, sizeof(line), "%s   %d dBm", networks[i].ssid, networks[i].rssi);
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, line);
        lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, color_text, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void wifi_ui_job_finished_cb(void *user_data)
{
    wifi_ui_job_t *job = (wifi_ui_job_t *)user_data;
    if (job == NULL) {
        worker_busy = false;
        wifi_ui_set_buttons_enabled(true);
        return;
    }

    if (job->scan_only) {
        wifi_ui_populate_network_list(job->networks, job->network_count);
        wifi_ui_set_message(job->network_count > 0 ?
                            "Ağ seçin ve dokunun" :
                            "Tarama tamam, ağ yok");
    } else if (job->auto_mode) {
        if (job->result == ESP_OK) {
            wifi_ui_set_message(connectivity_get_state() == CONN_STATE_OBD_READY ?
                                "Otomatik bağlantı başarılı" :
                                "WiFi/TCP OK — kontağı açın");
        } else {
            wifi_ui_set_message(wifi_ui_fail_hint());
        }
    } else {
        if (job->result == ESP_OK) {
            wifi_ui_set_message(connectivity_get_state() == CONN_STATE_OBD_READY ?
                                "Bağlandı ve kaydedildi" :
                                "WiFi/TCP OK — kontağı açın");
        } else {
            wifi_ui_set_message(wifi_ui_fail_hint());
        }
    }

    wifi_ui_progress_stop();
    wifi_ui_update_status_labels();
    wifi_ui_set_buttons_enabled(true);
    worker_busy = false;
    free(job);
}

static void wifi_ui_worker_task(void *arg)
{
    wifi_ui_job_t *job = (wifi_ui_job_t *)arg;

    if (job->scan_only) {
        connectivity_wifi_scan(job->networks, WIFI_SCAN_MAX_RESULTS, &job->network_count);
        job->result = (job->network_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
    } else if (job->auto_mode) {
        job->result = connectivity_wifi_enable_auto_mode();
    } else {
        job->result = connectivity_wifi_connect_manual(job->ssid, job->authmode);
    }

    lv_async_call(wifi_ui_job_finished_cb, job);
    vTaskDelete(NULL);
}

static void wifi_ui_start_job(wifi_ui_job_t *job)
{
    if (worker_busy) {
        wifi_ui_set_message("İşlem devam ediyor, bekleyin...");
        free(job);
        return;
    }

    worker_busy = true;
    wifi_ui_set_buttons_enabled(false);

    const bool is_connect_job = !job->scan_only;

    BaseType_t created = xTaskCreate(wifi_ui_worker_task, "wifi_ui_job", WIFI_UI_JOB_STACK, job, 5, NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to start WiFi UI worker task");
        worker_busy = false;
        wifi_ui_set_buttons_enabled(true);
        wifi_ui_set_message("Bağlantı görevi başlatılamadı");
        free(job);
        return;
    }

    if (is_connect_job) {
        wifi_ui_progress_start();
    }
}

static void wifi_ui_network_click_cb(lv_event_t *e)
{
    if (worker_busy) {
        wifi_ui_set_message("İşlem devam ediyor...");
        return;
    }

    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= scan_result_count) {
        ESP_LOGW(TAG, "Network click ignored: index=%d count=%d", index, scan_result_count);
        return;
    }

    wifi_ui_job_t *job = calloc(1, sizeof(wifi_ui_job_t));
    if (job == NULL) {
        wifi_ui_set_message("Bellek yetersiz");
        return;
    }

    strncpy(job->ssid, scan_results[index].ssid, sizeof(job->ssid) - 1);
    job->authmode = scan_results[index].authmode;

    char msg[80];
    if (wifi_is_elm327_ssid(job->ssid)) {
        snprintf(msg, sizeof(msg), "OBD: %s — WiFi deneniyor...", job->ssid);
    } else {
        snprintf(msg, sizeof(msg), "Bağlanıyor: %s...", job->ssid);
    }
    wifi_ui_set_message(msg);
    haptic_click();
    ESP_LOGI(TAG, "User selected network: %s", job->ssid);

    wifi_ui_start_job(job);
}

static void wifi_ui_scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (worker_busy) {
        return;
    }

    wifi_ui_job_t *job = calloc(1, sizeof(wifi_ui_job_t));
    if (job == NULL) {
        return;
    }

    job->scan_only = true;
    wifi_ui_set_message("Taranıyor...");
    wifi_ui_start_job(job);
}

static void wifi_ui_auto_btn_cb(lv_event_t *e)
{
    (void)e;
    if (worker_busy) {
        return;
    }

    wifi_ui_job_t *job = calloc(1, sizeof(wifi_ui_job_t));
    if (job == NULL) {
        return;
    }

    job->auto_mode = true;
    wifi_ui_set_message("Otomatik bağlantı deneniyor...");
    wifi_ui_start_job(job);
}

void wifi_settings_ui_create(lv_obj_t *screen)
{
    const int content_w = UI_SCREEN_W - (WIFI_UI_PAD * 2);

    lv_obj_t *status_card = lv_obj_create(screen);
    lv_obj_set_pos(status_card, WIFI_UI_PAD, WIFI_UI_HEADER_H + 6);
    lv_obj_set_size(status_card, content_w, WIFI_UI_STATUS_H);
    lv_obj_add_style(status_card, style_get_card(), 0);
    lv_obj_remove_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    status_wifi_icon = ui_wifi_ind_create(status_card, -8, 6);
    lv_obj_set_style_text_font(status_wifi_icon, UI_FONT_ICON, 0);

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
    lv_label_set_text(status_msg_lbl, "Tara → WIFI_OBDII seçin (telefonu AP'den ayırın)");

    scan_btn = lv_btn_create(screen);
    lv_obj_set_size(scan_btn, (content_w - 8) / 2, 36);
    lv_obj_set_pos(scan_btn, WIFI_UI_PAD, WIFI_UI_BTN_ROW_Y);
    lv_obj_add_style(scan_btn, style_get_btn_primary(), 0);
    lv_obj_add_event_cb(scan_btn, wifi_ui_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    wifi_ui_btn_content(scan_btn, LV_SYMBOL_REFRESH, "Tara", color_bg_dark);

    auto_btn = lv_btn_create(screen);
    lv_obj_set_size(auto_btn, (content_w - 8) / 2, 36);
    lv_obj_set_pos(auto_btn, WIFI_UI_PAD + (content_w - 8) / 2 + 8, WIFI_UI_BTN_ROW_Y);
    lv_obj_add_style(auto_btn, style_get_btn_secondary(), 0);
    lv_obj_add_event_cb(auto_btn, wifi_ui_auto_btn_cb, LV_EVENT_CLICKED, NULL);
    wifi_ui_btn_content(auto_btn, LV_SYMBOL_WIFI, "Otomatik", color_primary);

    network_list = lv_obj_create(screen);
    lv_obj_set_pos(network_list, WIFI_UI_PAD, WIFI_UI_LIST_TOP);
    lv_obj_set_size(network_list, content_w, WIFI_UI_LIST_H);
    lv_obj_add_style(network_list, style_get_card(), 0);
    lv_obj_set_flex_flow(network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(network_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(network_list, 6, 0);
    lv_obj_set_style_pad_all(network_list, 8, 0);
    lv_obj_add_flag(network_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(network_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(network_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    wifi_settings_ui_refresh();
}

void wifi_settings_ui_refresh(void)
{
    if (status_saved_lbl == NULL) {
        return;
    }

    if (!lvgl_lock(0)) {
        return;
    }

    telemetry_snapshot_t snap;
    telemetry_get_snapshot(&snap);
    wifi_settings_ui_sync_wifi_ind(
        ui_wifi_ind_level_from(snap.wifi_ap_up, snap.wifi_tcp_up, snap.conn_state));
    wifi_ui_update_status_labels();
    lvgl_unlock();
}
