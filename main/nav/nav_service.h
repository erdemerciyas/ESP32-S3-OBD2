#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Telefon ↔ ESP32 servisi: NAV ve ROLL modlarında çalışır (bkz. app_mode).
 * start/stop bloklayabilir. */
void nav_service_init(void);
void nav_service_start(void);
void nav_service_stop(void);

/* Harita sayfası: telefondan yeni harita iste. fit: tüm rotayı sığdır;
 * değilse araca ortalı, verilen zoom'da. UI görevinden çağrılabilir. */
void nav_service_request_map(bool fit, int zoom);

/* ESP modunu ("obd" | "nav" | "roll") kaydeder ve bağlı telefona status gönderir. */
void nav_service_set_mode(const char *mode);
/* ROLL ikili telemetrisi (TEL kanalı); telefon bağlı/abone değilse false. */
bool nav_service_send_tel(const uint8_t *data, size_t len);
