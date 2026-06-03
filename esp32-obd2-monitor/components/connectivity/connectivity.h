#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app.h"
#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t connectivity_start(connection_type_t type);
void connectivity_stop(void);
esp_err_t connectivity_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
connection_type_t connectivity_get_current_type(void);
bool connectivity_is_connected(void);
esp_err_t connectivity_auto_reconnect(void);
esp_err_t connectivity_wifi_scan(wifi_ap_info_t *list, int max_count, int *found_count);
esp_err_t connectivity_wifi_connect_manual(const char *ssid, wifi_auth_mode_t authmode);
esp_err_t connectivity_wifi_enable_auto_mode(void);

#ifdef __cplusplus
}
#endif
