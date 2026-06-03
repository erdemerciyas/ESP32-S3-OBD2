#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void haptic_init(void);
void haptic_pulse_ms(uint16_t duration_ms);
void haptic_click(void);
void haptic_alert(void);

#ifdef __cplusplus
}
#endif
