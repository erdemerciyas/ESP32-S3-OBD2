#include "ui.h"
#include "theme.h"
#include "bsp.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_main_arc;
static lv_obj_t *s_main_value;
static lv_obj_t *s_main_unit;
static lv_obj_t *s_sub_labels[3];
static lv_obj_t *s_sub_cells[3];    /* cell containers for bg tint */
static lv_obj_t *s_sub_name0;      /* name label of first stats cell */
static lv_obj_t *s_bt_icon;
static uint32_t  s_last_click_ms;  /* for double-tap detection */

/* --- Performance: skip redundant LVGL redraws --- */
static int32_t  s_prev_max_val = -1;
static int32_t  s_prev_main_val = -1;
static bool     s_prev_rpm_mode = true;
static bool     s_prev_connected = false;
static char     s_prev_sub0[10] = "";
static char     s_prev_sub1[10] = "";
static char     s_prev_sub2[10] = "";
static lv_color_t s_prev_arc_color;

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
    lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_bt_icon, LV_ALIGN_TOP_MID, 0, UI_BT_ICON_Y);
    lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);

    /* Gauge: absolutely centered on screen (ignore content padding) */
    lv_obj_t *gauge_wrap = lv_obj_create(parent);
    lv_obj_set_size(gauge_wrap, UI_GAUGE_SZ, UI_GAUGE_SZ);
    lv_obj_set_style_bg_opa(gauge_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge_wrap, 0, 0);
    lv_obj_set_style_pad_all(gauge_wrap, 0, 0);
    lv_obj_clear_flag(gauge_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gauge_wrap, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_flag(gauge_wrap, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(gauge_wrap, LV_ALIGN_CENTER, 0, UI_GAUGE_Y_OFF);
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
    {
        const lv_coord_t stat_b = UI_VIEWPORT_SZ - UI_STAT_BOTTOM_OFF;
        const lv_coord_t stat_t = stat_b - UI_STAT_H;
        const lv_coord_t stat_w = theme_safe_width(stat_t, stat_b);
        lv_obj_set_width(stats_row, stat_w);
    }
    lv_obj_set_height(stats_row, UI_STAT_H);
    lv_obj_add_flag(stats_row, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(stats_row, LV_ALIGN_BOTTOM_MID, 0, UI_FLOAT_BOTTOM_Y);

    const char *sub_names[] = { "Speed", "Temp", "Volt" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cell = theme_create_metric_cell(stats_row, sub_names[i], &s_sub_labels[i]);
        s_sub_cells[i] = cell;
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

    /* --- BT icon: only update on state change --- */
    if (connected != s_prev_connected) {
        s_prev_connected = connected;
        if (connected) {
            lv_obj_clear_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(s_bt_icon, t->ok, 0);
        } else {
            lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    float main_val = rpm_mode ? vd->rpm : vehicle_data_convert_speed(vd->speed, vd->metric_units);
    int max_val = rpm_mode ? profile->rpm_max : (vd->metric_units ? 260 : 160);
    int32_t main_val_int = (int32_t)main_val;

    /* --- Arc range: only update on mode switch or unit toggle --- */
    if (max_val != s_prev_max_val) {
        s_prev_max_val = max_val;
        lv_arc_set_range(s_main_arc, 0, max_val);
    }

    /* --- Arc value & center label: only update on actual value change --- */
    if (main_val_int != s_prev_main_val) {
        s_prev_main_val = main_val_int;
        lv_arc_set_value(s_main_arc, main_val_int);
        lv_label_set_text_fmt(s_main_value, "%d", (int)main_val_int);
    }

    /* --- Arc color & gauge unit: only on mode switch --- */
    if (rpm_mode != s_prev_rpm_mode) {
        s_prev_rpm_mode = rpm_mode;
        if (rpm_mode) {
            lv_label_set_text(s_main_unit, "rpm");
        } else {
            lv_label_set_text(s_main_unit, vehicle_data_speed_unit(vd->metric_units));
        }
    }

    lv_color_t arc_color = rpm_mode
        ? theme_rpm_gradient_color(vd->rpm)
        : theme_speed_gradient_color(vd->speed);
    if (arc_color.full != s_prev_arc_color.full) {
        s_prev_arc_color = arc_color;
        lv_obj_set_style_arc_color(s_main_arc, arc_color, LV_PART_INDICATOR);
    }

    /* --- Bottom stats: only update when formatted value changes --- */
    char buf[24];

    /* Cell 0: swaps with center gauge */
    if (rpm_mode) {
        snprintf(buf, sizeof(buf), "%.0f",
                 vehicle_data_convert_speed(vd->speed, vd->metric_units));
    } else {
        snprintf(buf, sizeof(buf), "%.0f", vd->rpm);
    }
    if (strcmp(buf, s_prev_sub0) != 0) {
        strncpy(s_prev_sub0, buf, sizeof(s_prev_sub0));
        if (rpm_mode) {
            lv_label_set_text(s_sub_name0, "Speed");
        } else {
            lv_label_set_text(s_sub_name0, "RPM");
        }
        lv_label_set_text(s_sub_labels[0], buf);
    }

    /* Cell 1: coolant temp */
    snprintf(buf, sizeof(buf), "%.0f%s",
             vehicle_data_convert_temp(vd->coolant, vd->metric_units),
             vehicle_data_temp_unit(vd->metric_units));
    if (strcmp(buf, s_prev_sub1) != 0) {
        strncpy(s_prev_sub1, buf, sizeof(s_prev_sub1));
        threshold_level_t lvl = vehicle_data_coolant_level(vd->coolant);
        lv_color_t tint = theme_threshold_color(lvl);
        /* Subtle background tint: 15% opacity for normal/warn, 25% for crit */
        lv_opa_t opa = (lvl == THRESHOLD_CRIT) ? LV_OPA_30 : (lvl == THRESHOLD_WARN ? LV_OPA_20 : LV_OPA_10);
        lv_obj_set_style_bg_color(s_sub_cells[1], tint, 0);
        lv_obj_set_style_bg_opa(s_sub_cells[1], opa, 0);
        lv_obj_set_style_text_color(s_sub_labels[1], tint, 0);
        lv_label_set_text(s_sub_labels[1], buf);
    }

    /* Cell 2: voltage */
    snprintf(buf, sizeof(buf), vd->voltage > 0.1f ? "%.2fV" : "--V", vd->voltage);
    if (strcmp(buf, s_prev_sub2) != 0) {
        strncpy(s_prev_sub2, buf, sizeof(s_prev_sub2));
        threshold_level_t lvl = vehicle_data_voltage_level(vd->voltage);
        lv_color_t tint = theme_threshold_color(lvl);
        lv_opa_t opa = (lvl == THRESHOLD_CRIT) ? LV_OPA_30 : (lvl == THRESHOLD_WARN ? LV_OPA_20 : LV_OPA_10);
        lv_obj_set_style_bg_color(s_sub_cells[2], tint, 0);
        lv_obj_set_style_bg_opa(s_sub_cells[2], opa, 0);
        lv_obj_set_style_text_color(s_sub_labels[2],
            vd->voltage > 0.1f ? tint : t->text_dim, 0);
        lv_label_set_text(s_sub_labels[2], buf);
    }

    /* Check temperature alert */
    if (connected) {
        alert_check(vd->coolant);
    }
}
