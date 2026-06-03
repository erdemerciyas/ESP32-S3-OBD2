#pragma once

#include "lvgl.h"
#include "ui_fonts.h"
#include "connectivity.h"

/**
 * Create a centered icon label (LVGL Font Awesome symbols).
 * Parent should be the card/button container.
 */
lv_obj_t *ui_icon_create(lv_obj_t *parent, const char *symbol, const lv_font_t *font);

/** Apply icon font + accent color to an existing label. */
void ui_icon_style(lv_obj_t *label, const lv_font_t *font);

/** WiFi link indicator levels for HUD / menu / connection screen */
typedef enum {
    UI_WIFI_IND_OFF = 0,
    UI_WIFI_IND_AP,
    UI_WIFI_IND_TCP,
    UI_WIFI_IND_OBD,
} ui_wifi_ind_level_t;

ui_wifi_ind_level_t ui_wifi_ind_level_from(bool wifi_ap_up, bool wifi_tcp_up,
                                           connectivity_state_t conn_state);

/** Top-right WiFi status icon (LV_SYMBOL_WIFI), non-clickable. */
lv_obj_t *ui_wifi_ind_create(lv_obj_t *parent, int x_ofs, int y_ofs);

void ui_wifi_ind_apply(lv_obj_t *icon, ui_wifi_ind_level_t level);
