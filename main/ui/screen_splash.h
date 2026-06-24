#pragma once

#include "lvgl.h"

lv_obj_t *screen_splash_create(lv_obj_t *parent);
void screen_splash_start(lv_obj_t *splash, lv_timer_cb_t on_finish);
