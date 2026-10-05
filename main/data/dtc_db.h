#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Önem derecesi: UI rengi ve sıralama. */
typedef enum {
    DTC_SEV_INFO = 1,   /* bilgi / izleme */
    DTC_SEV_WARN = 2,   /* servise götür */
    DTC_SEV_CRIT = 3,   /* motor/katalizör hasarı riski: hemen ilgilen */
} dtc_sev_t;

typedef struct {
    const char *desc;     /* Türkçe açıklama */
    const char *hint;     /* olası neden (NULL olabilir) */
    const char *system;   /* sistem grubu: "Ateşleme", "Yakıt/Hava"... */
    dtc_sev_t   sev;
    bool        known;    /* tabloda birebir bulundu (false: grup açıklaması) */
    bool        oem;      /* üreticiye özel (GM/Daewoo P1xxx) */
} dtc_info_t;

/* Ham J2012 kodu (P0133 = 0x0133) için açıklama. Her zaman doldurur. */
void dtc_db_lookup(uint16_t code, dtc_info_t *out);
