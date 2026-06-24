#pragma once

#include "lvgl.h"
#include "vehicle_data.h"
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

lv_obj_t *screen_splash_create(lv_obj_t *parent);
void screen_splash_start(lv_obj_t *splash, lv_timer_cb_t on_finish);

void screen_connect_create(lv_obj_t *parent);
void screen_dash_create(lv_obj_t *parent);
void screen_grid_create(lv_obj_t *parent);
void screen_settings_create(lv_obj_t *parent);

void screen_connect_update(const vehicle_data_snapshot_t *snap);
void screen_dash_update(bool connected, const vehicle_data_snapshot_t *snap);
void screen_grid_update(const vehicle_data_snapshot_t *snap);
void screen_settings_update(const vehicle_data_snapshot_t *snap);
