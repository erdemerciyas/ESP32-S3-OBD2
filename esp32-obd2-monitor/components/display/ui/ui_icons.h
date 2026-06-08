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



/** Connection indicator levels for HUD / menu / connection screen */

typedef enum {

    UI_CONN_IND_OFF = 0,

    UI_CONN_IND_LINK,

    UI_CONN_IND_SERIAL,

    UI_CONN_IND_OBD,

} ui_conn_ind_level_t;



/** Backward-compatible aliases */

typedef ui_conn_ind_level_t ui_wifi_ind_level_t;

#define UI_WIFI_IND_OFF    UI_CONN_IND_OFF

#define UI_WIFI_IND_AP     UI_CONN_IND_LINK

#define UI_WIFI_IND_TCP    UI_CONN_IND_SERIAL

#define UI_WIFI_IND_OBD    UI_CONN_IND_OBD



ui_conn_ind_level_t ui_conn_ind_level_from(bool link_up, bool serial_up,

                                           connectivity_state_t conn_state);



static inline ui_wifi_ind_level_t ui_wifi_ind_level_from(bool wifi_ap_up, bool wifi_tcp_up,

                                                         connectivity_state_t conn_state)

{

    return ui_conn_ind_level_from(wifi_ap_up, wifi_tcp_up, conn_state);

}



/** Top-right connection status icon, non-clickable. */
lv_obj_t *ui_conn_ind_create(lv_obj_t *parent, int x_ofs, int y_ofs);

/** Centered above gauge pagination dots on the main screen. */
lv_obj_t *ui_conn_ind_create_gauge_hud(lv_obj_t *parent);



static inline lv_obj_t *ui_wifi_ind_create(lv_obj_t *parent, int x_ofs, int y_ofs)

{

    return ui_conn_ind_create(parent, x_ofs, y_ofs);

}



void ui_conn_ind_apply(lv_obj_t *icon, ui_conn_ind_level_t level);



static inline void ui_wifi_ind_apply(lv_obj_t *icon, ui_wifi_ind_level_t level)

{

    ui_conn_ind_apply(icon, level);

}

