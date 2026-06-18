#pragma once

#include "lvgl.h"
#include <stdbool.h>

enum {
    UI_TAB_CONNECT = 0,
    UI_TAB_DASH,
    UI_TAB_GRID,
    UI_TAB_SETTINGS,
    UI_TAB_COUNT,
};

void ui_init(void);
void ui_start_update_timer(void);
void ui_show_dash(void);
int ui_get_active_tab(void);
bool ui_is_obd_connected(void);

void screen_connect_create(lv_obj_t *parent);
void screen_dash_create(lv_obj_t *parent);
void screen_grid_create(lv_obj_t *parent);
void screen_settings_create(lv_obj_t *parent);

void screen_connect_update(void);
void screen_dash_update(bool connected);
void screen_grid_update(void);
void screen_settings_update(void);
