#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_NAME "ESP32-S3 OBD2 Monitor"
#define APP_VERSION "1.0.0"

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define DISPLAY_ROUND true

#define UART_OBD2_TX_PIN 43
#define UART_OBD2_RX_PIN 44
#define UART_OBD2_BAUD 38400

#define OBD2_FAST_POLL_MS 40
#define OBD2_SLOW_POLL_MS 2000
#define OBD2_DTC_POLL_MS 30000

#define GAUGE_UPDATE_RATE_HZ 25
#define GAUGE_SMOOTH_DIVISOR 4

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_DHCP_SETTLE_MS     3500
#define BT_CONNECT_TIMEOUT_MS 15000

#define OBD2_DEFAULT_WIFI_PASSWORD "12345678"
#define OBD2_DEFAULT_ADAPTER_IP "192.168.0.10"
#define OBD2_DEFAULT_ADAPTER_PORT 35000

typedef enum {
    CONN_TYPE_NONE = 0,
    CONN_TYPE_USB,
    CONN_TYPE_WIFI,
    CONN_TYPE_BLUETOOTH
} connection_type_t;

typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT,
    THEME_AUTO
} theme_mode_t;

typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char obd_adapter_ip[16];
    uint16_t obd_adapter_port;
    connection_type_t preferred_connection;
    theme_mode_t theme;
    bool haptic_enabled;
    bool sound_enabled;
    uint8_t brightness;
    bool wifi_manual_mode;
    uint8_t wifi_authmode;
    uint8_t default_gauge;
} app_settings_t;

extern app_settings_t g_settings;

void app_settings_init(void);
void app_settings_save(void);

#ifdef __cplusplus
}
#endif
