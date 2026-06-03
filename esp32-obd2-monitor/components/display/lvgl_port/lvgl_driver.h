#pragma once

#include "lvgl.h"
#include "esp_io_expander.h"
#include <stdbool.h>

esp_io_expander_handle_t board_expander_get(void);

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480

void lvgl_init(void);
void lvgl_start(void);
bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);
void lvgl_set_backlight(uint8_t percent);
uint32_t get_tick_cb(void);
void display_set_rotation(uint8_t rotation);
