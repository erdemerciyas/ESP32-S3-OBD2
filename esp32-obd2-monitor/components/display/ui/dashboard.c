#include "dashboard.h"
#include "gauge.h"
#include "telemetry.h"
#include "obd_service.h"
#include "styles.h"
#include "board_config.h"
#include "display.h"
#include "haptic.h"
#include "connectivity.h"
#include "bt_settings_ui.h"
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

#define UI_MENU_GAP          10
#define UI_MENU_TOP          (UI_SUB_PAD + 4)
#define UI_MENU_CARD_W       ((UI_SUB_CONTENT_W - UI_MENU_GAP) / 2)
#define UI_MENU_CARD_H       ((UI_SCREEN_H - UI_MENU_TOP - UI_FOOTER_H - UI_SUB_PAD - UI_MENU_GAP) / 2)

#define UI_SETTINGS_TAB_H      44
#define UI_SETTINGS_TAB_COUNT  3

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
static lv_timer_t *gauge_hold_timer;
static int gauge_hold_dir;
static bool dashboard_first_show = true;
static uint32_t subscreen_activity_tick;
static lv_timer_t *idle_return_timer;
static bool idle_return_pending;
static lv_obj_t *conn_obd_hud_icon;
static lv_obj_t *menu_wifi_icon;
static lv_obj_t *dtc_banner;

extern app_settings_t g_settings;

static void dashboard_raise_gauge_hud_layers(void);
static void dashboard_update_conn_indicators(const telemetry_snapshot_t *snap);
static void gauge_hold_repeat_cb(lv_timer_t *timer);
static void gauge_hold_stop(void);
static void cancel_long_press_timer(void);
static void dashboard_note_activity(void);
static void dashboard_idle_return_cb(lv_timer_t *timer);
static void dashboard_input_activity_cb(lv_event_t *e);

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

    lv_label_set_text(default_gauge_toast, "Default startup gauge saved");
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
    const uint8_t val = (uint8_t)lv_slider_get_value(slider);
    g_settings.brightness = val;
    display_set_brightness(val);
    settings_persist_from_ui();
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl != NULL) {
        lv_label_set_text_fmt(lbl, "%u%%", val);
    }
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

static lv_obj_t *gauge_order_list;
static bool gauge_order_list_built;
static lv_obj_t *settings_tab_panels[UI_SETTINGS_TAB_COUNT];
static lv_obj_t *settings_tab_btns[UI_SETTINGS_TAB_COUNT];
static int settings_active_tab;

static void gauge_order_up_cb(lv_event_t *e);
static void gauge_order_down_cb(lv_event_t *e);

static void settings_rebuild_gauge_order_rows(void)
{
    if (gauge_order_list == NULL) {
        return;
    }

    lv_obj_clean(gauge_order_list);

    for (int slot = 0; slot < GAUGE_MAX; slot++) {
        const gauge_type_t type = (gauge_type_t)g_settings.gauge_order[slot];
        const uint32_t gauge_color = gauge_get_color(type);

        lv_obj_t *row = lv_obj_create(gauge_order_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), UI_SUB_ROW_H);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(gauge_color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%d. %s", slot + 1, gauge_get_label(type));
        lv_obj_set_width(lbl, UI_SUB_CONTENT_W - (UI_SUB_TOUCH_BTN * 2) - 56);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(gauge_color), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

        lv_obj_t *up_btn = lv_btn_create(row);
        lv_obj_set_size(up_btn, UI_SUB_TOUCH_BTN, 40);
        lv_obj_align(up_btn, LV_ALIGN_RIGHT_MID, -(UI_SUB_TOUCH_BTN + 6), 0);
        lv_obj_add_style(up_btn, style_get_btn_secondary(), 0);
        lv_obj_add_event_cb(up_btn, gauge_order_up_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)slot);
        lv_obj_t *up_lbl = lv_label_create(up_btn);
        lv_label_set_text(up_lbl, LV_SYMBOL_UP);
        lv_obj_center(up_lbl);
        lv_obj_set_style_text_font(up_lbl, UI_FONT_MD, 0);
        if (slot == 0) {
            lv_obj_add_state(up_btn, LV_STATE_DISABLED);
        }

        lv_obj_t *down_btn = lv_btn_create(row);
        lv_obj_set_size(down_btn, UI_SUB_TOUCH_BTN, 40);
        lv_obj_align(down_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_style(down_btn, style_get_btn_secondary(), 0);
        lv_obj_add_event_cb(down_btn, gauge_order_down_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)slot);
        lv_obj_t *down_lbl = lv_label_create(down_btn);
        lv_label_set_text(down_lbl, LV_SYMBOL_DOWN);
        lv_obj_center(down_lbl);
        lv_obj_set_style_text_font(down_lbl, UI_FONT_MD, 0);
        if (slot == GAUGE_MAX - 1) {
            lv_obj_add_state(down_btn, LV_STATE_DISABLED);
        }
    }
}

static void gauge_order_up_cb(lv_event_t *e)
{
    const int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot <= 0) {
        return;
    }
    gauge_swap_order_slots(slot, slot - 1);
    settings_persist_from_ui();
    settings_rebuild_gauge_order_rows();
    gauge_settings_changed();
}

static void gauge_order_down_cb(lv_event_t *e)
{
    const int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot >= GAUGE_MAX - 1) {
        return;
    }
    gauge_swap_order_slots(slot, slot + 1);
    settings_persist_from_ui();
    settings_rebuild_gauge_order_rows();
    gauge_settings_changed();
}

static void max_rpm_step_cb(lv_event_t *e)
{
    const int step = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(lv_event_get_target(e));
    int v = (int)g_settings.max_rpm + step;
    if (v < 2000) {
        v = 2000;
    }
    if (v > 9000) {
        v = 9000;
    }
    g_settings.max_rpm = (uint16_t)v;
    if (lbl != NULL) {
        lv_label_set_text_fmt(lbl, "%u rpm", g_settings.max_rpm);
    }
    gauge_settings_changed();
    settings_persist_from_ui();
}

static void max_speed_step_cb(lv_event_t *e)
{
    const int step = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(lv_event_get_target(e));
    int v = (int)g_settings.max_speed + step;
    if (v < 60) {
        v = 60;
    }
    if (v > 320) {
        v = 320;
    }
    g_settings.max_speed = (uint16_t)v;
    if (lbl != NULL) {
        lv_label_set_text_fmt(lbl, "%u km/h", g_settings.max_speed);
    }
    gauge_settings_changed();
    settings_persist_from_ui();
}

static void settings_show_tab(int tab)
{
    if (tab < 0 || tab >= UI_SETTINGS_TAB_COUNT) {
        return;
    }
    settings_active_tab = tab;
    for (int i = 0; i < UI_SETTINGS_TAB_COUNT; i++) {
        if (settings_tab_panels[i] != NULL) {
            if (i == tab) {
                lv_obj_remove_flag(settings_tab_panels[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(settings_tab_panels[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (settings_tab_btns[i] != NULL) {
            if (i == tab) {
                lv_obj_add_state(settings_tab_btns[i], LV_STATE_CHECKED);
                lv_obj_set_style_bg_color(settings_tab_btns[i], color_primary, LV_PART_MAIN);
                lv_obj_set_style_text_color(settings_tab_btns[i], color_bg_dark, LV_PART_MAIN);
            } else {
                lv_obj_clear_state(settings_tab_btns[i], LV_STATE_CHECKED);
                lv_obj_set_style_bg_color(settings_tab_btns[i], color_card_bg, LV_PART_MAIN);
                lv_obj_set_style_text_color(settings_tab_btns[i], color_text, LV_PART_MAIN);
            }
        }
    }
    dashboard_note_activity();
}

static void settings_tab_click_cb(lv_event_t *e)
{
    const int tab = (int)(intptr_t)lv_event_get_user_data(e);
    settings_show_tab(tab);
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

static void dashboard_note_activity(void)
{
    subscreen_activity_tick = lv_tick_get();
}

static void dashboard_input_activity_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_CLICKED ||
        code == LV_EVENT_VALUE_CHANGED) {
        dashboard_note_activity();
    }
}

static void dashboard_idle_return_async_cb(void *user_data)
{
    (void)user_data;
    idle_return_pending = false;

    if (!display_live_updates_enabled()) {
        return;
    }

    lv_obj_t *active = lv_scr_act();
    if (active == NULL || active == screen_dashboard) {
        return;
    }

    ESP_LOGI(TAG, "Idle %u ms on sub-screen — returning to gauges",
             (unsigned)DASHBOARD_IDLE_RETURN_MS);
    dashboard_show_screen(DASHBOARD_MAIN);
}

static void dashboard_idle_return_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!display_live_updates_enabled()) {
        return;
    }

    lv_obj_t *active = lv_scr_act();
    if (active == NULL || active == screen_dashboard) {
        idle_return_pending = false;
        return;
    }

    if (connectivity_is_user_busy()) {
        dashboard_note_activity();
        return;
    }

    lv_indev_t *indev = lv_indev_get_next(NULL);
    for (; indev != NULL; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            dashboard_note_activity();
            return;
        }
    }

    if (lv_tick_elaps(subscreen_activity_tick) >= DASHBOARD_IDLE_RETURN_MS && !idle_return_pending) {
        idle_return_pending = true;
        lv_async_call(dashboard_idle_return_async_cb, NULL);
    }
}

static void ui_subscreen_prepare(lv_obj_t *scr)
{
    ui_screen_prepare(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, dashboard_input_activity_cb, LV_EVENT_ALL, NULL);
}

static bool touch_in_gauge_nav_zone(const lv_point_t *pt)
{
    return pt->y >= UI_GAUGE_NAV_Y;
}

static void dashboard_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        dashboard_note_activity();
        lv_indev_get_point(indev, &touch_press_pt);
        touch_tracking = true;
        long_press_fired = false;
        cancel_long_press_timer();

        if (touch_in_gauge_nav_zone(&touch_press_pt)) {
            gauge_hold_stop();
            gauge_hold_dir = (touch_press_pt.x < UI_SCREEN_W / 2) ? -1 : 1;
            gauge_hold_timer = lv_timer_create(gauge_hold_repeat_cb, UI_GAUGE_HOLD_MS, NULL);
            lv_timer_set_repeat_count(gauge_hold_timer, 1);
            return;
        }

        long_press_timer = lv_timer_create(long_press_timer_cb, UI_LONG_PRESS_MS, NULL);
        lv_timer_set_repeat_count(long_press_timer, 1);
        return;
    }

    if (code == LV_EVENT_LONG_PRESSED && touch_tracking &&
        touch_in_gauge_nav_zone(&touch_press_pt)) {
        if (gauge_hold_dir == 0) {
            gauge_hold_dir = (touch_press_pt.x < UI_SCREEN_W / 2) ? -1 : 1;
        }
        gauge_hold_stop();
        gauge_hold_repeat_cb(NULL);
        gauge_hold_timer = lv_timer_create(gauge_hold_repeat_cb, UI_GAUGE_HOLD_REPEAT_MS, NULL);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (touch_in_gauge_nav_zone(&touch_press_pt)) {
            gauge_hold_stop();
        }
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
    if (touch_in_gauge_nav_zone(&touch_press_pt)) {
        last_tap_tick = 0;
        return;
    }

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
    gauge_cancel_transition();
    cancel_long_press_timer();
    gauge_hold_stop();
    touch_tracking = false;
}

void dashboard_init(void)
{
    ESP_LOGI(TAG, "Initializing dashboard UI (%dx%d)", UI_SCREEN_W, UI_SCREEN_H);

    create_dashboard_screen();
    create_menu_screen();
    create_settings_screen();
    create_connection_screen();
    create_about_screen();

    subscreen_activity_tick = lv_tick_get();
    if (idle_return_timer == NULL) {
        idle_return_timer = lv_timer_create(dashboard_idle_return_cb, 1000, NULL);
    }

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
        bt_settings_ui_refresh();
        bt_settings_ui_on_screen_shown();
    }

    if (screen == DASHBOARD_SETTINGS && !gauge_order_list_built && gauge_order_list != NULL) {
        settings_rebuild_gauge_order_rows();
        gauge_order_list_built = true;
    }

    if (dashboard_first_show) {
        dashboard_first_show = false;
        lv_scr_load(target);
        return;
    }

    if (screen == DASHBOARD_MAIN) {
        /* FADE_ON here caused reboots (nested opacity + gauge arc redraw). Match boot path. */
        gauge_cancel_transition();
        lv_scr_load(target);
        dashboard_raise_gauge_hud_layers();
        connectivity_sync_transport_state();
        telemetry_snapshot_t snap;
        telemetry_get_snapshot(&snap);
        dashboard_update_conn_indicators(&snap);
    } else {
        dashboard_note_activity();
        if (lv_scr_act() == screen_dashboard) {
            cancel_long_press_timer();
            touch_tracking = false;
            gauge_hold_stop();
        }
        lv_screen_load_anim(target, LV_SCR_LOAD_ANIM_MOVE_LEFT, 240, 0, false);
    }
}

static void dashboard_update_conn_indicators(const telemetry_snapshot_t *snap)
{
    if (connectivity_is_user_busy() && lv_scr_act() == screen_connection) {
        return;
    }

    const ui_conn_ind_level_t level = ui_conn_ind_level_from_ex(
        snap->bt_linked, snap->bt_serial_up, snap->conn_state, snap->bt_auto_pending);

    if (lv_scr_act() == screen_connection) {
        bt_settings_ui_sync_conn_ind(level);
        return;
    }

    if (conn_obd_hud_icon != NULL) {
        ui_conn_ind_apply(conn_obd_hud_icon, level);
    }
    if (menu_wifi_icon != NULL) {
        ui_conn_ind_apply(menu_wifi_icon, level);
    }
}

static void dashboard_update_hud(const telemetry_snapshot_t *snap)
{
    dashboard_update_conn_indicators(snap);

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
    if (!display_live_updates_enabled() || connectivity_is_user_busy() || lvgl_rf_quiet()) {
        return;
    }

    if (!lvgl_lock(0)) {
        return;
    }

    lv_obj_t *active = lv_scr_act();

    if (active != screen_dashboard) {
        if (active == screen_connection) {
            telemetry_snapshot_t snap;
            telemetry_get_snapshot(&snap);
            dashboard_update_conn_indicators(&snap);
        }
        lvgl_unlock();
        return;
    }

    telemetry_snapshot_t snap;
    telemetry_get_snapshot(&snap);

    const obd_data_t *data = &snap.obd;
    const bool live = (snap.conn_state == CONN_STATE_OBD_READY) &&
                      obd_service_data_is_live(data);

    static uint32_t last_idle_hud_tick;
    if (!live) {
        const uint32_t hud_interval_ms = 1000;
        if (lv_tick_elaps(last_idle_hud_tick) < hud_interval_ms && !gauge_needs_animation_tick()) {
            lvgl_unlock();
            return;
        }
        last_idle_hud_tick = lv_tick_get();
    }

    gauge_sync_availability();
    dashboard_update_hud(&snap);

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

    if (lv_scr_act() == screen_dashboard && gauge_needs_animation_tick()) {
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

static void gauge_hold_repeat_cb(lv_timer_t *timer)
{
    (void)timer;
    if (lv_scr_act() != screen_dashboard || gauge_is_transitioning()) {
        return;
    }
    if (gauge_hold_dir < 0) {
        dashboard_navigate_gauge_prev();
    } else {
        dashboard_navigate_gauge_next();
    }
    haptic_click();
}

static void gauge_hold_stop(void)
{
    if (gauge_hold_timer != NULL) {
        lv_timer_delete(gauge_hold_timer);
        gauge_hold_timer = NULL;
    }
    gauge_hold_dir = 0;
}

static void create_gauge_nav_strip(lv_obj_t *parent)
{
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, LV_SYMBOL_LEFT " hold  " LV_SYMBOL_RIGHT);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_text_font(hint, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(hint, color_text_dim, 0);
    lv_obj_set_style_opa(hint, LV_OPA_50, 0);
    lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);
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
    lv_obj_add_event_cb(touch_overlay, dashboard_touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(touch_overlay, dashboard_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(touch_overlay, dashboard_touch_cb, LV_EVENT_PRESS_LOST, NULL);
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

static void create_connection_obd_hud_icon(lv_obj_t *parent)
{
    conn_obd_hud_icon = ui_conn_ind_create_gauge_hud(parent);
}

static void dashboard_raise_gauge_hud_layers(void)
{
    gauge_raise_indicator_layers();
    if (conn_obd_hud_icon != NULL) {
        lv_obj_move_foreground(conn_obd_hud_icon);
    }
    if (touch_overlay != NULL) {
        lv_obj_move_foreground(touch_overlay);
    }
    gauge_hold_stop();
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
    create_gauge_nav_strip(screen_dashboard);
    create_connection_obd_hud_icon(screen_dashboard);

    dtc_banner = lv_label_create(screen_dashboard);
    lv_obj_set_width(dtc_banner, UI_SCREEN_W - 64);
    lv_obj_align(dtc_banner, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_align(dtc_banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(dtc_banner, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(dtc_banner, color_warning, 0);
    lv_obj_add_flag(dtc_banner, LV_OBJ_FLAG_HIDDEN);

    create_default_gauge_toast(screen_dashboard);
    create_touch_overlay(screen_dashboard);

    if (dtc_banner != NULL) {
        lv_obj_move_foreground(dtc_banner);
    }
    dashboard_raise_gauge_hud_layers();
    if (default_gauge_toast != NULL) {
        lv_obj_move_foreground(default_gauge_toast);
    }
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
    lv_obj_set_size(btn, UI_SUB_CONTENT_W, UI_SUB_TOUCH_BTN);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -(UI_SUB_PAD / 2));
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
    lv_label_set_text(lbl, "Back");
    lv_obj_set_style_text_font(lbl, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(lbl, color_bg_dark, 0);
    return btn;
}

static lv_obj_t *create_menu_card(lv_obj_t *parent, const char *icon_sym, const char *title,
                                  const char *desc, int col, int row, dashboard_screen_t target)
{
    const int x = UI_SUB_PAD + col * (UI_MENU_CARD_W + UI_MENU_GAP);
    const int y = UI_MENU_TOP + row * (UI_MENU_CARD_H + UI_MENU_GAP);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, UI_MENU_CARD_W, UI_MENU_CARD_H);
    lv_obj_add_style(card, style_get_card(), 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, menu_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)target);

    lv_obj_t *icon_bg = lv_obj_create(card);
    lv_obj_set_size(icon_bg, 52, 52);
    lv_obj_align(icon_bg, LV_ALIGN_TOP_MID, 0, 12);
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
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_text_font(t, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(t, color_text, 0);

    lv_obj_t *d = lv_label_create(card);
    lv_label_set_text(d, desc);
    lv_obj_set_width(d, UI_MENU_CARD_W - 16);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_text_font(d, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(d, color_text_dim, 0);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
    return card;
}

static lv_obj_t *create_settings_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_add_style(card, style_get_card(), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *hdr = lv_label_create(card);
    lv_label_set_text(hdr, title);
    lv_obj_set_style_text_font(hdr, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(hdr, color_accent, 0);
    lv_obj_set_style_text_letter_space(hdr, 2, 0);
    return card;
}

static lv_obj_t *create_settings_brightness_block(lv_obj_t *card, int16_t value,
                                                  lv_event_cb_t cb, lv_obj_t **value_lbl_out)
{
    lv_obj_t *block = lv_obj_create(card);
    lv_obj_remove_style_all(block);
    lv_obj_set_width(block, LV_PCT(100));
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    lv_obj_set_style_pad_row(block, 8, 0);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *top = lv_obj_create(block);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(top);
    lv_label_set_text(lbl, "Brightness");
    lv_obj_set_style_text_font(lbl, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(lbl, color_text, 0);

    lv_obj_t *val_lbl = lv_label_create(top);
    lv_label_set_text_fmt(val_lbl, "%d%%", value);
    lv_obj_set_style_text_font(val_lbl, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(val_lbl, color_accent, 0);
    if (value_lbl_out != NULL) {
        *value_lbl_out = val_lbl;
    }

    lv_obj_t *slider = lv_slider_create(block);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_set_height(slider, 22);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_add_style(slider, style_get_slider(), LV_PART_MAIN);
    lv_obj_add_style(slider, style_get_slider(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, color_primary, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, color_primary, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 10, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, val_lbl);
    return slider;
}

static lv_obj_t *create_settings_stepper_block(lv_obj_t *card, const char *label,
                                               const char *value_fmt, int16_t value,
                                               lv_color_t value_color,
                                               lv_event_cb_t dec_cb, intptr_t dec_step,
                                               lv_event_cb_t inc_cb, intptr_t inc_step,
                                               lv_obj_t **value_lbl_out)
{
    lv_obj_t *block = lv_obj_create(card);
    lv_obj_remove_style_all(block);
    lv_obj_set_width(block, LV_PCT(100));
    lv_obj_set_height(block, UI_SUB_ROW_H + 8);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    lv_obj_set_style_pad_row(block, 6, 0);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(block);
    lv_label_set_text(title, label);
    lv_obj_set_style_text_font(title, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(title, color_text, 0);

    lv_obj_t *row = lv_obj_create(block);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), UI_SUB_TOUCH_BTN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *dec_btn = lv_btn_create(row);
    lv_obj_set_size(dec_btn, UI_SUB_TOUCH_BTN, UI_SUB_TOUCH_BTN);
    lv_obj_add_style(dec_btn, style_get_btn_secondary(), 0);
    lv_obj_t *dec_sym = lv_label_create(dec_btn);
    lv_label_set_text(dec_sym, LV_SYMBOL_MINUS);
    lv_obj_center(dec_sym);

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_label_set_text_fmt(val_lbl, value_fmt, value);
    lv_obj_set_flex_grow(val_lbl, 1);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(val_lbl, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(val_lbl, value_color, 0);
    if (value_lbl_out != NULL) {
        *value_lbl_out = val_lbl;
    }

    lv_obj_t *inc_btn = lv_btn_create(row);
    lv_obj_set_size(inc_btn, UI_SUB_TOUCH_BTN, UI_SUB_TOUCH_BTN);
    lv_obj_add_style(inc_btn, style_get_btn_secondary(), 0);
    lv_obj_t *inc_sym = lv_label_create(inc_btn);
    lv_label_set_text(inc_sym, LV_SYMBOL_PLUS);
    lv_obj_center(inc_sym);

    lv_obj_set_user_data(dec_btn, val_lbl);
    lv_obj_set_user_data(inc_btn, val_lbl);
    lv_obj_add_event_cb(dec_btn, dec_cb, LV_EVENT_CLICKED, (void *)dec_step);
    lv_obj_add_event_cb(inc_btn, inc_cb, LV_EVENT_CLICKED, (void *)inc_step);
    return row;
}

static lv_obj_t *create_settings_switch_row(lv_obj_t *card, const char *label, bool enabled,
                                            lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), UI_SUB_ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(lbl, color_text, 0);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 56, 30);
    lv_obj_add_style(sw, style_get_toggle(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, color_primary, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(sw, 12);
    if (enabled) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static lv_obj_t *create_settings_tab_bar(lv_obj_t *parent, const char *const *labels, int count)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_pos(bar, UI_SUB_PAD, UI_HEADER_H);
    lv_obj_set_size(bar, UI_SUB_CONTENT_W, UI_SETTINGS_TAB_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const int tab_w = (UI_SUB_CONTENT_W - (6 * (count - 1))) / count;
    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_size(btn, tab_w, UI_SETTINGS_TAB_H);
        lv_obj_add_style(btn, style_get_btn_secondary(), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
        lv_obj_add_event_cb(btn, settings_tab_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        settings_tab_btns[i] = btn;
    }
    return bar;
}

static lv_obj_t *create_settings_panel(lv_obj_t *parent, int panel_h, bool scrollable)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, UI_SUB_PAD, UI_HEADER_H + UI_SETTINGS_TAB_H + 6);
    lv_obj_set_size(panel, UI_SUB_CONTENT_W, panel_h);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    if (scrollable) {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    } else {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    }
    return panel;
}

static void default_gauge_step(bool forward, lv_obj_t *name_lbl)
{
    gauge_type_t cur = (gauge_type_t)g_settings.default_gauge;
    if (!gauge_is_available(cur)) {
        cur = gauge_first_available();
    }
    const gauge_type_t next = forward ? gauge_next_available(cur) : gauge_prev_available(cur);
    if (next == cur) {
        return;
    }
    g_settings.default_gauge = (uint8_t)next;
    settings_persist_from_ui();
    if (name_lbl != NULL) {
        lv_label_set_text(name_lbl, gauge_get_label(next));
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(gauge_get_color(next)), 0);
    }
}

static void default_gauge_prev_cb(lv_event_t *e)
{
    default_gauge_step(false, (lv_obj_t *)lv_event_get_user_data(e));
}

static void default_gauge_next_cb(lv_event_t *e)
{
    default_gauge_step(true, (lv_obj_t *)lv_event_get_user_data(e));
}

static void create_menu_screen(void)
{
    screen_menu = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_menu);

    create_menu_card(screen_menu, LV_SYMBOL_CHARGE, "Gauges", "Back to dashboard", 0, 0, DASHBOARD_MAIN);
    create_menu_card(screen_menu, LV_SYMBOL_BLUETOOTH, "Bluetooth", "Scan & connect", 1, 0, DASHBOARD_CONNECTION);
    create_menu_card(screen_menu, LV_SYMBOL_SETTINGS, "Settings", "Display & gauges", 0, 1, DASHBOARD_SETTINGS);
    create_menu_card(screen_menu, LV_SYMBOL_LIST, "About", "Device info", 1, 1, DASHBOARD_ABOUT);

    menu_wifi_icon = ui_conn_ind_create(screen_menu, -UI_SUB_PAD, UI_SUB_PAD);
    lv_obj_move_foreground(menu_wifi_icon);
    create_footer_back_btn(screen_menu);
}

static void create_settings_screen(void)
{
    const int body_h = UI_SCREEN_H - UI_HEADER_H - UI_FOOTER_H - UI_SETTINGS_TAB_H - 6;
    static const char *tab_labels[] = {"Display", "Gauges", "Alerts"};

    screen_settings = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_settings);
    create_sub_header(screen_settings, "Settings", LV_SYMBOL_SETTINGS);

    memset(settings_tab_panels, 0, sizeof(settings_tab_panels));
    memset(settings_tab_btns, 0, sizeof(settings_tab_btns));
    create_settings_tab_bar(screen_settings, tab_labels, UI_SETTINGS_TAB_COUNT);

    /* --- Display tab (fits one screen, no scroll) --- */
    settings_tab_panels[0] = create_settings_panel(screen_settings, body_h, false);
    lv_obj_t *group_display = create_settings_section(settings_tab_panels[0], "DISPLAY");
    lv_obj_t *bright_val_lbl = NULL;
    create_settings_brightness_block(group_display, (int16_t)g_settings.brightness,
                                   brightness_changed_cb, &bright_val_lbl);

    lv_obj_t *round_hint = lv_label_create(group_display);
    lv_label_set_text(round_hint, "Content stays inside the round bezel safe area.");
    lv_obj_set_style_text_font(round_hint, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(round_hint, color_text_dim, 0);
    lv_label_set_long_mode(round_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(round_hint, LV_PCT(100));

    /* --- Gauges tab (scrollable: limits + startup + swipe order) --- */
    settings_tab_panels[1] = create_settings_panel(screen_settings, body_h, true);
    lv_obj_t *group_limits = create_settings_section(settings_tab_panels[1], "GAUGE LIMITS");
    lv_obj_t *rpm_val_lbl = NULL;
    lv_obj_t *spd_val_lbl = NULL;
    create_settings_stepper_block(group_limits, "Max RPM", "%u rpm", (int16_t)g_settings.max_rpm,
                                  lv_color_hex(gauge_get_color(GAUGE_RPM)),
                                  max_rpm_step_cb, -500, max_rpm_step_cb, 500, &rpm_val_lbl);
    create_settings_stepper_block(group_limits, "Max speed", "%u km/h", (int16_t)g_settings.max_speed,
                                  lv_color_hex(gauge_get_color(GAUGE_SPEED)),
                                  max_speed_step_cb, -10, max_speed_step_cb, 10, &spd_val_lbl);

    gauge_type_t startup = (gauge_type_t)g_settings.default_gauge;
    if (!gauge_is_available(startup)) {
        startup = gauge_first_available();
    }
    lv_obj_t *group_startup = create_settings_section(settings_tab_panels[1], "STARTUP GAUGE");
    lv_obj_t *startup_row = lv_obj_create(group_startup);
    lv_obj_remove_style_all(startup_row);
    lv_obj_set_size(startup_row, LV_PCT(100), UI_SUB_TOUCH_BTN);
    lv_obj_set_style_bg_opa(startup_row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(startup_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(startup_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(startup_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(startup_row, 8, 0);

    lv_obj_t *prev_btn = lv_btn_create(startup_row);
    lv_obj_set_size(prev_btn, UI_SUB_TOUCH_BTN, UI_SUB_TOUCH_BTN);
    lv_obj_add_style(prev_btn, style_get_btn_secondary(), 0);
    lv_obj_t *startup_name = lv_label_create(startup_row);
    lv_label_set_text(startup_name, gauge_get_label(startup));
    lv_obj_set_flex_grow(startup_name, 1);
    lv_obj_set_style_text_align(startup_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(startup_name, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(startup_name, lv_color_hex(gauge_get_color(startup)), 0);
    lv_label_set_long_mode(startup_name, LV_LABEL_LONG_DOT);
    lv_obj_t *next_btn = lv_btn_create(startup_row);
    lv_obj_set_size(next_btn, UI_SUB_TOUCH_BTN, UI_SUB_TOUCH_BTN);
    lv_obj_add_style(next_btn, style_get_btn_secondary(), 0);
    lv_obj_t *prev_sym = lv_label_create(prev_btn);
    lv_label_set_text(prev_sym, LV_SYMBOL_LEFT);
    lv_obj_center(prev_sym);
    lv_obj_t *next_sym = lv_label_create(next_btn);
    lv_label_set_text(next_sym, LV_SYMBOL_RIGHT);
    lv_obj_center(next_sym);
    lv_obj_add_event_cb(prev_btn, default_gauge_prev_cb, LV_EVENT_CLICKED, startup_name);
    lv_obj_add_event_cb(next_btn, default_gauge_next_cb, LV_EVENT_CLICKED, startup_name);

    lv_obj_t *group_order = create_settings_section(settings_tab_panels[1], "SWIPE ORDER");
    lv_obj_t *hint = lv_label_create(group_order);
    lv_label_set_text(hint, "Tap arrows to reorder gauge pages");
    lv_obj_set_style_text_font(hint, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(hint, color_text_dim, 0);

    gauge_order_list = lv_obj_create(group_order);
    lv_obj_set_width(gauge_order_list, LV_PCT(100));
    lv_obj_set_height(gauge_order_list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(gauge_order_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge_order_list, 0, 0);
    lv_obj_set_style_pad_all(gauge_order_list, 0, 0);
    lv_obj_set_style_pad_row(gauge_order_list, 4, 0);
    lv_obj_set_flex_flow(gauge_order_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(gauge_order_list, LV_OBJ_FLAG_SCROLLABLE);
    gauge_order_list_built = false;

    /* --- Alerts tab --- */
    settings_tab_panels[2] = create_settings_panel(screen_settings, body_h, false);
    lv_obj_t *group_alerts = create_settings_section(settings_tab_panels[2], "ALERTS");
    create_settings_switch_row(group_alerts, "Haptic feedback", g_settings.haptic_enabled,
                               haptic_changed_cb);
    create_settings_switch_row(group_alerts, "Sound alert", g_settings.sound_enabled,
                               sound_changed_cb);

    settings_active_tab = 0;
    settings_show_tab(0);

    create_footer_back_btn(screen_settings);
}

static void create_connection_screen(void)
{
    screen_connection = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_connection);
    create_sub_header(screen_connection, "Bluetooth / OBD2", LV_SYMBOL_BLUETOOTH);
    bt_settings_ui_create(screen_connection);
    create_footer_back_btn(screen_connection);
}

static void create_about_screen(void)
{
    screen_about = lv_obj_create(NULL);
    ui_subscreen_prepare(screen_about);
    create_sub_header(screen_about, "About", LV_SYMBOL_LIST);

    lv_obj_t *app_title = lv_label_create(screen_about);
    lv_label_set_text(app_title, APP_NAME);
    lv_obj_set_pos(app_title, UI_SUB_PAD, UI_HEADER_H + 10);
    lv_obj_set_width(app_title, UI_SUB_CONTENT_W);
    lv_obj_set_style_text_align(app_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(app_title, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(app_title, color_text, 0);
    lv_label_set_long_mode(app_title, LV_LABEL_LONG_WRAP);

    lv_obj_t *version = lv_label_create(screen_about);
    lv_label_set_text_fmt(version, "v%s · ESP32-S3 · Round LCD", APP_VERSION);
    lv_obj_set_pos(version, UI_SUB_PAD, UI_HEADER_H + 44);
    lv_obj_set_width(version, UI_SUB_CONTENT_W);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(version, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(version, color_accent, 0);

    lv_obj_t *info_card = lv_obj_create(screen_about);
    lv_obj_set_pos(info_card, UI_SUB_PAD, UI_HEADER_H + 78);
    lv_obj_set_size(info_card, UI_SUB_CONTENT_W, UI_SUB_ROW_H * 4 + 24);
    lv_obj_add_style(info_card, style_get_card(), 0);
    lv_obj_remove_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(info_card, 12, 0);
    lv_obj_set_style_pad_row(info_card, 4, 0);
    lv_obj_set_flex_flow(info_card, LV_FLEX_FLOW_COLUMN);

    const char *labels[] = {"Hardware", "Display", "Touch", "Protocol"};
    const char *values[] = {
        "ESP32-S3-Touch-LCD-2.1",
        "480x480 ST7701 round",
        "CST820 capacitive",
        "ELM327 / Bluetooth BLE"
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_obj_create(info_card);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), UI_SUB_ROW_H);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                             LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, color_text_dim, 0);
        lv_obj_set_width(lbl, 100);

        lv_obj_t *val = lv_label_create(row);
        lv_label_set_text(val, values[i]);
        lv_obj_set_flex_grow(val, 1);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(val, UI_FONT_SM, 0);
        lv_obj_set_style_text_color(val, color_text, 0);
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    }

    create_footer_back_btn(screen_about);
}
