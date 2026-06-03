#pragma once

#include "lvgl.h"
#include "ui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_settings_ui_create(lv_obj_t *screen);
void wifi_settings_ui_refresh(void);
/** Update status-card WiFi icon (call from gauge refresh loop). */
void wifi_settings_ui_sync_wifi_ind(ui_wifi_ind_level_t level);

#ifdef __cplusplus
}
#endif
