#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ELM327 WiFi adapter common SSIDs
#define ELM327_SSID_LIST \
    {"OBDII", "WiFi-ELM327", "ELM327", "OBD2", "ELM-327"}
#define ELM327_DEFAULT_PASSWORD "12345678"
#define ELM327_DEFAULT_IP "192.168.0.10"
#define ELM327_DEFAULT_PORT 35000
#define WIFI_SCAN_TIMEOUT_MS 5000
#define WIFI_CONNECT_TIMEOUT_MS 15000

typedef struct {
    char ssid[32];
    int8_t rssi;
} wifi_ap_info_t;

bool wifi_connect(const char *ssid, const char *password);
bool wifi_connect_to_obd_adapter(void);
void wifi_disconnect(void);
esp_err_t wifi_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
bool wifi_is_connected(void);
esp_err_t wifi_get_status(void);
int wifi_scan_elm327_networks(wifi_ap_info_t *ap_list, int max_count);
bool wifi_connect_to_elm327(void);
esp_err_t wifi_test_elm327_connection(uint8_t *version, size_t version_len);
bool wifi_is_elm327_ssid(const char *ssid);
