#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);
void display_set_brightness(uint8_t brightness);
bool display_live_updates_enabled(void);
void display_set_live_updates(bool enable);

#ifdef __cplusplus
}
#endif
