#include "ui.h"
#include "theme.h"
#include "bsp.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static lv_obj_t *s_bt_icon;
static lv_obj_t *s_profile_lbl;
static lv_obj_t *s_main_arc;
static lv_obj_t *s_redline_arc;
static lv_obj_t *s_needle;
static lv_obj_t *s_main_value;
static lv_obj_t *s_main_unit;
static lv_obj_t *s_hub;
static lv_obj_t *s_value_disc;
static lv_obj_t *s_shift_seg[UI_SHIFT_LIGHT_SEGS];
static lv_obj_t *s_data_cells[3];
static lv_obj_t *s_data_names[3];
static lv_obj_t *s_data_values[3];
static lv_obj_t *s_data_units[3];
static lv_point_t s_needle_points[2];
static uint32_t  s_last_click_ms;

/* Previous state caches to skip redundant LVGL updates */
static int32_t  s_prev_max_val = -1;
static float    s_prev_main_val = -1.0f;
static bool     s_prev_rpm_mode = true;
static bool     s_prev_connected = false;
static char     s_prev_profile_name[VEHICLE_PROFILE_NAME_LEN] = "";
static char     s_prev_data_name[3][8];
static char     s_prev_data_val[3][16];
static char     s_prev_data_unit[3][8];
static threshold_level_t s_prev_data_lvl[3] = { THRESHOLD_OK, THRESHOLD_OK, THRESHOLD_OK };
static lv_color_t s_prev_arc_color;

/* Buzzer alert state */
#define ALERT_BEEP_MS     400
#define ALERT_REPEAT_MS   30000

static bool     s_alert_beeping;
static uint32_t s_alert_beep_start;
static uint32_t s_alert_last_beep;
static bool     s_alert_was_crit;

static void alert_check(float coolant)
{
    uint32_t now = lv_tick_get();
    bool is_crit = (vehicle_data_coolant_level(coolant) == THRESHOLD_CRIT);

    if (s_alert_beeping && (now - s_alert_beep_start) >= ALERT_BEEP_MS) {
        bsp_buzzer_off();
        s_alert_beeping = false;
        s_alert_last_beep = now;
    }

    if (is_crit) {
        if (!s_alert_was_crit) {
            bsp_buzzer_on();
            s_alert_beeping = true;
            s_alert_beep_start = now;
        } else if (!s_alert_beeping && (now - s_alert_last_beep) >= ALERT_REPEAT_MS) {
            bsp_buzzer_on();
            s_alert_beeping = true;
            s_alert_beep_start = now;
        }
    } else {
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
    (void)e;
    uint32_t now = lv_tick_get();
    if (now - s_last_click_ms <= DOUBLE_TAP_MS) {
        vehicle_data_t *vd = vehicle_data_get();
        vd->center_gauge_rpm = !vd->center_gauge_rpm;
        s_last_click_ms = 0;
    } else {
        s_last_click_ms = now;
    }
}

static void data_pill_update(int idx, const char *name, const char *val,
                             const char *unit, threshold_level_t lvl,
                             const ui_theme_t *t)
{
    if (strcmp(name, s_prev_data_name[idx]) != 0) {
        strncpy(s_prev_data_name[idx], name, sizeof(s_prev_data_name[idx]));
        lv_label_set_text(s_data_names[idx], name);
    }
    if (strcmp(val, s_prev_data_val[idx]) != 0) {
        strncpy(s_prev_data_val[idx], val, sizeof(s_prev_data_val[idx]));
        lv_label_set_text(s_data_values[idx], val);
    }
    if (strcmp(unit, s_prev_data_unit[idx]) != 0) {
        strncpy(s_prev_data_unit[idx], unit, sizeof(s_prev_data_unit[idx]));
        lv_label_set_text(s_data_units[idx], unit);
    }
    if (lvl != s_prev_data_lvl[idx]) {
        s_prev_data_lvl[idx] = lvl;
        lv_color_t c = theme_threshold_color(lvl);
        lv_obj_set_style_border_color(s_data_cells[idx], c, 0);
        lv_obj_set_style_text_color(s_data_values[idx], c, 0);
    }
}

static lv_obj_t *create_data_pill(lv_obj_t *parent, const char *name,
                                   lv_obj_t **value_out, lv_obj_t **unit_out,
                                   const ui_theme_t *t)
{
    lv_obj_t *pill = lv_obj_create(parent);
    theme_apply_card(pill);
    lv_obj_set_style_bg_color(pill, t->surface, 0);
    lv_obj_set_style_border_width(pill, 2, 0);
    lv_obj_set_style_border_color(pill, t->border, 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 8, 0);
    lv_obj_set_width(pill, LV_SIZE_CONTENT);
    lv_obj_set_height(pill, LV_PCT(100));
    lv_obj_set_flex_grow(pill, 1);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(pill, 6, 0);
    lv_obj_set_style_pad_hor(pill, 6, 0);
    lv_obj_set_style_pad_row(pill, 4, 0);

    lv_obj_t *nm = lv_label_create(pill);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_font(nm, t->font_sm, 0);
    lv_obj_set_style_text_color(nm, t->text_dim, 0);

    /* Keep value and unit on one row so the pill fits in 48 px. */
    lv_obj_t *val_row = lv_obj_create(pill);
    lv_obj_set_width(val_row, LV_PCT(100));
    lv_obj_set_height(val_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(val_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(val_row, 0, 0);
    lv_obj_set_style_pad_all(val_row, 0, 0);
    lv_obj_set_flex_flow(val_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(val_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *val = lv_label_create(val_row);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, t->font_data, 0);
    lv_obj_set_style_text_color(val, t->primary, 0);

    lv_obj_t *unit = lv_label_create(val_row);
    lv_label_set_text(unit, "");
    lv_obj_set_style_text_font(unit, t->font_sm, 0);
    lv_obj_set_style_text_color(unit, t->text_dim, 0);
    lv_obj_set_style_pad_left(unit, 4, 0);

    if (value_out) {
        *value_out = val;
    }
    if (unit_out) {
        *unit_out = unit;
    }

    return pill;
}

void screen_dash_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    /* Dashboard occupies the full 460x460 viewport; remove the generic
     * content padding so that every element is placed in absolute pixels. */
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_top(parent, 0, 0);
    lv_obj_set_style_pad_bottom(parent, 0, 0);
    lv_obj_set_style_pad_hor(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);

    /* --- Top status bar: profile (left) + BT (right) --- */
    {
        lv_coord_t status_y = UI_DASH_STATUS_TOP + UI_DASH_STATUS_H;
        lv_coord_t status_w = theme_safe_width(status_y, status_y);
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_set_size(bar, status_w, UI_DASH_STATUS_H);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_pad_hor(bar, UI_MARGIN, 0);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, UI_DASH_STATUS_TOP);

        s_profile_lbl = lv_label_create(bar);
        lv_label_set_text(s_profile_lbl, "");
        lv_label_set_long_mode(s_profile_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_profile_lbl, LV_PCT(80));
        lv_obj_set_style_text_align(s_profile_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s_profile_lbl, t->font_sm, 0);
        lv_obj_set_style_text_color(s_profile_lbl, t->text_dim, 0);

        s_bt_icon = lv_label_create(bar);
        lv_label_set_text(s_bt_icon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_font(s_bt_icon, t->font_sm, 0);
        lv_obj_set_style_text_color(s_bt_icon, t->primary, 0);
        lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(s_bt_icon, LV_ALIGN_RIGHT_MID, -UI_MARGIN, 0);

        lv_obj_move_foreground(bar);
    }

    /* --- Center gauge container --- */
    lv_obj_t *gauge_wrap = lv_obj_create(parent);
    lv_obj_set_size(gauge_wrap, UI_GAUGE_SZ, UI_GAUGE_SZ);
    lv_obj_set_style_bg_opa(gauge_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge_wrap, 0, 0);
    lv_obj_set_style_pad_all(gauge_wrap, 0, 0);
    lv_obj_clear_flag(gauge_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gauge_wrap, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_flag(gauge_wrap, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(gauge_wrap, LV_ALIGN_TOP_MID, 0, UI_GAUGE_TOP);
    lv_obj_add_event_cb(gauge_wrap, gauge_double_tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_background(gauge_wrap);

    /* Shift light strip at top of gauge */
    {
        lv_coord_t total_w = (UI_GAUGE_SZ * 55) / 100;
        lv_coord_t seg_w = (total_w - (UI_SHIFT_LIGHT_SEGS - 1) * UI_SHIFT_LIGHT_GAP) / UI_SHIFT_LIGHT_SEGS;
        lv_obj_t *strip = lv_obj_create(gauge_wrap);
        lv_obj_set_size(strip, total_w, UI_SHIFT_LIGHT_H + 6);
        lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(strip, 0, 0);
        lv_obj_set_style_pad_all(strip, 0, 0);
        lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(strip, UI_SHIFT_LIGHT_GAP, 0);
        lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(strip, LV_ALIGN_TOP_MID, 0, UI_DASH_STATUS_TOP + UI_DASH_STATUS_H + UI_GAP_MD);

        for (int i = 0; i < UI_SHIFT_LIGHT_SEGS; i++) {
            s_shift_seg[i] = lv_obj_create(strip);
            lv_obj_set_size(s_shift_seg[i], seg_w, UI_SHIFT_LIGHT_H);
            lv_obj_set_style_radius(s_shift_seg[i], 2, 0);
            lv_obj_set_style_border_width(s_shift_seg[i], 0, 0);
            lv_obj_set_style_bg_color(s_shift_seg[i], t->arc_bg, 0);
            lv_obj_set_style_bg_opa(s_shift_seg[i], LV_OPA_50, 0);
            lv_obj_set_style_shadow_width(s_shift_seg[i], 0, 0);
        }
    }

    /* Background arc */
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
    lv_obj_set_style_arc_rounded(s_main_arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_main_arc, false, LV_PART_INDICATOR);
    lv_obj_align(s_main_arc, LV_ALIGN_CENTER, 0, UI_GAUGE_VALUE_Y_OFF);

    /* Redline zone arc */
    s_redline_arc = lv_arc_create(gauge_wrap);
    lv_obj_set_size(s_redline_arc, UI_GAUGE_SZ, UI_GAUGE_SZ);
    lv_arc_set_rotation(s_redline_arc, 135);
    lv_arc_set_bg_angles(s_redline_arc, 270, 270);
    lv_arc_set_range(s_redline_arc, 0, 100);
    lv_arc_set_value(s_redline_arc, 100);
    lv_obj_remove_style(s_redline_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_redline_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_redline_arc, t->crit, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_redline_arc, t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_redline_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_redline_arc, UI_GAUGE_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_redline_arc, false, LV_PART_INDICATOR);
    lv_obj_align(s_redline_arc, LV_ALIGN_CENTER, 0, UI_GAUGE_VALUE_Y_OFF);

    /* Needle line */
    s_needle = lv_line_create(gauge_wrap);
    lv_obj_set_style_line_width(s_needle, UI_NEEDLE_W, 0);
    lv_obj_set_style_line_color(s_needle, t->secondary, 0);
    lv_obj_set_style_line_rounded(s_needle, true, 0);
    s_needle_points[0].x = UI_GAUGE_SZ / 2;
    s_needle_points[0].y = UI_GAUGE_SZ / 2 + UI_GAUGE_VALUE_Y_OFF;
    s_needle_points[1].x = UI_GAUGE_SZ / 2;
    s_needle_points[1].y = UI_GAUGE_SZ / 2 + UI_GAUGE_VALUE_Y_OFF;
    lv_line_set_points(s_needle, s_needle_points, 2);
    lv_obj_add_flag(s_needle, LV_OBJ_FLAG_FLOATING);

    /* Central value disc: invisible mask that hides the needle behind the
     * large RPM digits so the value stays readable. Same color as the
     * screen background, no border, so it does not look like an extra circle. */
    s_value_disc = lv_obj_create(gauge_wrap);
    lv_coord_t disc_sz = (UI_GAUGE_SZ * 50) / 100;
    lv_obj_set_size(s_value_disc, disc_sz, disc_sz);
    lv_obj_align(s_value_disc, LV_ALIGN_CENTER, 0, UI_GAUGE_VALUE_Y_OFF);
    lv_obj_set_style_radius(s_value_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_value_disc, t->bg, 0);
    lv_obj_set_style_bg_opa(s_value_disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_value_disc, 0, 0);
    lv_obj_set_style_pad_all(s_value_disc, 0, 0);
    lv_obj_clear_flag(s_value_disc, LV_OBJ_FLAG_SCROLLABLE);

    /* Center digital value */
    s_main_value = lv_label_create(gauge_wrap);
    lv_label_set_text(s_main_value, "0");
    lv_obj_set_style_text_font(s_main_value, t->font_value, 0);
    lv_obj_set_style_text_color(s_main_value, t->text, 0);
    lv_obj_align(s_main_value, LV_ALIGN_CENTER, 0, UI_GAUGE_VALUE_Y_OFF);

    s_main_unit = lv_label_create(gauge_wrap);
    lv_label_set_text(s_main_unit, "rpm");
    lv_obj_set_style_text_font(s_main_unit, t->font_sm, 0);
    lv_obj_set_style_text_color(s_main_unit, t->text_dim, 0);
    lv_obj_align_to(s_main_unit, s_main_value, LV_ALIGN_OUT_BOTTOM_MID, 0, -4);

    /* Center hub cap over needle pivot */
    s_hub = lv_obj_create(gauge_wrap);
    lv_obj_set_size(s_hub, 14, 14);
    lv_obj_align(s_hub, LV_ALIGN_CENTER, 0, UI_GAUGE_VALUE_Y_OFF);
    lv_obj_set_style_radius(s_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_hub, t->surface, 0);
    lv_obj_set_style_bg_opa(s_hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_hub, t->primary, 0);
    lv_obj_set_style_border_width(s_hub, 2, 0);
    lv_obj_set_style_border_opa(s_hub, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_hub, 0, 0);
    lv_obj_clear_flag(s_hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_hub, LV_OBJ_FLAG_HIDDEN);

    /* Bottom data strip: Speed | Coolant | Voltage */
    lv_obj_t *data_row = theme_create_flex_row(parent);
    {
        const lv_coord_t row_b = UI_VIEWPORT_SZ - UI_STAT_BOTTOM_OFF;
        const lv_coord_t row_t = row_b - UI_DASH_DATA_H;
        const lv_coord_t row_w = theme_safe_width(row_t, row_b);
        lv_obj_set_width(data_row, row_w);
        lv_obj_set_style_pad_column(data_row, UI_GAP_MD, 0);
    }
    lv_obj_set_height(data_row, UI_DASH_DATA_H);
    lv_obj_add_flag(data_row, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(data_row, LV_ALIGN_BOTTOM_MID, 0, UI_FLOAT_BOTTOM_Y);

    const char *data_names[] = { "Speed", "Temp", "Volt" };
    for (int i = 0; i < 3; i++) {
        s_data_cells[i] = create_data_pill(data_row, data_names[i],
                                           &s_data_values[i], &s_data_units[i], t);
        s_data_names[i] = lv_obj_get_child(s_data_cells[i], 0);
    }
    lv_obj_move_foreground(data_row);
}

void screen_dash_update(bool connected, const vehicle_data_snapshot_t *snap)
{
    const ui_theme_t *t = theme_get();
    const vehicle_profile_t *profile = vehicle_profile_get();
    bool rpm_mode = snap->center_gauge_rpm;

    /* Active profile name */
    if (strcmp(profile->display_name, s_prev_profile_name) != 0) {
        strncpy(s_prev_profile_name, profile->display_name,
                sizeof(s_prev_profile_name));
        lv_label_set_text(s_profile_lbl, profile->display_name);
    }

    /* BT icon */
    if (connected != s_prev_connected) {
        s_prev_connected = connected;
        if (connected) {
            lv_obj_clear_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    float main_val = rpm_mode ? snap->rpm
                              : vehicle_data_convert_speed(snap->speed, snap->metric_units);
    int max_val = rpm_mode ? profile->rpm_max
                           : (snap->metric_units ? 260 : 160);

    /* Arc range & redline zone */
    if (max_val != s_prev_max_val || rpm_mode != s_prev_rpm_mode) {
        s_prev_max_val = max_val;
        lv_arc_set_range(s_main_arc, 0, max_val);

        float redline_ratio;
        if (rpm_mode) {
            redline_ratio = (float)profile->rpm_redline / (float)max_val;
        } else {
            float redline = snap->metric_units ? 200.0f : 120.0f;
            redline_ratio = redline / (float)max_val;
        }
        if (redline_ratio < 0.0f) redline_ratio = 0.0f;
        if (redline_ratio > 1.0f) redline_ratio = 1.0f;
        int redline_start = (int)(redline_ratio * 270.0f);
        if (redline_start < 270) {
            lv_arc_set_bg_angles(s_redline_arc, redline_start, 270);
        } else {
            lv_arc_set_bg_angles(s_redline_arc, 270, 270);
        }
    }

    /* Unit on mode switch */
    if (rpm_mode != s_prev_rpm_mode) {
        s_prev_rpm_mode = rpm_mode;
        if (rpm_mode) {
            lv_label_set_text(s_main_unit, "rpm");
        } else {
            lv_label_set_text(s_main_unit, vehicle_data_speed_unit(snap->metric_units));
        }
    }

    /* Value arc, color, label, needle */
    if (main_val != s_prev_main_val) {
        s_prev_main_val = main_val;
        int main_int = (int)main_val;
        lv_arc_set_value(s_main_arc, main_int);
        lv_label_set_text_fmt(s_main_value, "%d", main_int);

        /* Needle angle: 135deg at min, 405deg (45deg) at max */
        float ratio = (max_val > 0) ? main_val / (float)max_val : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float angle_deg = 135.0f + ratio * 270.0f;
        float angle_rad = angle_deg * M_PI / 180.0f;
        lv_coord_t cx = UI_GAUGE_SZ / 2;
        lv_coord_t cy = UI_GAUGE_SZ / 2 + UI_GAUGE_VALUE_Y_OFF;
        lv_coord_t len = (UI_GAUGE_SZ / 2) - UI_GAUGE_ARC_W - 18;
        s_needle_points[1].x = cx + (lv_coord_t)(len * cosf(angle_rad));
        s_needle_points[1].y = cy + (lv_coord_t)(len * sinf(angle_rad));
        lv_line_set_points(s_needle, s_needle_points, 2);
    }

    /* Arc color */
    lv_color_t arc_color = rpm_mode
        ? theme_rpm_gradient_color(snap->rpm)
        : theme_speed_gradient_color(snap->speed);
    if (arc_color.full != s_prev_arc_color.full) {
        s_prev_arc_color = arc_color;
        lv_obj_set_style_arc_color(s_main_arc, arc_color, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(s_needle, arc_color, 0);
        lv_obj_set_style_border_color(s_hub, arc_color, 0);
        lv_obj_set_style_border_color(s_value_disc, arc_color, 0);
    }

    /* Shift light segments */
    {
        float rpm_ratio = (profile->rpm_max > 0) ? snap->rpm / (float)profile->rpm_max : 0.0f;
        uint32_t now = lv_tick_get();
        bool blink = (rpm_ratio >= 0.95f) && ((now / 120) & 1);
        for (int i = 0; i < UI_SHIFT_LIGHT_SEGS; i++) {
            float seg_ratio = 0.70f + (0.30f * i) / (UI_SHIFT_LIGHT_SEGS - 1);
            bool lit = rpm_ratio >= seg_ratio;
            lv_color_t c = lit ? theme_shift_light_color(seg_ratio) : t->arc_bg;
            lv_opa_t opa = lit ? LV_OPA_COVER : LV_OPA_30;
            /* Blink only the top 3 segments when deep in redline */
            if (blink && lit && i >= UI_SHIFT_LIGHT_SEGS - 3) {
                c = t->arc_bg;
                opa = LV_OPA_30;
            }
            lv_obj_set_style_bg_color(s_shift_seg[i], c, 0);
            lv_obj_set_style_bg_opa(s_shift_seg[i], opa, 0);
        }
    }

    /* Bottom data strip */
    char buf[24];

    /* Bottom-left cell toggles with the center gauge */
    if (rpm_mode) {
        float speed = vehicle_data_convert_speed(snap->speed, snap->metric_units);
        snprintf(buf, sizeof(buf), "%.0f", speed);
        data_pill_update(0, "Speed", buf,
                         vehicle_data_speed_unit(snap->metric_units),
                         THRESHOLD_OK, t);
    } else {
        snprintf(buf, sizeof(buf), "%.0f", snap->rpm);
        data_pill_update(0, "RPM", buf, "rpm",
                         THRESHOLD_OK, t);
    }

    /* Coolant */
    snprintf(buf, sizeof(buf), "%.0f",
             vehicle_data_convert_temp(snap->coolant, snap->metric_units));
    data_pill_update(1, "Temp", buf,
                     vehicle_data_temp_unit(snap->metric_units),
                     vehicle_data_coolant_level(snap->coolant), t);

    /* Voltage */
    if (snap->voltage > 0.1f) {
        snprintf(buf, sizeof(buf), "%.1f", snap->voltage);
        data_pill_update(2, "Volt", buf, "V",
                         vehicle_data_voltage_level(snap->voltage), t);
    } else {
        data_pill_update(2, "Volt", "--", "V",
                         THRESHOLD_OK, t);
    }

    /* Coolant critical alert */
    if (connected) {
        alert_check(snap->coolant);
    }
}
