#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

bool bt_connect(void);
void bt_disconnect(void);
esp_err_t bt_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
bool bt_is_connected(void);
