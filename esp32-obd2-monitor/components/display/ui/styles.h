#pragma once

#include "lvgl.h"

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

/* Racing theme colors - matching ui-demo.html */
extern lv_color_t color_primary;      /* #ff2d2d - Kırmızı */
extern lv_color_t color_secondary;    /* #ff6b35 - Turuncu */
extern lv_color_t color_accent;       /* #ffcc00 - Sarı */
extern lv_color_t color_success;      /* #00ff88 - Yeşil */
extern lv_color_t color_warning;      /* #ffaa00 - Turuncu-sarı */
extern lv_color_t color_danger;       /* #ff2d2d - Kırmızı */
extern lv_color_t color_text;         /* #ffffff - Beyaz */
extern lv_color_t color_text_dim;     /* #666666 - Gri */
extern lv_color_t color_bg_dark;      /* #0a0a0a - Koyu siyah */
extern lv_color_t color_card_bg;      /* #1a1a1a - Kart arka plan */
extern lv_color_t color_card_border;  /* #2a2a2a - Kart kenarlık */

void styles_init(void);

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
