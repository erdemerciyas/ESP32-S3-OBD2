#pragma once

#include <Arduino.h>

struct ObdWifiProfile {
    const char *ssid;
    const char *password;
};

// Yaygin ELM327 WiFi adaptör SSID / sifreleri (plana gore)
static const ObdWifiProfile OBD_WIFI_PROFILES[] = {
    {"OBDII", "12345678"},
    {"OBDII", "123456"},
    {"WiFi_OBDII", "12345678"},
    {"WiFi_OBDII", "123456"},
    {"WIFI_OBDII", "12345678"},
    {"WIFI_OBDII", "123456"},
    {"V-LINK", "12345678"},
    {"V-LINK", "123456"},
    {"OBD2", "12345678"},
    {"OBD", "12345678"},
    {"ELM327", "12345678"},
    {"ScanTool", "12345678"},
};

static const size_t OBD_WIFI_PROFILE_COUNT =
    sizeof(OBD_WIFI_PROFILES) / sizeof(OBD_WIFI_PROFILES[0]);

inline bool isKnownObdSsid(const char *ssid) {
    if (!ssid || !ssid[0]) {
        return false;
    }
    for (size_t i = 0; i < OBD_WIFI_PROFILE_COUNT; ++i) {
        if (strcmp(ssid, OBD_WIFI_PROFILES[i].ssid) == 0) {
            return true;
        }
    }
    return false;
}
