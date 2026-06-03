#include "tca9554_expander.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_io_expander.h"
#include <stddef.h>
#include <stdlib.h>

#ifndef __containerof
#define __containerof(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

static const char *TAG = "tca9554";

#define TCA9554_ADDR     0x20
#define REG_INPUT        0x00
#define REG_OUTPUT       0x01
#define REG_CONFIG       0x03
#define IO_COUNT         8

typedef struct {
    esp_io_expander_t base;
    i2c_port_t i2c_num;
    uint8_t output;
    uint8_t direction;
} tca9554_expander_t;

static esp_err_t tca9554_write_reg(tca9554_expander_t *dev, uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_master_write_to_device(dev->i2c_num, TCA9554_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t tca9554_read_reg(tca9554_expander_t *dev, uint8_t reg, uint8_t *data)
{
    return i2c_master_write_read_device(dev->i2c_num, TCA9554_ADDR, &reg, 1, data, 1, pdMS_TO_TICKS(100));
}

static esp_err_t read_input_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    tca9554_expander_t *dev = __containerof(handle, tca9554_expander_t, base);
    uint8_t temp = 0;
    ESP_RETURN_ON_ERROR(tca9554_read_reg(dev, REG_INPUT, &temp), TAG, "read input failed");
    *value = temp;
    return ESP_OK;
}

static esp_err_t write_output_reg(esp_io_expander_handle_t handle, uint32_t value)
{
    tca9554_expander_t *dev = __containerof(handle, tca9554_expander_t, base);
    dev->output = (uint8_t)value;
    return tca9554_write_reg(dev, REG_OUTPUT, dev->output);
}

static esp_err_t read_output_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    tca9554_expander_t *dev = __containerof(handle, tca9554_expander_t, base);
    *value = dev->output;
    return ESP_OK;
}

static esp_err_t write_direction_reg(esp_io_expander_handle_t handle, uint32_t value)
{
    tca9554_expander_t *dev = __containerof(handle, tca9554_expander_t, base);
    dev->direction = (uint8_t)value;
    return tca9554_write_reg(dev, REG_CONFIG, dev->direction);
}

static esp_err_t read_direction_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    tca9554_expander_t *dev = __containerof(handle, tca9554_expander_t, base);
    *value = dev->direction;
    return ESP_OK;
}

static esp_err_t reset(esp_io_expander_t *handle)
{
    tca9554_expander_t *dev = __containerof(handle, tca9554_expander_t, base);
    dev->direction = 0;
    dev->output = 0x7F; /* EXIO8 buzzer off (active high) */
    ESP_RETURN_ON_ERROR(tca9554_write_reg(dev, REG_CONFIG, dev->direction), TAG, "config failed");
    return tca9554_write_reg(dev, REG_OUTPUT, dev->output);
}

static esp_err_t del(esp_io_expander_t *handle)
{
    tca9554_expander_t *dev = __containerof(handle, tca9554_expander_t, base);
    free(dev);
    return ESP_OK;
}

esp_err_t tca9554_new_i2c_expander(i2c_port_t i2c_num, esp_io_expander_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid handle");

    tca9554_expander_t *dev = calloc(1, sizeof(tca9554_expander_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "no mem");

    dev->base.config.io_count = IO_COUNT;
    dev->base.config.flags.dir_out_bit_zero = 1;
    dev->i2c_num = i2c_num;
    dev->base.read_input_reg = read_input_reg;
    dev->base.write_output_reg = write_output_reg;
    dev->base.read_output_reg = read_output_reg;
    dev->base.write_direction_reg = write_direction_reg;
    dev->base.read_direction_reg = read_direction_reg;
    dev->base.reset = reset;
    dev->base.del = del;

    ESP_RETURN_ON_ERROR(reset(&dev->base), TAG, "reset failed");
    *handle = &dev->base;
    ESP_LOGI(TAG, "TCA9554 IO expander ready at 0x%02X", TCA9554_ADDR);
    return ESP_OK;
}
