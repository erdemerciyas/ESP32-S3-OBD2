#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ELM327 taşıma katmanı: BLE veya WiFi (TCP). Seçim NVS'de tutulur; aynı
 * anda yalnız seçilen radyo yığını çalışır, geçiş yeniden başlatmadan olur. */

typedef enum {
    OBD_LINK_BLE = 0,
    OBD_LINK_WIFI,
} obd_link_type_t;

typedef void (*obd_link_rx_cb_t)(const uint8_t *data, size_t len);

void obd_link_init(void);
void obd_link_start(void);
void obd_link_rescan(void);

bool obd_link_send(const uint8_t *data, size_t len);
bool obd_link_is_connected(void);
void obd_link_set_rx_callback(obd_link_rx_cb_t cb);

obd_link_type_t obd_link_get_type(void);
/* Kaydeder; eskisini durdurup yenisini arka planda başlatır (UI bloklanmaz).
 * Geçiş sürerken gelen istekler yok sayılır. */
void obd_link_switch(obd_link_type_t type);

/* NAV modu: OBD radyosunu (BLE ya da WiFi) tamamen kapatır / seçili taşımayla
 * yeniden başlatır. suspend bloklar (WiFi'de ~11 sn'ye kadar) — görevden çağır. */
void obd_link_suspend(void);
void obd_link_resume(void);
