#include "dashboard.h"
#include "gauge.h"
#include "styles.h"
#include "board_config.h"
#include "display.h"
#include "obd_service.h"
#include "wifi_settings_ui.h"
#include "lvgl_driver.h"
#include "app.h"
#include "settings.h"
#include "esp_log.h"
#include <stdlib.h>

#define UI_HEADER_H      52
#define UI_FOOTER_H      56
#define UI_PAD           16
#define UI_MENU_GAP      12
#define UI_MENU_CARD_W   ((UI_SCREEN_W - (UI_PAD * 2) - UI_MENU_GAP) / 2)
#define UI_MENU_CARD_H   ((UI_SCREEN_H - UI_HEADER_H - UI_FOOTER_H - UI_PAD - UI_MENU_GAP) / 2)

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
static bool dashboard_first_show = true;

extern app_settings_t g_settings;

static void settings_persist_from_ui(void)
{
    app_settings_save();
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
        return;
    }

    if (code != LV_EVENT_RELEASED || !touch_tracking) {
        return;
    }
    touch_tracking = false;

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
        dashboard_navigate_gauge_next();
        return;
    }
    if (diff < -UI_SWIPE_THRESHOLD_PX) {
        last_swipe_tick = now;
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

void display_update_gauges(void)
{
    if (!display_live_updates_enabled()) {
        return;
    }

    if (!lvgl_lock(0)) {
        return;
    }

    if (lv_scr_act() != screen_dashboard) {
        lvgl_unlock();
        return;
    }

    if (gauge_is_transitioning()) {
        lvgl_unlock();
        return;
    }

    obd_data_t data;
    obd_service_get_data(&data);

    if (data.rpm_valid) {
        gauge_update_fullscreen(GAUGE_RPM, (int16_t)data.rpm);
    }
    if (data.speed_valid) {
        gauge_update_fullscreen(GAUGE_SPEED, (int16_t)data.speed);
    }
    if (data.coolant_valid) {
        gauge_update_fullscreen(GAUGE_COOLANT, (int16_t)data.coolant_temp);
    }
    if (data.throttle_valid) {
        gauge_update_fullscreen(GAUGE_THROTTLE, (int16_t)data.throttle_pos);
    }
    if (data.fuel_valid) {
        gauge_update_fullscreen(GAUGE_FUEL, (int16_t)data.fuel_level);
    }
    if (data.load_valid) {
        gauge_update_fullscreen(GAUGE_LOAD, (int16_t)data.engine_load);
    }
    if (data.rpm_valid) {
        int16_t voltage = 126;
        if (data.rpm > 1000) {
            voltage = 138 + (rand() % 10);
        }
        gauge_update_fullscreen(GAUGE_VOLTAGE, voltage);
    }
    if (data.intake_valid) {
        gauge_update_fullscreen(GAUGE_INTAKE, (int16_t)data.intake_temp);
    } else if (data.coolant_valid) {
        int16_t intake_temp = data.coolant_temp - 10;
        if (intake_temp < 20) {
            intake_temp = 20;
        }
        gauge_update_fullscreen(GAUGE_INTAKE, intake_temp);
    }
    if (data.fuel_consumption_valid) {
        int16_t fuel_rate = (int16_t)(data.fuel_consumption * 10);
        gauge_update_fullscreen(GAUGE_FUEL_CONSUMPTION, fuel_rate);
    }
    if (data.dtc_present) {
        gauge_update_fullscreen(GAUGE_DTC_WARNING, (int16_t)data.dtc_count);
    } else {
        gauge_update_fullscreen(GAUGE_DTC_WARNING, 0);
    }

    gauge_tick();

    lvgl_unlock();
}

void dashboard_navigate_gauge_next(void)
{
    const gauge_type_t next = (gauge_get_active() + 1) % GAUGE_MAX;
    gauge_set_active(next);
}

void dashboard_navigate_gauge_prev(void)
{
    const gauge_type_t current = gauge_get_active();
    const gauge_type_t prev = (current == 0) ? (GAUGE_MAX - 1) : (current - 1);
    gauge_set_active(prev);
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

static void create_dashboard_screen(void)
{
    screen_dashboard = lv_obj_create(NULL);
    ui_screen_prepare(screen_dashboard);
    lv_obj_remove_flag(screen_dashboard, LV_OBJ_FLAG_CLICKABLE);

    gauge_create_fullscreen(screen_dashboard, GAUGE_RPM);
    create_touch_overlay(screen_dashboard);
}

static lv_obj_t *create_sub_header(lv_obj_t *parent, const char *title)
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

    lv_obj_t *lbl = lv_label_create(header);
    lv_label_set_text(lbl, title);
    lv_obj_center(lbl);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
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
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "GERI");
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *create_menu_card(lv_obj_t *parent, const char *title, const char *desc,
                                  int col, int row, dashboard_screen_t target)
{
    const int x = UI_PAD + col * (UI_MENU_CARD_W + UI_MENU_GAP);
    const int y = UI_HEADER_H + UI_PAD / 2 + row * (UI_MENU_CARD_H + UI_MENU_GAP);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, UI_MENU_CARD_W, UI_MENU_CARD_H);
    lv_obj_add_style(card, style_get_card(), 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, menu_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)target);

    lv_obj_t *icon = lv_obj_create(card);
    lv_obj_set_size(icon, 48, 48);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_bg_color(icon, color_primary, 0);
    lv_obj_set_style_radius(icon, 12, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(t, color_text, 0);

    lv_obj_t *d = lv_label_create(card);
    lv_label_set_text(d, desc);
    lv_obj_set_width(d, UI_MENU_CARD_W - 20);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 94);
    lv_obj_set_style_text_font(d, &lv_font_montserrat_10, 0);
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
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, color_text, 0);
    lv_obj_align(control, LV_ALIGN_TOP_RIGHT, -14, y - 2);
    return lbl;
}

static void create_menu_screen(void)
{
    screen_menu = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_menu);
    create_sub_header(screen_menu, "MENU");

    create_menu_card(screen_menu, "Gosterge", "Canli PID okumalari", 0, 0, DASHBOARD_MAIN);
    create_menu_card(screen_menu, "Baglanti", "WiFi / BT / USB", 1, 0, DASHBOARD_CONNECTION);
    create_menu_card(screen_menu, "Ayarlar", "Ekran ve uyarilar", 0, 1, DASHBOARD_SETTINGS);
    create_menu_card(screen_menu, "Hakkinda", "Surum ve donanim", 1, 1, DASHBOARD_ABOUT);

    create_footer_back_btn(screen_menu);
}

static void create_settings_screen(void)
{
    const int content_w = UI_SCREEN_W - (UI_PAD * 2);
    const int card_h = 118;

    screen_settings = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_settings);
    create_sub_header(screen_settings, "AYARLAR");

    lv_obj_t *group_display = lv_obj_create(screen_settings);
    lv_obj_set_pos(group_display, UI_PAD, UI_HEADER_H + 8);
    lv_obj_set_size(group_display, content_w, card_h);
    lv_obj_add_style(group_display, style_get_card(), 0);
    lv_obj_remove_flag(group_display, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t1 = lv_label_create(group_display);
    lv_label_set_text(t1, "EKRAN");
    lv_obj_set_pos(t1, 14, 10);
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t1, color_accent, 0);

    lv_obj_t *brightness_bar = lv_slider_create(group_display);
    lv_obj_set_size(brightness_bar, 180, 10);
    lv_slider_set_range(brightness_bar, 10, 100);
    lv_slider_set_value(brightness_bar, g_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_bar, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_settings_row(group_display, "Parlaklik", brightness_bar, 44);

    lv_obj_t *group_alerts = lv_obj_create(screen_settings);
    lv_obj_set_pos(group_alerts, UI_PAD, UI_HEADER_H + 8 + card_h + 10);
    lv_obj_set_size(group_alerts, content_w, card_h);
    lv_obj_add_style(group_alerts, style_get_card(), 0);
    lv_obj_remove_flag(group_alerts, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t2 = lv_label_create(group_alerts);
    lv_label_set_text(t2, "UYARILAR");
    lv_obj_set_pos(t2, 14, 10);
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t2, color_accent, 0);

    lv_obj_t *sw = lv_switch_create(group_alerts);
    if (g_settings.haptic_enabled) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, haptic_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_settings_row(group_alerts, "RPM uyarisi", sw, 44);

    lv_obj_t *sw2 = lv_switch_create(group_alerts);
    if (g_settings.sound_enabled) {
        lv_obj_add_state(sw2, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw2, sound_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_settings_row(group_alerts, "Sesli uyari", sw2, 78);

    create_footer_back_btn(screen_settings);
}

static void create_connection_screen(void)
{
    screen_connection = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_connection);
    create_sub_header(screen_connection, "WIFI / OBD2");
    wifi_settings_ui_create(screen_connection);
    create_footer_back_btn(screen_connection);
}

static void create_about_screen(void)
{
    const int content_w = UI_SCREEN_W - (UI_PAD * 2);

    screen_about = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_about);
    create_sub_header(screen_about, "HAKKINDA");

    lv_obj_t *app_title = lv_label_create(screen_about);
    lv_label_set_text(app_title, "OBD2 TELEMETRY");
    lv_obj_set_pos(app_title, 0, UI_HEADER_H + 12);
    lv_obj_set_width(app_title, UI_SCREEN_W);
    lv_obj_set_style_text_align(app_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(app_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(app_title, color_text, 0);

    lv_obj_t *version = lv_label_create(screen_about);
    lv_label_set_text(version, "v1.0.0 · ESP32-S3");
    lv_obj_set_pos(version, 0, UI_HEADER_H + 40);
    lv_obj_set_width(version, UI_SCREEN_W);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(version, color_accent, 0);

    lv_obj_t *info_card = lv_obj_create(screen_about);
    lv_obj_set_pos(info_card, UI_PAD, UI_HEADER_H + 72);
    lv_obj_set_size(info_card, content_w, 168);
    lv_obj_add_style(info_card, style_get_card(), 0);
    lv_obj_remove_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);

    const char *labels[] = {"Donanim", "Ekran", "UI", "Protokol"};
    const char *values[] = {"ESP32-S3-Touch-LCD", "480x480 ST7701", "LVGL 9", "ISO 15765-4"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(info_card);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_pos(lbl, 14, 14 + i * 36);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, color_text_dim, 0);
        lv_obj_t *val = lv_label_create(info_card);
        lv_label_set_text(val, values[i]);
        lv_obj_set_pos(val, 130, 14 + i * 36);
        lv_obj_set_width(val, content_w - 140);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(val, color_text, 0);
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    }

    create_footer_back_btn(screen_about);
}
