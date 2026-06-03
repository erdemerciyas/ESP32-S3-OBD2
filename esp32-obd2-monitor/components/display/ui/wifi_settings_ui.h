#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_settings_ui_create(lv_obj_t *screen);
void wifi_settings_ui_refresh(void);

#ifdef __cplusplus
}
#endif
