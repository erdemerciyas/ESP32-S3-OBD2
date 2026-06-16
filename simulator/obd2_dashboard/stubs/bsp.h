#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bsp_display_init(void);
void bsp_display_lock(int timeout_ms);
void bsp_display_unlock(void);

#ifdef __cplusplus
}
#endif
