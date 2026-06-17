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
#define UI_PAD_BOT      6
#define UI_GAP          6

/* Round-panel layout (visible circle R = UI_VIEWPORT_SZ / 2, center in tab) */
#define UI_CIRCLE_CY          (UI_VIEWPORT_SZ / 2)
#define UI_SAFE_MARGIN        10
#define UI_GAUGE_ARC_W        18
#define UI_GAUGE_SZ           (UI_VIEWPORT_SZ - UI_GAUGE_ARC_W)
#define UI_GAUGE_Y_OFF        -10   /* slight lift; keep inside tab bounds */
#define UI_STAT_H             68
#define UI_STAT_BOTTOM_OFF    68
#define UI_BT_ICON_Y          20
#define UI_FLOAT_BOTTOM_Y     (-(UI_STAT_BOTTOM_OFF))
#define UI_SCAN_ARC_SZ        142
#define UI_BTN_H              42
#define UI_SETTING_ROW_H      40
#define UI_SETTING_GAP        6

lv_coord_t ui_chord_width_at_y(lv_coord_t y_tab);
lv_coord_t theme_safe_width(lv_coord_t y_top, lv_coord_t y_bottom);

typedef struct {
    lv_color_t bg;
    lv_color_t surface;
    lv_color_t surface_hi;
    lv_color_t primary;
    lv_color_t secondary;
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
void theme_apply_screen(lv_obj_t *obj);
void theme_apply_bg(lv_obj_t *obj);
void theme_apply_content(lv_obj_t *obj);
void theme_apply_card(lv_obj_t *obj);
lv_obj_t *theme_create_header(lv_obj_t *parent, const char *title);
lv_obj_t *theme_create_metric_cell(lv_obj_t *parent, const char *label, lv_obj_t **value_out);
lv_obj_t *theme_create_flex_row(lv_obj_t *parent);
lv_obj_t *theme_create_flex_col(lv_obj_t *parent, bool grow);
