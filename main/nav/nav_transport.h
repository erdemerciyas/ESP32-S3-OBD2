#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Navigasyon taşıma katmanı — nav_service hangi radyonun kullanıldığını
 * bilmez. Geri çağrılar taşımanın kendi görevinden gelir (ör. NimBLE host). */

/* ch: NAV_CH_MSG = protokol mesajı (JSON), NAV_CH_BULK = harita resmi parçası */
enum { NAV_CH_MSG = 0, NAV_CH_BULK = 1 };
typedef void (*nav_transport_rx_cb_t)(int ch, const uint8_t *data, size_t len);
typedef void (*nav_transport_link_cb_t)(bool up);

typedef struct {
    const char *name;
    /* Radyoyu açar, telefonu beklemeye başlar. */
    bool (*start)(nav_transport_rx_cb_t rx, nav_transport_link_cb_t link);
    /* Radyoyu tamamen kapatır (bloklar). */
    void (*stop)(void);
    /* Telefona tek mesaj; bağlı/abone değilse false. */
    bool (*send)(const uint8_t *data, size_t len);
} nav_transport_t;

extern const nav_transport_t nav_transport_ble;
