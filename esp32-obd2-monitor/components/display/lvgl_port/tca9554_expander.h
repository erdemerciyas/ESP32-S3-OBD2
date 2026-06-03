#pragma once

#include "driver/i2c.h"
#include "esp_io_expander.h"

esp_err_t tca9554_new_i2c_expander(i2c_port_t i2c_num, esp_io_expander_handle_t *handle);
