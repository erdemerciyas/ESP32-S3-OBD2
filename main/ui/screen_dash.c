#include "ui.h"
#include "theme.h"
#include "bsp.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include <stdio.h>

static lv_obj_t *s_main_arc;
static lv_obj_t *s_main_value;
static lv_obj_t *s_main_unit;
static lv_obj_t *s_sub_labels[3];
static lv_obj_t *s_sub_name0;      /* name label of first stats cell */
static lv_obj_t *s_bt_icon;
static uint32_t  s_last_click_ms;  /* for double-tap detection */

/* --- Buzzer alert state --- */
#define ALERT_BEEP_MS     400       /* single beep duration */
#define ALERT_REPEAT_MS   30000     /* re-beep interval while critical */

static bool     s_alert_beeping;    /* buzzer currently on */
static uint32_t s_alert_beep_start; /* when current beep started */
static uint32_t s_alert_last_beep;  /* when last beep ended */
static bool     s_alert_was_crit;   /* was temp critical last check */

static void alert_check(float coolant)
{
    uint32_t now = lv_tick_get();
    bool is_crit = (vehicle_data_coolant_level(coolant) == THRESHOLD_CRIT);

    /* Stop active beep if duration elapsed */
    if (s_alert_beeping && (now - s_alert_beep_start) >= ALERT_BEEP_MS) {
        bsp_buzzer_off();
        s_alert_beeping = false;
        s_alert_last_beep = now;
    }

    if (is_crit) {
        if (!s_alert_was_crit) {
            /* Newly critical — beep immediately */
            bsp_buzzer_on();
            s_alert_beeping = true;
            s_alert_beep_start = now;
        } else if (!s_alert_beeping && (now - s_alert_last_beep) >= ALERT_REPEAT_MS) {
            /* Still critical, 30s passed — re-beep */
            bsp_buzzer_on();
            s_alert_beeping = true;
            s_alert_beep_start = now;
        }
    } else {
        /* Normal — stop any active beep, reset state */
        if (s_alert_beeping) {
            bsp_buzzer_off();
            s_alert_beeping = false;
        }
    }
    s_alert_was_crit = is_crit;
}

#define DOUBLE_TAP_MS  350

static void gauge_double_tap_cb(lv_event_t *e)
{
    uint32_t now = lv_tick_get();
    if (now - s_last_click_ms <= DOUBLE_TAP_MS) {
        /* Double-tap detected — toggle RPM ↔ Speed */
        vehicle_data_t *vd = vehicle_data_get();
        vd->center_gauge_rpm = !vd->center_gauge_rpm;
        s_last_click_ms = 0;  /* reset to avoid triple-tap */
    } else {
        s_last_click_ms = now;
    }
}

void screen_dash_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    s_bt_icon = lv_label_create(parent);
    lv_label_set_text(s_bt_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(s_bt_icon, t->font_md, 0);
    lv_obj_set_style_text_color(s_bt_icon, t->text_dim, 0);
    lv_obj_align(s_bt_icon, LV_ALIGN_TOP_RIGHT, -12, UI_PAD_TOP - 4);
    lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);

    /* Gauge: absolutely centered on screen (ignore content padding) */
    lv_obj_t *gauge_wrap = lv_obj_create(parent);
    lv_obj_set_size(gauge_wrap, UI_GAUGE_SZ, UI_GAUGE_SZ);
    lv_obj_set_style_bg_opa(gauge_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge_wrap, 0, 0);
    lv_obj_set_style_pad_all(gauge_wrap, 0, 0);
    lv_obj_clear_flag(gauge_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gauge_wrap, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_align(gauge_wrap, LV_ALIGN_CENTER, 0, -20);  /* above center for stats room */
    lv_obj_add_event_cb(gauge_wrap, gauge_double_tap_cb, LV_EVENT_CLICKED, NULL);

    s_main_arc = lv_arc_create(gauge_wrap);
    lv_obj_set_size(s_main_arc, UI_GAUGE_SZ, UI_GAUGE_SZ);
    lv_arc_set_rotation(s_main_arc, 135);
    lv_arc_set_bg_angles(s_main_arc, 0, 270);
    lv_arc_set_range(s_main_arc, 0, vehicle_profile_get()->rpm_max);
    lv_arc_set_value(s_main_arc, 0);
    lv_obj_remove_style(s_main_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_main_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_main_arc, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_style_arc_color(s_main_arc, t->primary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_main_arc, t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_main_arc, UI_GAUGE_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_main_arc, UI_GAUGE_ARC_W, LV_PART_INDICATOR);
    lv_obj_center(s_main_arc);

    s_main_value = lv_label_create(gauge_wrap);
    lv_label_set_text(s_main_value, "0");
    lv_obj_set_style_text_font(s_main_value, t->font_value, 0);
    lv_obj_set_style_text_color(s_main_value, t->text, 0);
    lv_obj_align(s_main_value, LV_ALIGN_CENTER, 0, -24);

    s_main_unit = lv_label_create(gauge_wrap);
    lv_label_set_text(s_main_unit, "rpm");
    lv_obj_set_style_text_font(s_main_unit, t->font_md, 0);
    lv_obj_set_style_text_color(s_main_unit, t->primary, 0);
    lv_obj_align_to(s_main_unit, s_main_value, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    lv_obj_t *stats_row = theme_create_flex_row(parent);
    lv_obj_set_width(stats_row, LV_PCT(100));
    lv_obj_set_height(stats_row, UI_STAT_H);
    lv_obj_add_flag(stats_row, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(stats_row, LV_ALIGN_BOTTOM_MID, 0, -(UI_STAT_LIFT + UI_DOT_H));

    const char *sub_names[] = { "Speed", "Temp", "Volt" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cell = theme_create_metric_cell(stats_row, sub_names[i], &s_sub_labels[i]);
        if (i == 0) {
            /* Save reference to first cell's name label for later swap */
            s_sub_name0 = lv_obj_get_child(cell, 0);
        }
    }
}

void screen_dash_update(bool connected)
{
    vehicle_data_t *vd = vehicle_data_get();
    const ui_theme_t *t = theme_get();
    const vehicle_profile_t *profile = vehicle_profile_get();
    bool rpm_mode = vd->center_gauge_rpm;

    if (connected) {
        lv_obj_clear_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_bt_icon, t->ok, 0);
    } else {
        lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
    }

    float main_val = rpm_mode ? vd->rpm : vehicle_data_convert_speed(vd->speed, vd->metric_units);
    int max_val = rpm_mode ? profile->rpm_max : (vd->metric_units ? 260 : 160);

    lv_arc_set_range(s_main_arc, 0, max_val);
    lv_arc_set_value(s_main_arc, (int32_t)main_val);

    if (rpm_mode) {
        lv_label_set_text_fmt(s_main_value, "%d", (int)vd->rpm);
        lv_label_set_text(s_main_unit, "rpm");
        lv_obj_set_style_arc_color(s_main_arc,
            theme_rpm_gradient_color(vd->rpm), LV_PART_INDICATOR);
    } else {
        lv_label_set_text_fmt(s_main_value, "%d", (int)main_val);
        lv_label_set_text(s_main_unit, vehicle_data_speed_unit(vd->metric_units));
        lv_obj_set_style_arc_color(s_main_arc,
            theme_speed_gradient_color(vd->speed), LV_PART_INDICATOR);
    }

    char buf[24];

    /* Bottom-left cell swaps with center gauge */
    if (rpm_mode) {
        /* Center = RPM, bottom-left = Speed */
        lv_label_set_text(s_sub_name0, "Speed");
        snprintf(buf, sizeof(buf), "%.0f",
                 vehicle_data_convert_speed(vd->speed, vd->metric_units));
    } else {
        /* Center = Speed, bottom-left = RPM */
        lv_label_set_text(s_sub_name0, "RPM");
        snprintf(buf, sizeof(buf), "%.0f", vd->rpm);
    }
    lv_label_set_text(s_sub_labels[0], buf);

    snprintf(buf, sizeof(buf), "%.0f%s",
             vehicle_data_convert_temp(vd->coolant, vd->metric_units),
             vehicle_data_temp_unit(vd->metric_units));
    lv_obj_set_style_text_color(s_sub_labels[1],
        theme_threshold_color(vehicle_data_coolant_level(vd->coolant)), 0);
    lv_label_set_text(s_sub_labels[1], buf);

    snprintf(buf, sizeof(buf), vd->voltage > 0.1f ? "%.2fV" : "--V", vd->voltage);
    lv_obj_set_style_text_color(s_sub_labels[2],
        vd->voltage > 0.1f ? theme_threshold_color(vehicle_data_voltage_level(vd->voltage)) : t->text_dim, 0);
    lv_label_set_text(s_sub_labels[2], buf);

    /* Check temperature alert */
    if (connected) {
        alert_check(vd->coolant);
    }
}
