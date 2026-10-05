#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* WiFi ELM327 klonları: adaptör açık bir AP yayınlar (WiFi_OBDII, V-LINK...),
 * ELM'e 192.168.0.10:35000 üzerinden şeffaf TCP köprüsüyle erişilir. */

typedef void (*wifi_obd_rx_cb_t)(const uint8_t *data, size_t len);

/* start: ilk çağrıda yığını kurar, radyoyu açıp aramaya başlar.
 * stop: TCP'yi kapatır, radyoyu durdurur (bloklar, en fazla ~9 sn). */
void wifi_obd_start(void);
void wifi_obd_stop(void);
void wifi_obd_rescan(void);

bool wifi_obd_send(const uint8_t *data, size_t len);
bool wifi_obd_is_connected(void);

void wifi_obd_set_rx_callback(wifi_obd_rx_cb_t cb);
