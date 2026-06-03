#pragma once

#include "esp_io_expander.h"
#include "esp_lcd_touch.h"

esp_err_t cst820_touch_init(esp_io_expander_handle_t expander, esp_lcd_touch_handle_t *tp);
