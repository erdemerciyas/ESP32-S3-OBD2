#pragma once

#ifndef ELM327_HOST
#define ELM327_HOST "192.168.0.10"
#endif

static const char *ELM327_HOST_CANDIDATES[] = {
    ELM327_HOST,
    "192.168.0.1",
    "192.168.0.10",
    "192.168.1.1",
    "192.168.1.10",
    "192.168.4.1",
    "192.168.43.1",
};
static const size_t ELM327_HOST_CANDIDATE_COUNT =
    sizeof(ELM327_HOST_CANDIDATES) / sizeof(ELM327_HOST_CANDIDATES[0]);

static const uint16_t ELM327_PORTS[] = {35000, 23, 8080};

struct Elm327Endpoint {
    const char *host;
    uint16_t port;
};

/* Universal ELM327 WiFi — sabit host:port sweep (GW kilidi yoksa once) */
static const Elm327Endpoint ELM327_FIXED_ENDPOINTS[] = {
    {"192.168.0.10", 35000},
    {"192.168.0.10", 23},
    {"192.168.4.1", 35000},
    {"192.168.4.1", 23},
    {"192.168.1.10", 35000},
    {"192.168.1.10", 23},
    {"192.168.1.10", 8080},
};
static const size_t ELM327_FIXED_ENDPOINT_COUNT =
    sizeof(ELM327_FIXED_ENDPOINTS) / sizeof(ELM327_FIXED_ENDPOINTS[0]);

/* Yaygin adaptör TCP IP'leri (GW'den sonra denenir) */
static const char *ELM327_DISCOVERY_HOSTS[] = {
    "192.168.0.10",
    "192.168.0.1",
    "192.168.4.1",
    "192.168.1.10",
};
static const size_t ELM327_DISCOVERY_HOST_COUNT =
    sizeof(ELM327_DISCOVERY_HOSTS) / sizeof(ELM327_DISCOVERY_HOSTS[0]);
static const size_t ELM327_PORT_COUNT =
    sizeof(ELM327_PORTS) / sizeof(ELM327_PORTS[0]);

#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_SCAN_MAX           24
#define WIFI_SAVED_MAX          8
#define WIFI_DISPLAY_MAX        (WIFI_SCAN_MAX + WIFI_SAVED_MAX + 2)
#define ELM_TCP_SETTLE_MS       1500
#define ELM327_CONNECT_RETRY_MS 2000
#define ELM327_CONNECT_TIMEOUT_MS 3000
#define ELM327_TCP_WAKE_MS      1500
#define ELM327_TCP_FAIL_BACKOFF_MS 5000
#define ELM327_CMD_TIMEOUT_MS   4000
#define ELM327_INIT_TIMEOUT_MS  12000
#define ELM327_ATWS_TIMEOUT_MS  8000
#define ELM327_ATZ_TIMEOUT_MS   24000
#define ELM327_ATSP_TIMEOUT_MS  10000
#define ELM327_PROBE_TIMEOUT_MS 30000
#define ELM327_ENDPOINT_RETRIES 2
#define OBD_PID_INTERVAL_MS     180
#define OBD_EXTRAS_TIMEOUT_MS   6000
#define BOOT_SPLASH_MS          5000
#define UI_REFRESH_MS           200
#define UI_REFRESH_PENDING_MS   1200
#define UI_HINT_PENDING_MS      2000
#define WIFI_MENU_TICK_MS       200
#define WIFI_LIST_REBUILD_MS    500
#define OBD_AUTO_RESCAN_MS      8000
#define WIFI_FAIL_RESCAN_MS     3000
#define STATUS_BAR_UPDATE_MS    500

#define OBD_LOG_PATH            "/obd.log"
#define OBD_LOG_OLD_PATH        "/obd.log.old"
#define OBD_LOG_MAX_BYTES       (96 * 1024)
#define OBD_LOG_FLUSH_MS        5000
#define OBD_LOG_BOOT_TAIL_LINES 16

#define LCD_WIDTH  480
#define LCD_HEIGHT 480

/* Waveshare ESP32-S3-Touch-LCD-2.1: 480x480 round IPS */
#define LCD_ROUND_DISPLAY   1
#define LCD_RADIUS          240
/* Main HUD (gösterge) — WiFi menüsünden bağımsız */
#define LCD_SAFE_TOP        56
#define LCD_SAFE_BOTTOM     424
#define LCD_SAFE_WIDTH      320
#define LCD_HEADER_W        LCD_SAFE_WIDTH
#define LCD_HEADER_H        40
#define LCD_GAUGE_DIAMETER  468
#define LCD_GAUGE_ARC_W     8

/* WiFi / bağlantı ayarları — yalnızca bu ekran (yuvarlak panel) */
#define WIFI_SAFE_Y0        44
#define WIFI_SAFE_Y1        432
#define WIFI_UI_W           304
#define WIFI_UI_H           (WIFI_SAFE_Y1 - WIFI_SAFE_Y0)
#define WIFI_KB_H           126
#define WIFI_KB_Y           (WIFI_SAFE_Y1 - WIFI_KB_H)
#define WIFI_LIST_Y         62
#define WIFI_FOOTER_H       44
#define WIFI_LIST_H         (WIFI_KB_Y - WIFI_LIST_Y - WIFI_FOOTER_H - 6)
#define WIFI_PASS_Y0        WIFI_SAFE_Y0
#define WIFI_PASS_BACK_H    34
#define WIFI_PASS_TA_Y      (WIFI_PASS_Y0 + 52)
#define WIFI_PASS_TA_H      36
#define WIFI_PASS_BTNS_Y    (WIFI_PASS_TA_Y + WIFI_PASS_TA_H + 6)
#define WIFI_PASS_BTNS_H    36
#define WIFI_PASS_PRESET_Y  (WIFI_PASS_BTNS_Y + WIFI_PASS_BTNS_H + 4)
#define WIFI_PASS_PRESET_H  30
#define LCD_CHANNEL_COUNT   9

/* Opsiyonel: ilk acilista bu aga baglan (config.h icinde tanimlayin) */
// #define OBD_WIFI_SSID     "OBDII"
// #define OBD_WIFI_PASSWORD "12345678"
