#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"

#define WIFI_SCAN_TIMEOUT_MS 5000
#define WIFI_SCAN_MAX_RESULTS 24

typedef struct {
    char ssid[32];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_info_t;

bool wifi_connect(const char *ssid, const char *password);
bool wifi_connect_with_auth(const char *ssid, const char *password, wifi_auth_mode_t authmode);
bool wifi_connect_to_obd_adapter(void);
bool wifi_connect_manual_network(const char *ssid, wifi_auth_mode_t authmode);
void wifi_clear_manual_network(void);
void wifi_disconnect(void);
esp_err_t wifi_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
bool wifi_is_connected(void);
bool wifi_is_ap_connected(void);
esp_err_t wifi_get_status(void);
int wifi_scan_elm327_networks(wifi_ap_info_t *ap_list, int max_count);
int wifi_scan_all_networks(wifi_ap_info_t *ap_list, int max_count);
bool wifi_connect_to_elm327(void);
esp_err_t wifi_test_elm327_connection(uint8_t *version, size_t version_len);
bool wifi_is_elm327_ssid(const char *ssid);
