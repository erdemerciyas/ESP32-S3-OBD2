#pragma once

#include "lvgl.h"
#include "vehicle_data.h"

/*
 * Waveshare ESP32-S3-Touch-LCD-2.1 — 480x480 round panel
 *
 * Visible circle diameter ≈ 460px (≈10px bezel mask per side).
 * Viewport is a centered square; corners sit under the round mask.
 */
#define UI_PANEL_D      480
#define UI_BEZEL        10
#define UI_VIEWPORT_SZ  (UI_PANEL_D - UI_BEZEL * 2)
#define UI_TAB_H        0
#define UI_DOT_H        18
#define UI_DOT_BAR_LIFT 24
#define UI_TAB_BAR_LIFT 0
#define UI_STAT_LIFT    18
#define UI_PAD_HOR      10
#define UI_PAD_TOP      28
#define UI_PAD_BOT      40   /* keep content above the dot navigation bar */
#define UI_GAP          6

/* 8 px grid layout system */
#define UI_GRID               8
#define UI_MARGIN             16    /* side / outer margin */
#define UI_GAP_SM             4
#define UI_GAP_MD             8
#define UI_GAP_LG             12
#define UI_GAP_XL             16

/* Round-panel layout (visible circle R = UI_VIEWPORT_SZ / 2, center in tab) */
#define UI_CIRCLE_CY          (UI_VIEWPORT_SZ / 2)
#define UI_SAFE_MARGIN        5

/* Dashboard layout — gauge fills the viewport, data strip overlays the
 * bottom arc, status bar sits just above the gauge.
 * Coordinates are relative to the 460x460 safe viewport.
 * The dot navigation bar occupies the bottom 42 px of the 480x480 screen,
 * so the data strip bottom is kept 40 px above the viewport bottom.       */
#define UI_DASH_STATUS_H      16
#define UI_DASH_STATUS_TOP    56
#define UI_DASH_DATA_H        64
#define UI_DASH_BOTTOM_RES    68
#define UI_DASH_GAP           4

#define UI_GAUGE_TOP          0
#define UI_DATA_TOP           (UI_VIEWPORT_SZ - UI_DASH_BOTTOM_RES - UI_DASH_DATA_H)
#define UI_GAUGE_SZ           UI_VIEWPORT_SZ

#define UI_GAUGE_ARC_W        8    /* Main indicator arc width */
#define UI_GAUGE_ACCENT_W     3    /* Outer accent ring width */
#define UI_GAUGE_VALUE_Y_OFF  0
#define UI_GAUGE_Y_OFF        0
#define UI_STAT_H             UI_DASH_DATA_H
#define UI_STAT_BOTTOM_OFF    (UI_VIEWPORT_SZ - UI_DATA_TOP - UI_DASH_DATA_H)
#define UI_FLOAT_BOTTOM_Y     (-(UI_STAT_BOTTOM_OFF))
#define UI_SCAN_ARC_SZ        160
#define UI_SCAN_ARC_W         12
#define UI_BTN_H              46
#define UI_HEADER_H           34
#define UI_SETTING_ROW_H      44
#define UI_SETTING_GAP        8
#define UI_SHIFT_LIGHT_H      10
#define UI_SHIFT_LIGHT_GAP    4
#define UI_SHIFT_LIGHT_SEGS   9
#define UI_NEEDLE_W           3
#define UI_TICK_MARK_LEN      10
#define UI_TICK_MINOR_LEN     5
#define UI_DATA_RADIUS        12

/* Gauge animation timing (ms) — LVGL ease-out interpolation.
 * RPM and Speed use the same duration so the center gauge and shift lights
 * always move in lockstep regardless of mode. */
#define UI_GAUGE_ANIM_RPM     80
#define UI_GAUGE_ANIM_SPEED   80

/* Data pill text smoothing — mini EMA alpha in UI layer.
 * 0.75 tracks real changes slightly faster than the old 0.70. */
#define UI_TEXT_SMOOTH_ALPHA  0.75f

lv_coord_t ui_chord_width_at_y(lv_coord_t y_tab);
lv_coord_t theme_safe_width(lv_coord_t y_top, lv_coord_t y_bottom);

typedef struct {
    lv_color_t bg;
    lv_color_t surface;
    lv_color_t surface_hi;
    lv_color_t primary;
    lv_color_t secondary;
    lv_color_t accent;
    lv_color_t text;
    lv_color_t text_dim;
    lv_color_t ok;
    lv_color_t warn;
    lv_color_t crit;
    lv_color_t arc_bg;
    lv_color_t border;
    const lv_font_t *font_sm;
    const lv_font_t *font_md;
    const lv_font_t *font_lg;
    const lv_font_t *font_xl;
    const lv_font_t *font_xxl;      /* 56px regular */
    const lv_font_t *font_value;    /* 94px bold for gauge value */
    const lv_font_t *font_data;
} ui_theme_t;

const ui_theme_t *theme_get(void);
lv_color_t theme_threshold_color(threshold_level_t level);
lv_color_t theme_rpm_gradient_color(float rpm);
lv_color_t theme_speed_gradient_color(float kmh);

/* Shift-light color based on RPM relative to redline (0.0..1.0+). */
lv_color_t theme_shift_light_color(float rpm_ratio);

void theme_apply_screen(lv_obj_t *obj);
void theme_apply_bg(lv_obj_t *obj);
void theme_apply_content(lv_obj_t *obj);
void theme_apply_card(lv_obj_t *obj);
void theme_apply_glass_card(lv_obj_t *obj);   /* Glass morphism data pill */
lv_obj_t *theme_create_header(lv_obj_t *parent, const char *title);
lv_obj_t *theme_create_metric_cell(lv_obj_t *parent, const char *label,
                                    lv_color_t accent, lv_obj_t **value_out,
                                    lv_obj_t **unit_out);
lv_obj_t *theme_create_stat_cell(lv_obj_t *parent, const char *label,
                                  lv_obj_t **value_out, lv_obj_t **unit_out);
void theme_apply_card_topline(lv_obj_t *obj, lv_color_t color);
void theme_apply_setting_row(lv_obj_t *obj, lv_color_t accent);
lv_obj_t *theme_create_flex_row(lv_obj_t *parent);
lv_obj_t *theme_create_flex_col(lv_obj_t *parent, bool grow);
