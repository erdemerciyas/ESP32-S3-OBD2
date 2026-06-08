#pragma once

// I2C Bus (shared: TCA9554PWR, CST820, QMI8658, PCF85063)
#define I2C_SDA_PIN             15
#define I2C_SCL_PIN             7

// LCD SPI (ST7701 init commands only)
#define LCD_SPI_CLK_PIN         2
#define LCD_SPI_MOSI_PIN        1

// LCD Backlight (PWM)
#define LCD_BACKLIGHT_PIN       6
#define LCD_BACKLIGHT_FREQ      20000
#define LCD_BACKLIGHT_RES       10

// LCD Resolution
#define LCD_WIDTH               480
#define LCD_HEIGHT              480

// LCD RGB Parallel Interface Pins
#define LCD_RGB_HSYNC_PIN       38
#define LCD_RGB_VSYNC_PIN       39
#define LCD_RGB_DE_PIN          40
#define LCD_RGB_PCLK_PIN        41

// RGB Data Pins (B0-B4, G0-G5, R0-R4)
#define LCD_RGB_DATA0_PIN       5    // B0
#define LCD_RGB_DATA1_PIN       45   // B1
#define LCD_RGB_DATA2_PIN       48   // B2
#define LCD_RGB_DATA3_PIN       47   // B3
#define LCD_RGB_DATA4_PIN       21   // B4
#define LCD_RGB_DATA5_PIN       14   // G0
#define LCD_RGB_DATA6_PIN       13   // G1
#define LCD_RGB_DATA7_PIN       12   // G2
#define LCD_RGB_DATA8_PIN       11   // G3
#define LCD_RGB_DATA9_PIN       10   // G4
#define LCD_RGB_DATA10_PIN      9    // G5
#define LCD_RGB_DATA11_PIN      46   // R0
#define LCD_RGB_DATA12_PIN      3    // R1
#define LCD_RGB_DATA13_PIN      8    // R2
#define LCD_RGB_DATA14_PIN      18   // R3
#define LCD_RGB_DATA15_PIN      17   // R4

// Touch Controller CST820
#define TOUCH_INT_PIN           16

// RGB Timing Parameters
#define LCD_RGB_TIMING_FREQ_HZ  (16 * 1000 * 1000)
#define LCD_RGB_TIMING_HPW      8
#define LCD_RGB_TIMING_HBP      10
#define LCD_RGB_TIMING_HFP      50
#define LCD_RGB_TIMING_VPW      3
#define LCD_RGB_TIMING_VBP      8
#define LCD_RGB_TIMING_VFP      8
