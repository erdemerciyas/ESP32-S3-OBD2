#include "lvgl_driver.h"
#include "board_config.h"
#include "tca9554_expander.h"
#include "cst820_touch.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch.h"
#include "esp_io_expander.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "lvgl_driver";

#define DRAW_BUF_LINES   160
#define LEDC_BL_TIMER    LEDC_TIMER_0
#define LEDC_BL_MODE     LEDC_LOW_SPEED_MODE
#define LEDC_BL_CHANNEL  LEDC_CHANNEL_0

static lv_display_t *display_handle = NULL;
static esp_io_expander_handle_t s_board_expander = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_panel = NULL;
static lv_indev_t *touch_handle = NULL;
static SemaphoreHandle_t lvgl_mux = NULL;
static uint8_t backlight_percent = 70;

static const st7701_lcd_init_cmd_t waveshare_init_cmds[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0B, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x07, 0x02}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x6D}, 1, 0},
    {0xB1, (uint8_t[]){0x37}, 1, 0},
    {0xB2, (uint8_t[]){0x81}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x43}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE5, (uint8_t[]){0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},
    {0xE8, (uint8_t[]){0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x66}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 480},
    {0x20, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

bool lvgl_lock(int timeout_ms)
{
    if (!lvgl_mux) {
        return true;
    }
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, ticks) == pdTRUE;
}

void lvgl_unlock(void)
{
    if (lvgl_mux) {
        xSemaphoreGiveRecursive(lvgl_mux);
    }
}

static esp_err_t init_board_i2c(void)
{
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_PORT, &i2c_conf), TAG, "I2C config failed");
    return i2c_driver_install(BOARD_I2C_PORT, i2c_conf.mode, 0, 0, 0);
}

static esp_err_t init_board_expander(esp_io_expander_handle_t expander)
{
    const uint32_t outputs = BOARD_EXIO_LCD_RST | BOARD_EXIO_TP_RST | BOARD_EXIO_LCD_CS | BOARD_EXIO_BUZZER;
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(expander, outputs, IO_EXPANDER_OUTPUT), TAG, "exp dir failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, BOARD_EXIO_BUZZER, 0), TAG, "buzzer off failed");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, BOARD_EXIO_LCD_RST, 0), TAG, "LCD RST low failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, BOARD_EXIO_LCD_RST, 1), TAG, "LCD RST high failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, BOARD_EXIO_LCD_CS, 1), TAG, "CS high failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, BOARD_EXIO_LCD_CS, 0), TAG, "CS low failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t lcd_cs_disable(esp_io_expander_handle_t expander)
{
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, BOARD_EXIO_LCD_CS, 0), TAG, "CS low failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, BOARD_EXIO_LCD_CS, 1), TAG, "CS high failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static void init_backlight_hw(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_BL_MODE,
        .timer_num = LEDC_BL_TIMER,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 4000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .speed_mode = LEDC_BL_MODE,
        .channel = LEDC_BL_CHANNEL,
        .timer_sel = LEDC_BL_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BOARD_LCD_PIN_BL,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void lvgl_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    backlight_percent = percent;
    const uint32_t duty = (8192U * percent) / 100U;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_BL_MODE, LEDC_BL_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_BL_MODE, LEDC_BL_CHANNEL));
}

static void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

uint32_t get_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    if (!touch_panel) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x[1] = {0};
    uint16_t y[1] = {0};
    uint8_t points = 0;

    esp_lcd_touch_read_data(touch_panel);
    if (esp_lcd_touch_get_coordinates(touch_panel, x, y, NULL, &points, 1) && points > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_handler_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t delay_ms = 1;
        if (lvgl_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if (delay_ms > 500) {
            delay_ms = 500;
        } else if (delay_ms == 0) {
            delay_ms = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_io_expander_handle_t board_expander_get(void)
{
    return s_board_expander;
}

void lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL for Waveshare ESP32-S3-Touch-LCD-2.1...");

    lv_init();
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    configASSERT(lvgl_mux);

    ESP_ERROR_CHECK(init_board_i2c());

    esp_io_expander_handle_t expander = NULL;
    ESP_ERROR_CHECK(tca9554_new_i2c_expander(BOARD_I2C_PORT, &expander));
    s_board_expander = expander;
    ESP_ERROR_CHECK(init_board_expander(expander));

    ESP_LOGI(TAG, "Install 3-wire SPI panel IO");
    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_EXPANDER,
        .cs_gpio_num = BOARD_EXIO_LCD_CS,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = BOARD_LCD_PIN_SPI_SCL,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = BOARD_LCD_PIN_SPI_SDA,
        .io_expander = expander,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_spi_cfg = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_spi_cfg, &io_handle));

    ESP_LOGI(TAG, "Install ST7701 RGB panel");
    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .psram_trans_align = 64,
        .data_width = 16,
        .bits_per_pixel = 16,
        .de_gpio_num = BOARD_LCD_PIN_DE,
        .pclk_gpio_num = BOARD_LCD_PIN_PCLK,
        .vsync_gpio_num = BOARD_LCD_PIN_VSYNC,
        .hsync_gpio_num = BOARD_LCD_PIN_HSYNC,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            5, 45, 48, 47, 21,
            14, 13, 12, 11, 10, 9,
            46, 3, 8, 18, 17,
        },
        .timings = {
            .pclk_hz = 18 * 1000 * 1000,
            .h_res = DISPLAY_WIDTH,
            .v_res = DISPLAY_HEIGHT,
            .hsync_pulse_width = 8,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .vsync_pulse_width = 3,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram = true,
        .num_fbs = 2,
        .bounce_buffer_size_px = DISPLAY_WIDTH * 10,
    };

    st7701_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
        .init_cmds = waveshare_init_cmds,
        .init_cmds_size = sizeof(waveshare_init_cmds) / sizeof(st7701_lcd_init_cmd_t),
        .flags = {
            .mirror_by_cmd = 1,
            .enable_io_multiplex = 0,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_ERROR_CHECK(lcd_cs_disable(expander));

    init_backlight_hw();
    lvgl_set_backlight(backlight_percent);

    ESP_ERROR_CHECK(cst820_touch_init(expander, &touch_panel));

    const size_t draw_buf_bytes = DISPLAY_WIDTH * DRAW_BUF_LINES * 2;
    void *draw_buf1 = heap_caps_malloc(draw_buf_bytes, MALLOC_CAP_SPIRAM);
    void *draw_buf2 = heap_caps_malloc(draw_buf_bytes, MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(draw_buf1 && draw_buf2 ? ESP_OK : ESP_ERR_NO_MEM);

    display_handle = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_buffers(display_handle, draw_buf1, draw_buf2, draw_buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_handle, display_flush);
    lv_display_set_color_format(display_handle, LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(display_handle, LV_DISPLAY_ROTATION_0);

    lv_tick_set_cb(get_tick_cb);

    touch_handle = lv_indev_create();
    lv_indev_set_type(touch_handle, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_handle, touchpad_read);
    lv_indev_set_display(touch_handle, display_handle);

    ESP_LOGI(TAG, "LVGL initialized (%dx%d, CST820 touch)", BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
}

void lvgl_start(void)
{
    xTaskCreatePinnedToCore(lvgl_handler_task, "lvgl_handler", 20480, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "LVGL handler task started");
}

void display_set_rotation(uint8_t rotation)
{
    if (display_handle && lvgl_lock(-1)) {
        lv_display_set_rotation(display_handle, (lv_display_rotation_t)rotation);
        lvgl_unlock();
    }
}
