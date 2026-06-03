#include "dashboard.h"
#include "gauge.h"
#include "styles.h"
#include "board_config.h"
#include "display.h"
#include "telemetry.h"
#include "haptic.h"
#include "connectivity.h"
#include "wifi_settings_ui.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "lvgl_driver.h"
#include "app.h"
#include "settings.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define UI_HEADER_H      52
#define UI_FOOTER_H      56
#define UI_PAD           16
#define UI_MENU_GAP      12
#define UI_MENU_CARD_W   ((UI_SCREEN_W - (UI_PAD * 2) - UI_MENU_GAP) / 2)
#define UI_MENU_TOP      UI_PAD
#define UI_MENU_CARD_H   ((UI_SCREEN_H - UI_MENU_TOP - UI_FOOTER_H - UI_PAD - UI_MENU_GAP) / 2)

static const char *TAG = "dashboard";

static lv_obj_t *screen_dashboard;
static lv_obj_t *touch_overlay;
static lv_obj_t *screen_menu;
static lv_obj_t *screen_settings;
static lv_obj_t *screen_connection;
static lv_obj_t *screen_about;

static lv_point_t touch_press_pt;
static uint32_t last_tap_tick;
static uint32_t last_swipe_tick;
static bool touch_tracking;
static bool long_press_fired;
static lv_timer_t *long_press_timer;
static lv_obj_t *default_gauge_toast;
static lv_timer_t *toast_hide_timer;
static bool dashboard_first_show = true;
static lv_obj_t *conn_wifi_icon;
static lv_obj_t *menu_wifi_icon;
static lv_obj_t *dtc_banner;

extern app_settings_t g_settings;

static void settings_persist_from_ui(void)
{
    app_settings_save();
}

static void cancel_long_press_timer(void)
{
    if (long_press_timer != NULL) {
        lv_timer_delete(long_press_timer);
        long_press_timer = NULL;
    }
}

static void hide_default_gauge_toast_cb(lv_timer_t *timer)
{
    (void)timer;
    toast_hide_timer = NULL;
    if (default_gauge_toast != NULL) {
        lv_obj_add_flag(default_gauge_toast, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_default_gauge_saved_toast(void)
{
    if (default_gauge_toast == NULL) {
        return;
    }

    lv_label_set_text(default_gauge_toast, "Varsayılan açılış kaydedildi");
    lv_obj_remove_flag(default_gauge_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(default_gauge_toast);
    if (touch_overlay != NULL) {
        lv_obj_move_foreground(touch_overlay);
    }

    if (toast_hide_timer != NULL) {
        lv_timer_delete(toast_hide_timer);
    }
    toast_hide_timer = lv_timer_create(hide_default_gauge_toast_cb, 1800, NULL);
    lv_timer_set_repeat_count(toast_hide_timer, 1);
}

static void save_default_startup_gauge(void)
{
    const gauge_type_t active = gauge_get_active();
    if (active >= GAUGE_MAX) {
        return;
    }

    g_settings.default_gauge = (uint8_t)active;
    settings_persist_from_ui();
    ESP_LOGI(TAG, "Default startup gauge saved: %d", active);
    show_default_gauge_saved_toast();
}

static void long_press_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    long_press_timer = NULL;

    if (!touch_tracking || lv_scr_act() != screen_dashboard) {
        return;
    }

    long_press_fired = true;
    save_default_startup_gauge();
}

static void brightness_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    g_settings.brightness = (uint8_t)lv_slider_get_value(slider);
    display_set_brightness(g_settings.brightness);
    settings_persist_from_ui();
}

static void haptic_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    g_settings.haptic_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_persist_from_ui();
}

static void sound_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    g_settings.sound_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_persist_from_ui();
}

static void theme_cycle_cb(lv_event_t *e)
{
    (void)e;
    g_settings.theme = (theme_mode_t)(((uint8_t)g_settings.theme + 1) % 3);
    styles_init((uint8_t)g_settings.theme);
    settings_persist_from_ui();
}
static void ui_subscreen_prepare(lv_obj_t *scr);
static void create_dashboard_screen(void);
static void create_menu_screen(void);
static void create_settings_screen(void);
static void create_connection_screen(void);
static void create_about_screen(void);
static void ui_screen_prepare(lv_obj_t *scr)
{
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_style(scr, style_get_bg_dark(), 0);
    lv_obj_set_style_bg_color(scr, color_bg_dark, 0);
}

static void ui_subscreen_prepare(lv_obj_t *scr)
{
    ui_screen_prepare(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void dashboard_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &touch_press_pt);
        touch_tracking = true;
        long_press_fired = false;
        cancel_long_press_timer();
        long_press_timer = lv_timer_create(long_press_timer_cb, UI_LONG_PRESS_MS, NULL);
        lv_timer_set_repeat_count(long_press_timer, 1);
        return;
    }

    if (code != LV_EVENT_RELEASED || !touch_tracking) {
        return;
    }
    touch_tracking = false;
    cancel_long_press_timer();

    if (long_press_fired) {
        long_press_fired = false;
        last_tap_tick = 0;
        return;
    }

    lv_point_t release_pt;
    lv_indev_get_point(indev, &release_pt);
    const int16_t diff = (int16_t)(touch_press_pt.x - release_pt.x);
    const int16_t dy = (int16_t)(touch_press_pt.y - release_pt.y);
    const uint32_t now = lv_tick_get();

    if (LV_ABS(diff) < LV_ABS(dy) * 2) {
        goto check_double_tap;
    }
    if (gauge_is_transitioning() || (now - last_swipe_tick < UI_SWIPE_COOLDOWN_MS)) {
        return;
    }

    if (diff > UI_SWIPE_THRESHOLD_PX) {
        last_swipe_tick = now;
        haptic_click();
        dashboard_navigate_gauge_next();
        return;
    }
    if (diff < -UI_SWIPE_THRESHOLD_PX) {
        last_swipe_tick = now;
        haptic_click();
        dashboard_navigate_gauge_prev();
        return;
    }

check_double_tap:

    if (last_tap_tick != 0 && (now - last_tap_tick) < UI_DOUBLE_TAP_MS) {
        dashboard_show_screen(DASHBOARD_MENU);
        last_tap_tick = 0;
    } else {
        last_tap_tick = now;
    }
}

static void menu_card_click_cb(lv_event_t *e)
{
    const dashboard_screen_t target = (dashboard_screen_t)(intptr_t)lv_event_get_user_data(e);
    dashboard_show_screen(target);
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    dashboard_show_screen(DASHBOARD_MAIN);
}

lv_obj_t *dashboard_get_main_screen(void)
{
    return screen_dashboard;
}

void dashboard_finish_boot_screen(void)
{
    dashboard_first_show = false;
}

void dashboard_init(void)
{
    ESP_LOGI(TAG, "Initializing dashboard UI (%dx%d)", UI_SCREEN_W, UI_SCREEN_H);

    create_dashboard_screen();
    create_menu_screen();
    create_settings_screen();
    create_connection_screen();
    create_about_screen();

    ESP_LOGI(TAG, "Dashboard screens created (boot splash will show main)");
}

void dashboard_show_screen(dashboard_screen_t screen)
{
    lv_obj_t *target = NULL;
    switch (screen) {
        case DASHBOARD_MAIN: target = screen_dashboard; break;
        case DASHBOARD_MENU: target = screen_menu; break;
        case DASHBOARD_SETTINGS: target = screen_settings; break;
        case DASHBOARD_CONNECTION: target = screen_connection; break;
        case DASHBOARD_ABOUT: target = screen_about; break;
        default: return;
    }

    if (screen == DASHBOARD_CONNECTION) {
        wifi_settings_ui_refresh();
    }

    if (dashboard_first_show) {
        dashboard_first_show = false;
        lv_scr_load(target);
        return;
    }

    if (screen == DASHBOARD_MAIN) {
        lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 220, 0, false);
    } else {
        lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_MOVE_LEFT, 240, 0, false);
    }
}

static void dashboard_update_wifi_indicators(const telemetry_snapshot_t *snap)
{
    const ui_wifi_ind_level_t level = ui_wifi_ind_level_from(
        snap->wifi_ap_up, snap->wifi_tcp_up, snap->conn_state);

    if (conn_wifi_icon != NULL) {
        ui_wifi_ind_apply(conn_wifi_icon, level);
    }
    if (menu_wifi_icon != NULL) {
        ui_wifi_ind_apply(menu_wifi_icon, level);
    }
    wifi_settings_ui_sync_wifi_ind(level);
}

static void dashboard_update_hud(const telemetry_snapshot_t *snap)
{
    dashboard_update_wifi_indicators(snap);

    if (dtc_banner != NULL) {
        if (snap->obd.dtc_present && snap->obd.dtc_count > 0) {
            char dtc_buf[64] = "DTC: ";
            size_t pos = strlen(dtc_buf);
            for (size_t i = 0; i < snap->obd.dtc_count && i < OBD_DTC_MAX_CODES; i++) {
                if (pos + 6 >= sizeof(dtc_buf)) {
                    break;
                }
                pos += (size_t)snprintf(dtc_buf + pos, sizeof(dtc_buf) - pos, "%s ", snap->obd.dtc_codes[i]);
            }
            lv_label_set_text(dtc_banner, dtc_buf);
            lv_obj_remove_flag(dtc_banner, LV_OBJ_FLAG_HIDDEN);
            if (g_settings.haptic_enabled && gauge_get_active() == GAUGE_DTC_WARNING) {
                static bool dtc_alerted;
                if (!dtc_alerted) {
                    haptic_alert();
                    dtc_alerted = true;
                }
            }
        } else {
            lv_obj_add_flag(dtc_banner, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void display_update_gauges(void)
{
    if (!display_live_updates_enabled()) {
        return;
    }

    if (!lvgl_lock(0)) {
        return;
    }

    gauge_sync_availability();

    telemetry_snapshot_t snap;
    telemetry_get_snapshot(&snap);
    dashboard_update_hud(&snap);

    const obd_data_t *data = &snap.obd;
    const bool live = (snap.conn_state == CONN_STATE_OBD_READY) && data->session_valid;

    gauge_set_value_valid(GAUGE_RPM, live && data->rpm_valid && gauge_is_available(GAUGE_RPM));
    if (live && data->rpm_valid && gauge_is_available(GAUGE_RPM)) {
        gauge_update_fullscreen(GAUGE_RPM, (int16_t)data->rpm);
    }

    gauge_set_value_valid(GAUGE_SPEED, live && data->speed_valid && gauge_is_available(GAUGE_SPEED));
    if (live && data->speed_valid && gauge_is_available(GAUGE_SPEED)) {
        gauge_update_fullscreen(GAUGE_SPEED, (int16_t)data->speed);
    }

    gauge_set_value_valid(GAUGE_COOLANT, live && data->coolant_valid && gauge_is_available(GAUGE_COOLANT));
    if (live && data->coolant_valid && gauge_is_available(GAUGE_COOLANT)) {
        gauge_update_fullscreen(GAUGE_COOLANT, (int16_t)data->coolant_temp);
    }

    gauge_set_value_valid(GAUGE_THROTTLE, live && data->throttle_valid && gauge_is_available(GAUGE_THROTTLE));
    if (live && data->throttle_valid && gauge_is_available(GAUGE_THROTTLE)) {
        gauge_update_fullscreen(GAUGE_THROTTLE, (int16_t)data->throttle_pos);
    }

    gauge_set_value_valid(GAUGE_FUEL, live && data->fuel_valid && gauge_is_available(GAUGE_FUEL));
    if (live && data->fuel_valid && gauge_is_available(GAUGE_FUEL)) {
        gauge_update_fullscreen(GAUGE_FUEL, (int16_t)data->fuel_level);
    }

    gauge_set_value_valid(GAUGE_LOAD, live && data->load_valid && gauge_is_available(GAUGE_LOAD));
    if (live && data->load_valid && gauge_is_available(GAUGE_LOAD)) {
        gauge_update_fullscreen(GAUGE_LOAD, (int16_t)data->engine_load);
    }

    gauge_set_value_valid(GAUGE_VOLTAGE, live && data->battery_valid && gauge_is_available(GAUGE_VOLTAGE));
    if (live && data->battery_valid && gauge_is_available(GAUGE_VOLTAGE)) {
        gauge_update_fullscreen(GAUGE_VOLTAGE, (int16_t)(data->battery_voltage * 10.0f));
    }

    gauge_set_value_valid(GAUGE_INTAKE, live && data->intake_valid && gauge_is_available(GAUGE_INTAKE));
    if (live && data->intake_valid && gauge_is_available(GAUGE_INTAKE)) {
        gauge_update_fullscreen(GAUGE_INTAKE, (int16_t)data->intake_temp);
    }

    gauge_set_value_valid(GAUGE_FUEL_CONSUMPTION,
                          live && data->fuel_consumption_valid && gauge_is_available(GAUGE_FUEL_CONSUMPTION));
    if (live && data->fuel_consumption_valid && gauge_is_available(GAUGE_FUEL_CONSUMPTION)) {
        gauge_update_fullscreen(GAUGE_FUEL_CONSUMPTION, (int16_t)(data->fuel_consumption * 10));
    }

    gauge_set_value_valid(GAUGE_DTC_WARNING, live && data->dtc_present && gauge_is_available(GAUGE_DTC_WARNING));
    if (gauge_is_available(GAUGE_DTC_WARNING)) {
        gauge_update_fullscreen(GAUGE_DTC_WARNING, (int16_t)data->dtc_count);
    }

    if (!gauge_is_available(gauge_get_active())) {
        gauge_set_active(gauge_first_available());
        gauge_update_indicator_row(gauge_get_active());
    }

    if (lv_scr_act() == screen_dashboard && !gauge_is_transitioning()) {
        gauge_tick();
    }

    lvgl_unlock();
}

void dashboard_navigate_gauge_next(void)
{
    gauge_set_active(gauge_next_available(gauge_get_active()));
}

void dashboard_navigate_gauge_prev(void)
{
    gauge_set_active(gauge_prev_available(gauge_get_active()));
}

static void create_touch_overlay(lv_obj_t *parent)
{
    touch_overlay = lv_obj_create(parent);
    lv_obj_set_size(touch_overlay, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(touch_overlay, 0, 0);
    lv_obj_set_style_bg_opa(touch_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_overlay, 0, 0);
    lv_obj_set_style_pad_all(touch_overlay, 0, 0);
    lv_obj_remove_flag(touch_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_overlay, dashboard_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_overlay, dashboard_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_move_foreground(touch_overlay);
}

static void create_default_gauge_toast(lv_obj_t *parent)
{
    default_gauge_toast = lv_label_create(parent);
    lv_label_set_text(default_gauge_toast, "");
    lv_obj_set_width(default_gauge_toast, UI_SCREEN_W - (UI_PAD * 2));
    lv_obj_set_style_text_align(default_gauge_toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(default_gauge_toast, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_set_style_text_font(default_gauge_toast, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(default_gauge_toast, color_accent, 0);
    lv_obj_set_style_bg_color(default_gauge_toast, color_card_bg, 0);
    lv_obj_set_style_bg_opa(default_gauge_toast, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(default_gauge_toast, 16, 0);
    lv_obj_set_style_pad_ver(default_gauge_toast, 8, 0);
    lv_obj_set_style_radius(default_gauge_toast, 8, 0);
    lv_obj_add_flag(default_gauge_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(default_gauge_toast, LV_OBJ_FLAG_CLICKABLE);
}

static void create_connection_wifi_icon(lv_obj_t *parent)
{
    conn_wifi_icon = ui_wifi_ind_create(parent, -UI_ROUND_INSET, UI_ROUND_INSET / 2);
}

static void create_dashboard_screen(void)
{
    gauge_sync_availability();
    gauge_type_t startup_gauge = gauge_first_available();
    if (g_settings.default_gauge < GAUGE_MAX &&
        gauge_is_available((gauge_type_t)g_settings.default_gauge)) {
        startup_gauge = (gauge_type_t)g_settings.default_gauge;
    }

    screen_dashboard = lv_obj_create(NULL);
    ui_screen_prepare(screen_dashboard);
    lv_obj_remove_flag(screen_dashboard, LV_OBJ_FLAG_CLICKABLE);

    gauge_create_fullscreen(screen_dashboard, startup_gauge);
    gauge_create_indicator_row(screen_dashboard);
    create_connection_wifi_icon(screen_dashboard);

    dtc_banner = lv_label_create(screen_dashboard);
    lv_obj_set_width(dtc_banner, UI_SCREEN_W - 64);
    lv_obj_align(dtc_banner, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_align(dtc_banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(dtc_banner, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(dtc_banner, color_warning, 0);
    lv_obj_add_flag(dtc_banner, LV_OBJ_FLAG_HIDDEN);

    create_default_gauge_toast(screen_dashboard);
    create_touch_overlay(screen_dashboard);

    if (conn_wifi_icon != NULL) {
        lv_obj_move_foreground(conn_wifi_icon);
    }
    if (dtc_banner != NULL) {
        lv_obj_move_foreground(dtc_banner);
    }
    gauge_raise_indicator_layers();
}

static lv_obj_t *create_sub_header(lv_obj_t *parent, const char *title, const char *icon_sym)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, UI_SCREEN_W, UI_HEADER_H);
    lv_obj_set_style_bg_color(header, color_card_bg, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, color_card_border, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = lv_obj_create(header);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    if (icon_sym != NULL && icon_sym[0] != '\0') {
        ui_icon_create(row, icon_sym, UI_FONT_ICON);
    }

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(lbl, color_accent, 0);
    return header;
}

static lv_obj_t *create_footer_back_btn(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, UI_SCREEN_W - (UI_PAD * 2), 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_style(btn, style_get_btn_primary(), 0);
    lv_obj_add_event_cb(btn, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *row = lv_obj_create(btn);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    ui_icon_create(row, LV_SYMBOL_LEFT, UI_FONT_ICON);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Geri");
    lv_obj_set_style_text_font(lbl, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(lbl, color_bg_dark, 0);
    return btn;
}

static lv_obj_t *create_menu_card(lv_obj_t *parent, const char *icon_sym, const char *title,
                                  const char *desc, int col, int row, dashboard_screen_t target)
{
    const int x = UI_PAD + col * (UI_MENU_CARD_W + UI_MENU_GAP);
    const int y = UI_MENU_TOP + row * (UI_MENU_CARD_H + UI_MENU_GAP);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, UI_MENU_CARD_W, UI_MENU_CARD_H);
    lv_obj_add_style(card, style_get_card(), 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, menu_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)target);

    lv_obj_t *icon_bg = lv_obj_create(card);
    lv_obj_set_size(icon_bg, 48, 48);
    lv_obj_align(icon_bg, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_bg_color(icon_bg, color_card_bg, 0);
    lv_obj_set_style_bg_opa(icon_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(icon_bg, color_primary, 0);
    lv_obj_set_style_border_width(icon_bg, 1, 0);
    lv_obj_set_style_radius(icon_bg, 12, 0);
    lv_obj_remove_flag(icon_bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *icon_lbl = ui_icon_create(icon_bg, icon_sym, UI_FONT_ICON);
    lv_obj_center(icon_lbl);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_text_font(t, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(t, color_text, 0);

    lv_obj_t *d = lv_label_create(card);
    lv_label_set_text(d, desc);
    lv_obj_set_width(d, UI_MENU_CARD_W - 20);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 94);
    lv_obj_set_style_text_font(d, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(d, color_text_dim, 0);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
    return card;
}

static lv_obj_t *create_settings_row(lv_obj_t *parent, const char *label, lv_obj_t *control,
                                     int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_pos(lbl, 14, y);
    lv_obj_set_style_text_font(lbl, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(lbl, color_text, 0);
    lv_obj_align(control, LV_ALIGN_TOP_RIGHT, -14, y - 2);
    return lbl;
}

static void create_menu_screen(void)
{
    screen_menu = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_menu);

    create_menu_card(screen_menu, LV_SYMBOL_CHARGE, "Gösterge", "Canlı PID okumaları", 0, 0, DASHBOARD_MAIN);
    create_menu_card(screen_menu, LV_SYMBOL_WIFI, "Bağlantı", "WiFi / USB (ELM327)", 1, 0, DASHBOARD_CONNECTION);
    create_menu_card(screen_menu, LV_SYMBOL_SETTINGS, "Ayarlar", "Ekran ve uyarılar", 0, 1, DASHBOARD_SETTINGS);
    create_menu_card(screen_menu, LV_SYMBOL_LIST, "Hakkında", "Sürüm ve donanım", 1, 1, DASHBOARD_ABOUT);

    menu_wifi_icon = ui_wifi_ind_create(screen_menu, -UI_PAD, UI_PAD);
    lv_obj_move_foreground(menu_wifi_icon);
    create_footer_back_btn(screen_menu);
}

static void create_settings_screen(void)
{
    const int content_w = UI_SCREEN_W - (UI_PAD * 2);
    const int card_h = 118;

    screen_settings = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_settings);
    create_sub_header(screen_settings, "Ayarlar", LV_SYMBOL_SETTINGS);

    lv_obj_t *group_display = lv_obj_create(screen_settings);
    lv_obj_set_pos(group_display, UI_PAD, UI_HEADER_H + 8);
    lv_obj_set_size(group_display, content_w, card_h);
    lv_obj_add_style(group_display, style_get_card(), 0);
    lv_obj_remove_flag(group_display, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t1 = lv_label_create(group_display);
    lv_label_set_text(t1, "EKRAN");
    lv_obj_set_pos(t1, 14, 10);
    lv_obj_set_style_text_font(t1, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(t1, color_accent, 0);

    lv_obj_t *brightness_bar = lv_slider_create(group_display);
    lv_obj_set_size(brightness_bar, 180, 10);
    lv_slider_set_range(brightness_bar, 10, 100);
    lv_slider_set_value(brightness_bar, g_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_bar, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_settings_row(group_display, "Parlaklık", brightness_bar, 44);

    lv_obj_t *group_alerts = lv_obj_create(screen_settings);
    lv_obj_set_pos(group_alerts, UI_PAD, UI_HEADER_H + 8 + card_h + 10);
    lv_obj_set_size(group_alerts, content_w, card_h);
    lv_obj_add_style(group_alerts, style_get_card(), 0);
    lv_obj_remove_flag(group_alerts, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t2 = lv_label_create(group_alerts);
    lv_label_set_text(t2, "UYARILAR");
    lv_obj_set_pos(t2, 14, 10);
    lv_obj_set_style_text_font(t2, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(t2, color_accent, 0);

    lv_obj_t *sw = lv_switch_create(group_alerts);
    if (g_settings.haptic_enabled) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, haptic_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_settings_row(group_alerts, "RPM uyarısı", sw, 44);

    lv_obj_t *sw2 = lv_switch_create(group_alerts);
    if (g_settings.sound_enabled) {
        lv_obj_add_state(sw2, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw2, sound_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_settings_row(group_alerts, "Sesli uyarı", sw2, 78);

    lv_obj_t *theme_btn = lv_btn_create(group_display);
    lv_obj_set_size(theme_btn, 120, 36);
    lv_obj_add_style(theme_btn, style_get_btn_secondary(), 0);
    lv_obj_add_event_cb(theme_btn, theme_cycle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *theme_lbl = lv_label_create(theme_btn);
    lv_label_set_text(theme_lbl, "Tema");
    lv_obj_center(theme_lbl);
    create_settings_row(group_display, "Görünüm", theme_btn, 78);

    create_footer_back_btn(screen_settings);
}

static void create_connection_screen(void)
{
    screen_connection = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_connection);
    create_sub_header(screen_connection, "WiFi / OBD2", LV_SYMBOL_WIFI);
    wifi_settings_ui_create(screen_connection);
    create_footer_back_btn(screen_connection);
}

static void create_about_screen(void)
{
    const int content_w = UI_SCREEN_W - (UI_PAD * 2);

    screen_about = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_about);
    create_sub_header(screen_about, "Hakkında", LV_SYMBOL_LIST);

    lv_obj_t *app_title = lv_label_create(screen_about);
    lv_label_set_text(app_title, APP_NAME);
    lv_obj_set_pos(app_title, 0, UI_HEADER_H + 12);
    lv_obj_set_width(app_title, UI_SCREEN_W);
    lv_obj_set_style_text_align(app_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(app_title, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(app_title, color_text, 0);

    lv_obj_t *version = lv_label_create(screen_about);
    lv_label_set_text_fmt(version, "v%s · ESP32-S3", APP_VERSION);
    lv_obj_set_pos(version, 0, UI_HEADER_H + 40);
    lv_obj_set_width(version, UI_SCREEN_W);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(version, color_accent, 0);

    lv_obj_t *info_card = lv_obj_create(screen_about);
    lv_obj_set_pos(info_card, UI_PAD, UI_HEADER_H + 72);
    lv_obj_set_size(info_card, content_w, 168);
    lv_obj_add_style(info_card, style_get_card(), 0);
    lv_obj_remove_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);

    const char *labels[] = {"Donanım", "Ekran", "UI", "Protokol"};
    const char *values[] = {"ESP32-S3-Touch-LCD", "480x480 ST7701", "LVGL 9", "ISO 15765-4"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(info_card);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_pos(lbl, 14, 14 + i * 36);
        lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, color_text_dim, 0);
        lv_obj_t *val = lv_label_create(info_card);
        lv_label_set_text(val, values[i]);
        lv_obj_set_pos(val, 130, 14 + i * 36);
        lv_obj_set_width(val, content_w - 140);
        lv_obj_set_style_text_font(val, UI_FONT_SM, 0);
        lv_obj_set_style_text_color(val, color_text, 0);
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    }

    create_footer_back_btn(screen_about);
}
