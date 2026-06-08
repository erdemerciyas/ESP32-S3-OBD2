#pragma once

// ELM327 WiFi Access Point
#define ELM327_WIFI_SSID        "OBDII"
#define ELM327_WIFI_PASSWORD    ""
#define ELM327_IP_ADDR          "192.168.0.10"
#define ELM327_TCP_PORT         35000

// Polling
#define OBD2_POLL_INTERVAL_MS   200
#define LVGL_TICK_MS            5

// Display
#define DEFAULT_BRIGHTNESS      80

// Connection
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define ELM327_CONNECT_TIMEOUT  5000
#define RECONNECT_INTERVAL_MS   3000
