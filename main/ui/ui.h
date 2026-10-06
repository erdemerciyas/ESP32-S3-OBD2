#pragma once

#include "lvgl.h"
#include "vehicle_data.h"
#include "imu_data.h"
#include <stdbool.h>

enum {
    UI_TAB_CONNECT = 0,
    UI_TAB_DASH,
    UI_TAB_GRID,
    UI_TAB_DTC,
    UI_TAB_GYRO,
    UI_TAB_SETTINGS,
    UI_TAB_COUNT,
};

/* Üst düzey görünümler: mod seçimi, OBD sekmeleri, navigasyon, performans. */
enum {
    UI_VIEW_HOME = 0,
    UI_VIEW_OBD,
    UI_VIEW_NAV,
    UI_VIEW_ROLL,
};

void ui_init(void);
void ui_show_view(int view);
void ui_start_update_timer(void);
void ui_show_dash(void);
void ui_show_tab(int tab);
int ui_get_active_tab(void);
bool ui_is_obd_connected(void);

lv_obj_t *screen_splash_create(lv_obj_t *parent);
void screen_splash_start(lv_obj_t *splash, lv_timer_cb_t on_finish);

void screen_connect_create(lv_obj_t *parent);
void screen_dash_create(lv_obj_t *parent);
void screen_grid_create(lv_obj_t *parent);
void screen_dtc_create(lv_obj_t *parent);
void screen_settings_create(lv_obj_t *parent);
void screen_gyro_create(lv_obj_t *parent);

void screen_connect_update(const vehicle_data_snapshot_t *snap);
void screen_dash_update(bool connected, const vehicle_data_snapshot_t *snap);
void screen_grid_update(const vehicle_data_snapshot_t *snap);
void screen_dtc_update(const vehicle_data_snapshot_t *snap);
void screen_settings_update(const vehicle_data_snapshot_t *snap);
void screen_gyro_update(const imu_snapshot_t *snap);

lv_obj_t *screen_home_create(lv_obj_t *parent);
lv_obj_t *screen_nav_create(lv_obj_t *parent);
void screen_home_update(void);
void screen_nav_update(void);
void screen_nav_next_page(void);
lv_obj_t *screen_roll_create(lv_obj_t *parent);
void screen_roll_update(void);

/* CONFIG_UI_SHOT_TOUR: README ekran görüntüsü turu (yoksa boş) */
void ui_shots_start(void);

/* Saat ekran koruyucusu (üst katman) */
void screen_clock_create(void);
void screen_clock_show(void);
void screen_clock_hide(void);
bool screen_clock_visible(void);
void screen_clock_update(void);
