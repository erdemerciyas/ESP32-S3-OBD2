#include "cst820_touch.h"
#include "board_config.h"
#include "esp_lcd_panel_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cst820";

#define DATA_START_REG  0x15
#define CHIP_ID_REG     0x01
#define TOUCH_NUM_REG   0x02
#define TOUCH_POS_REG   0x03

static esp_io_expander_handle_t s_expander;

static esp_err_t read_data(esp_lcd_touch_handle_t tp);
static bool get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                   uint8_t *point_num, uint8_t max_point_num);
static esp_err_t del(esp_lcd_touch_handle_t tp);
static esp_err_t i2c_read_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);
static esp_err_t i2c_write_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, const uint8_t *data, uint8_t len);
static esp_err_t touch_reset(esp_lcd_touch_handle_t tp);
static esp_err_t read_id(esp_lcd_touch_handle_t tp);

static esp_err_t tp_rst_pulse(void)
{
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_expander, BOARD_EXIO_TP_RST, 0), TAG, "TP RST low failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_expander, BOARD_EXIO_TP_RST, 1), TAG, "TP RST high failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io,
                                              const esp_lcd_touch_config_t *config,
                                              esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(io && config && tp, ESP_ERR_INVALID_ARG, TAG, "invalid arg");

    esp_lcd_touch_handle_t cst820 = calloc(1, sizeof(esp_lcd_touch_t));
    ESP_RETURN_ON_FALSE(cst820, ESP_ERR_NO_MEM, TAG, "no mem");

    cst820->io = io;
    cst820->read_data = read_data;
    cst820->get_xy = get_xy;
    cst820->del = del;
    cst820->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&cst820->config, config, sizeof(esp_lcd_touch_config_t));

    if (cst820->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_cfg = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_NEGEDGE,
            .pin_bit_mask = BIT64(cst820->config.int_gpio_num),
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "INT gpio failed");
    }

    ESP_RETURN_ON_ERROR(touch_reset(cst820), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(read_id(cst820), TAG, "read id failed");
    *tp = cst820;
    return ESP_OK;
}

static esp_err_t read_data(esp_lcd_touch_handle_t tp)
{
    uint8_t buf[41];
    uint8_t touch_cnt;
    const uint8_t clear = 0;
    const uint8_t over = 0xAB;

    uint8_t start = 0x01;
    i2c_master_write_to_device(BOARD_I2C_PORT, BOARD_TOUCH_I2C_ADDR, &start, 1, pdMS_TO_TICKS(100));

    i2c_write_bytes(tp, 0xFE, (uint8_t[]){1}, 1);

    ESP_RETURN_ON_ERROR(i2c_read_bytes(tp, TOUCH_NUM_REG, buf, 1), TAG, "read num failed");
    i2c_write_bytes(tp, TOUCH_POS_REG, &over, 1);

    if ((buf[0] & 0x0F) == 0) {
        i2c_write_bytes(tp, TOUCH_NUM_REG, &clear, 1);
        return ESP_OK;
    }

    touch_cnt = buf[0] & 0x0F;
    if (touch_cnt > 2 || touch_cnt == 0) {
        i2c_write_bytes(tp, TOUCH_NUM_REG, &clear, 1);
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_read_bytes(tp, TOUCH_POS_REG, buf, touch_cnt * 6), TAG, "read pos failed");
    i2c_write_bytes(tp, TOUCH_POS_REG, &over, 1);
    i2c_write_bytes(tp, TOUCH_NUM_REG, &clear, 1);

    taskENTER_CRITICAL(&tp->data.lock);
    if (touch_cnt > CONFIG_ESP_LCD_TOUCH_MAX_POINTS) {
        touch_cnt = CONFIG_ESP_LCD_TOUCH_MAX_POINTS;
    }
    tp->data.points = touch_cnt;
    for (size_t i = 0; i < touch_cnt; i++) {
        tp->data.coords[i].x = (uint16_t)(((buf[i * 6] & 0x0F) << 8) | buf[i * 6 + 1]);
        tp->data.coords[i].y = (uint16_t)(((buf[i * 6 + 2] & 0x0F) << 8) | buf[i * 6 + 3]);
        tp->data.coords[i].strength = 50;
    }
    taskEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static bool get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                   uint8_t *point_num, uint8_t max_point_num)
{
    portENTER_CRITICAL(&tp->data.lock);
    *point_num = (tp->data.points > max_point_num) ? max_point_num : tp->data.points;
    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    return *point_num > 0;
}

static esp_err_t del(esp_lcd_touch_handle_t tp)
{
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
    }
    free(tp);
    return ESP_OK;
}

static esp_err_t touch_reset(esp_lcd_touch_handle_t tp)
{
    (void)tp;
    return tp_rst_pulse();
}

static esp_err_t read_id(esp_lcd_touch_handle_t tp)
{
    uint8_t id = 0;
    const uint8_t start = 0x01;
    i2c_write_bytes(tp, DATA_START_REG, &start, 1);
    ESP_RETURN_ON_ERROR(i2c_read_bytes(tp, CHIP_ID_REG, &id, 1), TAG, "read chip id failed");
    ESP_LOGI(TAG, "chip id: 0x%02X", id);
    return ESP_OK;
}

static esp_err_t i2c_read_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t i2c_write_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, const uint8_t *data, uint8_t len)
{
    return esp_lcd_panel_io_tx_param(tp->io, reg, data, len);
}

esp_err_t cst820_touch_init(esp_io_expander_handle_t expander, esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(expander && tp, ESP_ERR_INVALID_ARG, TAG, "invalid arg");
    s_expander = expander;

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = BOARD_TOUCH_I2C_ADDR,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        },
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)BOARD_I2C_PORT, &io_config, &io),
        TAG, "touch io failed");

    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_WIDTH,
        .y_max = BOARD_LCD_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = BOARD_TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst820(io, &cfg, tp), TAG, "cst820 init failed");
    ESP_LOGI(TAG, "CST820 touch ready (I2C 0x%02X, INT GPIO%d)", BOARD_TOUCH_I2C_ADDR, BOARD_TOUCH_PIN_INT);
    return ESP_OK;
}
