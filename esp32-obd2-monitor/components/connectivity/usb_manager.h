#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

bool usb_cdc_connect(void);
void usb_cdc_disconnect(void);
esp_err_t usb_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
bool usb_is_connected(void);
