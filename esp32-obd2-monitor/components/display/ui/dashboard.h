#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DASHBOARD_MAIN = 0,
    DASHBOARD_MENU,
    DASHBOARD_SETTINGS,
    DASHBOARD_CONNECTION,
    DASHBOARD_ABOUT
} dashboard_screen_t;

void dashboard_init(void);
lv_obj_t *dashboard_get_main_screen(void);
void dashboard_finish_boot_screen(void);
void dashboard_show_screen(dashboard_screen_t screen);
void display_update_gauges(void);

/* Gauge navigation */
void dashboard_navigate_gauge_next(void);
void dashboard_navigate_gauge_prev(void);

#ifdef __cplusplus
}
#endif
