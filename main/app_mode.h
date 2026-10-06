#pragma once

#include <stdbool.h>

/* Uygulama modu. Radyo politikası:
 *   OBD : OBD bağlantısı (BLE ya da WiFi)
 *   NAV : yalnız telefon (OBD radyosu tamamen kapalı)
 *   ROLL: telefon + OBD adaptörü aynı BLE yığınında (OBD WiFi seçiliyse
 *         yalnız telefon — WiFi ile BLE birlikte çalıştırılmaz)
 * Son mod NVS'de tutulur, açılışta o başlar. */

typedef enum {
    APP_MODE_OBD = 0,
    APP_MODE_NAV,
    APP_MODE_ROLL,
} app_mode_t;

void app_mode_init(void);
/* Açılış: kayıtlı modun radyosunu başlatır (obd_link_start yerine). */
void app_mode_start(void);

/* Kaydeder ve geçişi arka planda yapar (UI bloklanmaz). Geçiş sürerken gelen
 * istekler son hedefe göre birleştirilir. */
void app_mode_set(app_mode_t mode);
/* İstenen (hedef) mod — UI bunu gösterir. */
app_mode_t app_mode_get(void);
bool app_mode_is_switching(void);
