#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bsp_display_init(void);
void bsp_display_lock(int timeout_ms);
void bsp_display_unlock(void);

/* Buzzer stubs */
void bsp_buzzer_init(void);
void bsp_buzzer_on(void);
void bsp_buzzer_off(void);
void bsp_buzzer_beep(int duration_ms);

#ifdef __cplusplus
}
#endif
