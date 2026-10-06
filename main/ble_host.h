#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "host/ble_gatt.h"

/* Paylaşılan NimBLE yığını. Birden çok kullanıcı (OBD adaptörüne central,
 * telefona peripheral) aynı yığını aynı anda kullanabilir — ROLL modunda
 * ikisi birlikte açıktır. İlk acquire yığını açar, son release kapatır.
 *
 * GATT tablosu yığın açılmadan önce kaydedilmelidir; bu yüzden telefon
 * servisi açılışta bir kez verilir ve yığın her açıldığında kaydedilir
 * (reklam yapılmadıkça görünmez). */

typedef struct {
    void (*on_sync)(void);          /* yığın hazır (açılış / reset sonrası) */
    void (*on_reset)(int reason);
} ble_host_client_t;

void ble_host_init(const struct ble_gatt_svc_def *svcs, const char *name);

/* Yığın zaten hazırsa on_sync çağıranın görevinden hemen çağrılır. */
bool ble_host_acquire(const ble_host_client_t *client);
/* Kullanıcı kendi bağlantı/tarama/reklamını önceden kapatmış olmalı.
 * Son kullanıcıysa yığını kapatır (bloklar). */
void ble_host_release(const ble_host_client_t *client);

uint8_t ble_host_own_addr_type(void);
/* Yığını kullanan sayısı: >1 ise radyo paylaşılıyor (tarama görev oranı düşürülmeli). */
int ble_host_client_count(void);
