#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Blocking boot splash; call with LVGL mutex held. Loads @p next_screen before deleting splash. */
void splash_run_boot_animation(lv_obj_t *next_screen);

#ifdef __cplusplus
}
#endif
