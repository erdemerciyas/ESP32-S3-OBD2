#include "lvgl_driver.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lvgl_driver";

/* Waveshare ESP32-S3-Touch-LCD-2.1 RGB pin mapping */
#define LCD_PIN_PCLK    41
#define LCD_PIN_DE      40
#define LCD_PIN_VSYNC   39
#define LCD_PIN_HSYNC   38
#define LCD_PIN_BL      6
#define LCD_PIN_DISP_EN -1

/* Draw buffers - 1/10 of full screen for partial rendering */
DRAM_ATTR uint8_t draw_buf_1[DISPLAY_WIDTH * DISPLAY_HEIGHT / 10 * 2];
DRAM_ATTR uint8_t draw_buf_2[DISPLAY_WIDTH * DISPLAY_HEIGHT / 10 * 2];

static lv_display_t *display_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_indev_t *touch_handle = NULL;

/* LVGL flush callback - transfers buffer to LCD */
static void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int offsetx1 = area->x1;
    const int offsetx2 = area->x2;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static uint32_t get_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Touch read callback - placeholder (CST820 not yet implemented) */
static void touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;
    data->point.x = 0;
    data->point.y = 0;
}

/* LVGL tick task - required to drive LVGL timer */
static void lvgl_tick_task(void *arg)
{
    lv_tick_inc(1);
    vTaskDelay(pdMS_TO_TICKS(1));
}

void lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL for Waveshare ESP32-S3-Touch-LCD-2.1...");

    /* Initialize LVGL */
    lv_init();

    /* Configure SPI panel IO for ST7701 init commands */
    esp_lcd_panel_io_spi_config_t io_spi_cfg = {
        .cs_gpio_num = 20,
        .dc_gpio_num = 19,
        .spi_mode = 3,
        .pclk_hz = 80 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_spi_cfg, &io_handle));

    /* Configure ST7701 RGB panel */
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .bits_per_pixel = 16,
        .pclk_gpio_num = LCD_PIN_PCLK,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .de_gpio_num = LCD_PIN_DE,
        .disp_gpio_num = LCD_PIN_DISP_EN,
        .data_gpio_nums = {
            /* B0-B5 */
            -1, 5, 45, 48, 47, 21,
            /* G0-G5 */
            14, 13, 12, 11, 10, 9,
            /* R0-R5 */
            -1, 46, 3, 8, 18, 17,
        },
        .timings = {
            .pclk_hz = 12 * 1000 * 1000,
            .h_res = DISPLAY_WIDTH,
            .v_res = DISPLAY_HEIGHT,
            .hsync_back_porch = 30,
            .hsync_front_porch = 30,
            .hsync_pulse_width = 2,
            .vsync_back_porch = 30,
            .vsync_front_porch = 30,
            .vsync_pulse_width = 2,
        },
        .flags.fb_in_psram = true,
        .num_fbs = 2,
        .bounce_buffer_size_px = DISPLAY_WIDTH * 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* Enable backlight */
    gpio_config_t bl_gpio = {
        .pin_bit_mask = (1ULL << LCD_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_gpio);
    gpio_set_level(LCD_PIN_BL, 1);

    /* Create LVGL display */
    display_handle = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_buffers(display_handle, draw_buf_1, draw_buf_2, sizeof(draw_buf_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_handle, display_flush);
    lv_display_set_color_format(display_handle, LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(display_handle, LV_DISPLAY_ROTATION_0);

    /* Set tick callback */
    lv_tick_set_cb(get_tick_cb);

    /* Start LVGL tick task on core 0 */
    xTaskCreatePinnedToCore(lvgl_tick_task, "lvgl_tick", 2048, NULL, 1, NULL, 0);

    /* Create input device (touch) - placeholder */
    touch_handle = lv_indev_create();
    lv_indev_set_type(touch_handle, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_handle, touchpad_read);

    ESP_LOGI(TAG, "LVGL initialized successfully (%dx%d RGB565)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void display_set_rotation(uint8_t rotation)
{
    if (display_handle) {
        lv_display_set_rotation(display_handle, (lv_display_rotation_t)rotation);
    }
}
