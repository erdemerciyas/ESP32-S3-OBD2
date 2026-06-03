#pragma once

#include "driver/i2c.h"
#include "esp_io_expander.h"

/**
 * Waveshare ESP32-S3-Touch-LCD-2.1 pin map
 * @see https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1
 */

#define BOARD_LCD_WIDTH          480
#define BOARD_LCD_HEIGHT         480

/* UI layout — inset for round Waveshare bezel */
#define UI_ROUND_INSET           28
#define UI_SCREEN_W              BOARD_LCD_WIDTH
#define UI_SCREEN_H              BOARD_LCD_HEIGHT
#define UI_SAFE_W                (UI_SCREEN_W - (UI_ROUND_INSET * 2))
#define UI_SAFE_H                (UI_SCREEN_H - (UI_ROUND_INSET * 2))
#define UI_SWIPE_THRESHOLD_PX    50
#define UI_SWIPE_COOLDOWN_MS     400
#define UI_DOUBLE_TAP_MS         320
#define UI_LONG_PRESS_MS         4000

#define BOARD_LCD_PIN_PCLK       41
#define BOARD_LCD_PIN_DE         40
#define BOARD_LCD_PIN_VSYNC      39
#define BOARD_LCD_PIN_HSYNC      38
#define BOARD_LCD_PIN_BL         6
#define BOARD_LCD_PIN_SPI_SDA    1
#define BOARD_LCD_PIN_SPI_SCL    2

#define BOARD_I2C_PORT           I2C_NUM_0
#define BOARD_I2C_SCL            7
#define BOARD_I2C_SDA            15

#define BOARD_EXIO_LCD_RST       IO_EXPANDER_PIN_NUM_0
#define BOARD_EXIO_TP_RST        IO_EXPANDER_PIN_NUM_1
#define BOARD_EXIO_LCD_CS        IO_EXPANDER_PIN_NUM_2
#define BOARD_EXIO_BUZZER        IO_EXPANDER_PIN_NUM_7   /* EXIO8: active high, off = LOW */

#define BOARD_TOUCH_I2C_ADDR     0x15
#define BOARD_TOUCH_PIN_INT      16
