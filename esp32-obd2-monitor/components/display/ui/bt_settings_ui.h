#pragma once

#include "lvgl.h"
#include "ui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

void bt_settings_ui_create(lv_obj_t *screen);
void bt_settings_ui_refresh(void);
void bt_settings_ui_sync_conn_ind(ui_conn_ind_level_t level);

#ifdef __cplusplus
}
#endif
