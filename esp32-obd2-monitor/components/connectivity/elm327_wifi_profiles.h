#pragma once

#include <stdint.h>

/*
 * Common ELM327 WiFi OBD2 adapter profiles seen on the market.
 * Aligned with industry defaults (Car Scanner ELM Wi-Fi guide):
 *   https://www.carscanner.info/wifi/
 *   - SSID often OBDII / OBD2 / WIFI_OBDII (not the car brand name)
 *   - Many adapters: no Wi-Fi password (open AP)
 *   - Typical TCP: 192.168.0.10:35000 (verify adapter manual if needed)
 * Runtime order: NVS saved -> DHCP gateway -> same-subnet scan -> table below.
 * Do not brute-force 0.0.0.0/0: each failed TCP costs ~1–3 s on the adapter AP.
 */

#define ELM327_DEFAULT_PASSWORD     "12345678"
#define ELM327_DEFAULT_IP           "192.168.0.10"
#define ELM327_DEFAULT_PORT         35000

#define ELM327_TCP_PROBE_TIMEOUT_MS     3000
#define ELM327_TCP_CONNECT_TIMEOUT_S    3
#define ELM327_TCP_DISCOVERY_TIMEOUT_S  1

/* Ports seen on clone / VGate / OBDLink / Hi-Flying LPT230 WiFi modules */
static const uint16_t elm327_tcp_ports[] = {
    35000,  /* de facto ELM327 WiFi standard */
    35001,
    8080,
    23,     /* telnet-style raw AT on some firmware */
    60000,
};

#define ELM327_TCP_PORT_COUNT (sizeof(elm327_tcp_ports) / sizeof(elm327_tcp_ports[0]))

/*
 * Last octet candidates on the STA subnet (e.g. phone/ESP .12 -> try .10 adapter).
 * Skips the STA's own address at runtime.
 */
static const uint8_t elm327_subnet_host_octets[] = {
    1, 10, 11, 15, 20, 100, 254,
};

#define ELM327_SUBNET_HOST_COUNT \
    (sizeof(elm327_subnet_host_octets) / sizeof(elm327_subnet_host_octets[0]))

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
    "00000000",
    "88888888",
    "password",
    "",
};

/* WIFI_OBDII adapters (underscore SSID) often use these first */
static const char *const elm327_wifi_obdii_passwords[] = {
    "12345678",
    "1234567890",
    "00000000",
    "88888888",
    "87654321",
};
#define ELM327_WIFI_OBDII_PASSWORD_COUNT \
    (sizeof(elm327_wifi_obdii_passwords) / sizeof(elm327_wifi_obdii_passwords[0]))

/*
 * Fallback TCP endpoints when gateway/subnet scan miss (rare DHCP layouts).
 * Gateway + subnet scan cover most 192.168.0.x adapters (incl. WIFI_OBDII).
 */
static const elm327_tcp_profile_t elm327_tcp_profiles[] = {
    {"192.168.0.10", 35000},
    {"192.168.0.10", 35001},
    {"192.168.0.10", 8080},
    {"192.168.0.10", 23},
    {"192.168.0.1",  35000},
    {"192.168.0.1",  35001},
    {"192.168.0.1",  8080},
    {"192.168.0.1",  23},
    {"192.168.0.11", 35000},
    {"192.168.0.15", 35000},
    {"192.168.1.10", 35000},
    {"192.168.1.1",  35000},
    {"192.168.1.1",  8080},
    {"192.168.4.1",  35000},
    {"192.168.4.1",  35001},
    {"192.168.43.1", 35000},
    {"10.0.0.1",     35000},
    {"172.16.0.1",   35000},
};

#define ELM327_SSID_EXACT_COUNT   (sizeof(elm327_ssid_exact) / sizeof(elm327_ssid_exact[0]))
#define ELM327_SSID_KEYWORD_COUNT (sizeof(elm327_ssid_keywords) / sizeof(elm327_ssid_keywords[0]))
#define ELM327_PASSWORD_COUNT     (sizeof(elm327_passwords) / sizeof(elm327_passwords[0]))
#define ELM327_TCP_PROFILE_COUNT  (sizeof(elm327_tcp_profiles) / sizeof(elm327_tcp_profiles[0]))
