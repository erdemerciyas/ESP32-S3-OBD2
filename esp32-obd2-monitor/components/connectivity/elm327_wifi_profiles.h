#pragma once

#include <stdint.h>

/*
 * Common ELM327 WiFi OBD2 adapter profiles seen on the market.
 * Connection order at runtime: user settings -> DHCP gateway -> profiles below.
 */

#define ELM327_DEFAULT_PASSWORD     "12345678"
#define ELM327_DEFAULT_IP           "192.168.0.10"
#define ELM327_DEFAULT_PORT         35000

#define ELM327_TCP_PROBE_TIMEOUT_MS 3000
#define ELM327_TCP_CONNECT_TIMEOUT_S 3

typedef struct {
    const char *ip;
    uint16_t port;
} elm327_tcp_profile_t;

/* Exact SSID matches (case-sensitive as broadcast) */
static const char *const elm327_ssid_exact[] = {
    "OBDII",
    "OBD2",
    "OBD",
    "WiFi-OBD",
    "WiFi-OBDII",
    "WiFi-ELM327",
    "WiFi_OBD",
    "WiFi_OBDII",
    "ELM327",
    "ELM-327",
    "ELM327-WiFi",
    "OBDWIFI",
    "OBD-WIFI",
    "OBDLink",
    "Vlink",
    "V-LINK",
    "VLINK",
    "iCar",
    "ICAR",
    "Car_WIFI",
    "CAR_WIFI",
    "WIFI_OBD",
    "WIFI-OBD",
    "OBDII-WIFI",
    "OBD2-WIFI",
};

/* Case-insensitive substring match inside SSID */
static const char *const elm327_ssid_keywords[] = {
    "OBD",
    "ELM",
    "327",
    "VLINK",
    "ICAR",
    "WIFI-OBD",
    "WIFI_OBD",
    "OBDWIFI",
    "CAR_WIFI",
    "OBDII",
};

/* Passwords tried in order (empty string = open network) */
static const char *const elm327_passwords[] = {
    "12345678",
    "1234567890",
    "123456789",
    "87654321",
    "",
};

/*
 * Known TCP endpoints after WiFi association.
 * Gateway IP is probed first at runtime (not listed here).
 */
static const elm327_tcp_profile_t elm327_tcp_profiles[] = {
    {"192.168.0.10", 35000},
    {"192.168.0.10", 35001},
    {"192.168.0.10", 8080},
    {"192.168.0.1",  35000},
    {"192.168.0.1",  8080},
    {"192.168.1.1",  35000},
    {"192.168.1.1",  8080},
    {"192.168.4.1",  35000},
    {"10.0.0.1",     35000},
    {"172.16.0.1",   35000},
    {"192.168.43.1", 35000},
};

#define ELM327_SSID_EXACT_COUNT   (sizeof(elm327_ssid_exact) / sizeof(elm327_ssid_exact[0]))
#define ELM327_SSID_KEYWORD_COUNT (sizeof(elm327_ssid_keywords) / sizeof(elm327_ssid_keywords[0]))
#define ELM327_PASSWORD_COUNT     (sizeof(elm327_passwords) / sizeof(elm327_passwords[0]))
#define ELM327_TCP_PROFILE_COUNT  (sizeof(elm327_tcp_profiles) / sizeof(elm327_tcp_profiles[0]))
