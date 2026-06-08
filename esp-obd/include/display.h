#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "pin_config.h"
#include "TCA9554PWR.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/spi_master.h"

static spi_device_handle_t st7701_spi_handle = NULL;
static esp_lcd_panel_handle_t lcd_panel_handle = NULL;
static lv_disp_draw_buf_t lv_disp_buf;
static lv_disp_drv_t lv_disp_drv;
static lv_color_t* lv_buf1 = NULL;
static lv_color_t* lv_buf2 = NULL;

static void ST7701_WriteCmd(uint8_t cmd) {
    spi_transaction_t t = {};
    t.cmd = 0; t.addr = cmd; t.length = 0; t.rxlength = 0;
    spi_device_transmit(st7701_spi_handle, &t);
}

static void ST7701_WriteData(uint8_t data) {
    spi_transaction_t t = {};
    t.cmd = 1; t.addr = data; t.length = 0; t.rxlength = 0;
    spi_device_transmit(st7701_spi_handle, &t);
}

static void ST7701_InitCommands() {
    TCA9554_SetPin(EXIO_PIN3, EXIO_LOW);
    vTaskDelay(pdMS_TO_TICKS(10));

    ST7701_WriteCmd(0xFF); ST7701_WriteData(0x77); ST7701_WriteData(0x01);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x10);
    ST7701_WriteCmd(0xC0); ST7701_WriteData(0x3B); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xC1); ST7701_WriteData(0x0B); ST7701_WriteData(0x02);
    ST7701_WriteCmd(0xC2); ST7701_WriteData(0x07); ST7701_WriteData(0x02);
    ST7701_WriteCmd(0xCC); ST7701_WriteData(0x10);
    ST7701_WriteCmd(0xCD); ST7701_WriteData(0x08);

    ST7701_WriteCmd(0xB0);
    ST7701_WriteData(0x00); ST7701_WriteData(0x11); ST7701_WriteData(0x16);
    ST7701_WriteData(0x0E); ST7701_WriteData(0x11); ST7701_WriteData(0x06);
    ST7701_WriteData(0x05); ST7701_WriteData(0x09); ST7701_WriteData(0x08);
    ST7701_WriteData(0x21); ST7701_WriteData(0x06); ST7701_WriteData(0x13);
    ST7701_WriteData(0x10); ST7701_WriteData(0x29); ST7701_WriteData(0x31);
    ST7701_WriteData(0x18);

    ST7701_WriteCmd(0xB1);
    ST7701_WriteData(0x00); ST7701_WriteData(0x11); ST7701_WriteData(0x16);
    ST7701_WriteData(0x0E); ST7701_WriteData(0x11); ST7701_WriteData(0x07);
    ST7701_WriteData(0x05); ST7701_WriteData(0x09); ST7701_WriteData(0x09);
    ST7701_WriteData(0x21); ST7701_WriteData(0x05); ST7701_WriteData(0x13);
    ST7701_WriteData(0x11); ST7701_WriteData(0x2A); ST7701_WriteData(0x31);
    ST7701_WriteData(0x18);

    ST7701_WriteCmd(0xFF); ST7701_WriteData(0x77); ST7701_WriteData(0x01);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x11);
    ST7701_WriteCmd(0xB0); ST7701_WriteData(0x6D);
    ST7701_WriteCmd(0xB1); ST7701_WriteData(0x37);
    ST7701_WriteCmd(0xB2); ST7701_WriteData(0x81);
    ST7701_WriteCmd(0xB3); ST7701_WriteData(0x80);
    ST7701_WriteCmd(0xB5); ST7701_WriteData(0x43);
    ST7701_WriteCmd(0xB7); ST7701_WriteData(0x85);
    ST7701_WriteCmd(0xB8); ST7701_WriteData(0x20);
    ST7701_WriteCmd(0xC1); ST7701_WriteData(0x78);
    ST7701_WriteCmd(0xC2); ST7701_WriteData(0x78);
    ST7701_WriteCmd(0xD0); ST7701_WriteData(0x88);

    ST7701_WriteCmd(0xE0); ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x02);
    ST7701_WriteCmd(0xE1);
    ST7701_WriteData(0x03); ST7701_WriteData(0xA0); ST7701_WriteData(0x00);
    ST7701_WriteData(0x00); ST7701_WriteData(0x04); ST7701_WriteData(0xA0);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteData(0x20); ST7701_WriteData(0x20);
    ST7701_WriteCmd(0xE2);
    for (int i = 0; i < 12; i++) ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xE3); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteData(0x11); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xE4); ST7701_WriteData(0x22); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xE5);
    ST7701_WriteData(0x05); ST7701_WriteData(0xEC); ST7701_WriteData(0xA0); ST7701_WriteData(0xA0);
    ST7701_WriteData(0x07); ST7701_WriteData(0xEE); ST7701_WriteData(0xA0); ST7701_WriteData(0xA0);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xE6); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteData(0x11); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xE7); ST7701_WriteData(0x22); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xE8);
    ST7701_WriteData(0x06); ST7701_WriteData(0xED); ST7701_WriteData(0xA0); ST7701_WriteData(0xA0);
    ST7701_WriteData(0x08); ST7701_WriteData(0xEF); ST7701_WriteData(0xA0); ST7701_WriteData(0xA0);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xEB);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x40);
    ST7701_WriteData(0x40); ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0xED);
    ST7701_WriteData(0xFF); ST7701_WriteData(0xFF); ST7701_WriteData(0xFF); ST7701_WriteData(0xBA);
    ST7701_WriteData(0x0A); ST7701_WriteData(0xBF); ST7701_WriteData(0x45); ST7701_WriteData(0xFF);
    ST7701_WriteData(0xFF); ST7701_WriteData(0x54); ST7701_WriteData(0xFB); ST7701_WriteData(0xA0);
    ST7701_WriteData(0xAB); ST7701_WriteData(0xFF); ST7701_WriteData(0xFF); ST7701_WriteData(0xFF);
    ST7701_WriteCmd(0xEF);
    ST7701_WriteData(0x10); ST7701_WriteData(0x0D); ST7701_WriteData(0x04);
    ST7701_WriteData(0x08); ST7701_WriteData(0x3F); ST7701_WriteData(0x1F);

    ST7701_WriteCmd(0xFF); ST7701_WriteData(0x77); ST7701_WriteData(0x01);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x13);
    ST7701_WriteCmd(0xEF); ST7701_WriteData(0x08);
    ST7701_WriteCmd(0xFF); ST7701_WriteData(0x77); ST7701_WriteData(0x01);
    ST7701_WriteData(0x00); ST7701_WriteData(0x00); ST7701_WriteData(0x00);

    ST7701_WriteCmd(0x36); ST7701_WriteData(0x00);
    ST7701_WriteCmd(0x3A); ST7701_WriteData(0x55);
    ST7701_WriteCmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(480));
    ST7701_WriteCmd(0x20);
    vTaskDelay(pdMS_TO_TICKS(120));
    ST7701_WriteCmd(0x29);

    TCA9554_SetPin(EXIO_PIN3, EXIO_HIGH);
}

static void ST7701_HardwareReset() {
    TCA9554_SetPin(EXIO_PIN1, EXIO_LOW);
    vTaskDelay(pdMS_TO_TICKS(10));
    TCA9554_SetPin(EXIO_PIN1, EXIO_HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    esp_lcd_panel_draw_bitmap(lcd_panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(drv);
}

static uint8_t bl_channel = 0;

static void Backlight_Init() {
    ledcSetup(bl_channel, LCD_BACKLIGHT_FREQ, LCD_BACKLIGHT_RES);
    ledcAttachPin(LCD_BACKLIGHT_PIN, bl_channel);
}

static void Set_Backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t duty = percent * 10;
    if (duty == 1000) duty = 1023;
    ledcWrite(bl_channel, duty);
}

inline bool Display_Init() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    delay(100);
    Serial.println("[Display] I2C OK");

    TCA9554_Init();
    Serial.println("[Display] TCA9554PWR OK");

    TCA9554_SetPin(EXIO_PIN8, EXIO_LOW);   // LCD power ON (active low)
    delay(10);
    Serial.println("[Display] LCD power ON");

    // Touch controller reset (CST820 via TCA9554 PIN2)
    // Proper reset pulse: HIGH -> LOW -> HIGH, then wait for boot
    TCA9554_SetPin(EXIO_PIN2, EXIO_HIGH);  // start HIGH (idle)
    delay(5);
    TCA9554_SetPin(EXIO_PIN2, EXIO_LOW);   // reset pulse
    delay(10);
    TCA9554_SetPin(EXIO_PIN2, EXIO_HIGH);  // release reset
    delay(50);                             // wait for CST820 boot
    Serial.println("[Display] Touch reset done");

    spi_bus_config_t spi_bus = {};
    spi_bus.mosi_io_num = LCD_SPI_MOSI_PIN;
    spi_bus.miso_io_num = -1;
    spi_bus.sclk_io_num = LCD_SPI_CLK_PIN;
    spi_bus.quadwp_io_num = -1;
    spi_bus.quadhd_io_num = -1;
    spi_bus.max_transfer_sz = 64;
    if (spi_bus_initialize(SPI2_HOST, &spi_bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        Serial.println("[Display] SPI bus FAIL"); return false;
    }

    spi_device_interface_config_t spi_dev = {};
    spi_dev.command_bits = 1;
    spi_dev.address_bits = 8;
    spi_dev.mode = SPI_MODE0;
    spi_dev.clock_speed_hz = 40000000;
    spi_dev.spics_io_num = -1;
    spi_dev.queue_size = 1;
    if (spi_bus_add_device(SPI2_HOST, &spi_dev, &st7701_spi_handle) != ESP_OK) {
        Serial.println("[Display] SPI dev FAIL"); return false;
    }
    Serial.println("[Display] SPI OK");

    ST7701_HardwareReset();
    ST7701_InitCommands();
    Serial.println("[Display] ST7701 init OK");

    esp_lcd_rgb_panel_config_t rgb_config = {};
    rgb_config.clk_src = LCD_CLK_SRC_PLL160M;
    rgb_config.timings.pclk_hz = LCD_RGB_TIMING_FREQ_HZ;
    rgb_config.timings.h_res = LCD_HEIGHT;
    rgb_config.timings.v_res = LCD_WIDTH;
    rgb_config.timings.hsync_pulse_width = LCD_RGB_TIMING_HPW;
    rgb_config.timings.hsync_back_porch = LCD_RGB_TIMING_HBP;
    rgb_config.timings.hsync_front_porch = LCD_RGB_TIMING_HFP;
    rgb_config.timings.vsync_pulse_width = LCD_RGB_TIMING_VPW;
    rgb_config.timings.vsync_back_porch = LCD_RGB_TIMING_VBP;
    rgb_config.timings.vsync_front_porch = LCD_RGB_TIMING_VFP;
    rgb_config.timings.flags.pclk_active_neg = false;
    rgb_config.data_width = 16;
    rgb_config.psram_trans_align = 64;
    rgb_config.hsync_gpio_num = LCD_RGB_HSYNC_PIN;
    rgb_config.vsync_gpio_num = LCD_RGB_VSYNC_PIN;
    rgb_config.de_gpio_num = LCD_RGB_DE_PIN;
    rgb_config.pclk_gpio_num = LCD_RGB_PCLK_PIN;
    rgb_config.disp_gpio_num = -1;
    rgb_config.data_gpio_nums[0] = LCD_RGB_DATA0_PIN;
    rgb_config.data_gpio_nums[1] = LCD_RGB_DATA1_PIN;
    rgb_config.data_gpio_nums[2] = LCD_RGB_DATA2_PIN;
    rgb_config.data_gpio_nums[3] = LCD_RGB_DATA3_PIN;
    rgb_config.data_gpio_nums[4] = LCD_RGB_DATA4_PIN;
    rgb_config.data_gpio_nums[5] = LCD_RGB_DATA5_PIN;
    rgb_config.data_gpio_nums[6] = LCD_RGB_DATA6_PIN;
    rgb_config.data_gpio_nums[7] = LCD_RGB_DATA7_PIN;
    rgb_config.data_gpio_nums[8] = LCD_RGB_DATA8_PIN;
    rgb_config.data_gpio_nums[9] = LCD_RGB_DATA9_PIN;
    rgb_config.data_gpio_nums[10] = LCD_RGB_DATA10_PIN;
    rgb_config.data_gpio_nums[11] = LCD_RGB_DATA11_PIN;
    rgb_config.data_gpio_nums[12] = LCD_RGB_DATA12_PIN;
    rgb_config.data_gpio_nums[13] = LCD_RGB_DATA13_PIN;
    rgb_config.data_gpio_nums[14] = LCD_RGB_DATA14_PIN;
    rgb_config.data_gpio_nums[15] = LCD_RGB_DATA15_PIN;
    rgb_config.flags.fb_in_psram = true;

    if (esp_lcd_new_rgb_panel(&rgb_config, &lcd_panel_handle) != ESP_OK) {
        Serial.println("[Display] RGB panel FAIL"); return false;
    }
    esp_lcd_panel_reset(lcd_panel_handle);
    esp_lcd_panel_init(lcd_panel_handle);
    Serial.println("[Display] RGB panel OK");

    Backlight_Init();
    Set_Backlight(DEFAULT_BRIGHTNESS);
    Serial.println("[Display] Backlight ON");

    lv_init();
    size_t buf_size = LCD_WIDTH * 60;
    lv_buf1 = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_buf2 = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!lv_buf1) { Serial.println("[Display] LVGL buf FAIL"); return false; }

    lv_disp_draw_buf_init(&lv_disp_buf, lv_buf1, lv_buf2, buf_size);
    lv_disp_drv_init(&lv_disp_drv);
    lv_disp_drv.hor_res = LCD_WIDTH;
    lv_disp_drv.ver_res = LCD_HEIGHT;
    lv_disp_drv.flush_cb = lvgl_flush_cb;
    lv_disp_drv.draw_buf = &lv_disp_buf;
    lv_disp_drv_register(&lv_disp_drv);

    Serial.println("[Display] LVGL READY!");
    return true;
}
