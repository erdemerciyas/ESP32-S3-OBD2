#pragma once

#include <stdbool.h>

/* Telefon ↔ ESP32 navigasyon servisi. Yalnızca NAV modunda çalışır; OBD
 * radyosu kapatıldıktan sonra başlatılır (bkz. app_mode). Bloklayabilir. */
void nav_service_init(void);
void nav_service_start(void);
void nav_service_stop(void);

/* Harita sayfası: telefondan yeni harita iste. fit: tüm rotayı sığdır;
 * değilse araca ortalı, verilen zoom'da. UI görevinden çağrılabilir. */
void nav_service_request_map(bool fit, int zoom);
