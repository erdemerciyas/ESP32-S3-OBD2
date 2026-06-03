#pragma once

#include "lvgl.h"
#include "esp_attr.h"

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480

extern uint8_t draw_buf_1[];
extern uint8_t draw_buf_2[];

void lvgl_init(void);
uint32_t get_tick_cb(void);
void display_set_rotation(uint8_t rotation);
