#pragma once

#include "lvgl.h"

/* LVGL v9: LV_COLOR_MAKE is a brace initializer; use lv_color_hex for variables */
#define APP_COLOR_RGB(r, g, b) lv_color_hex((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

/* Theme color types */
typedef enum {
    THEME_PRIMARY = 0,
    THEME_SECONDARY,
    THEME_ACCENT,
    THEME_SUCCESS,
    THEME_WARNING,
    THEME_DANGER,
    THEME_TEXT,
    THEME_DIM
} theme_color_type_t;

/* Workshop-at-dusk theme — aligned with ui-demo.html */
extern lv_color_t color_primary;
extern lv_color_t color_secondary;
extern lv_color_t color_accent;
extern lv_color_t color_success;
extern lv_color_t color_warning;
extern lv_color_t color_danger;
extern lv_color_t color_text;
extern lv_color_t color_text_dim;
extern lv_color_t color_bg_dark;
extern lv_color_t color_card_bg;
extern lv_color_t color_card_border;

void styles_init(uint8_t theme_mode);

lv_style_t *style_get_bg_dark(void);
lv_style_t *style_get_card(void);
lv_style_t *style_get_card_pressed(void);
lv_style_t *style_get_btn_primary(void);
lv_style_t *style_get_btn_secondary(void);
lv_style_t *style_get_text_title(void);
lv_style_t *style_get_text_normal(void);
lv_style_t *style_get_text_dim(void);
lv_style_t *style_get_text_accent(void);
lv_style_t *style_get_toggle(void);
lv_style_t *style_get_slider(void);

lv_color_t color_get_gauge_normal(uint16_t value, uint16_t max);
lv_color_t color_get_theme(uint8_t theme_type);
