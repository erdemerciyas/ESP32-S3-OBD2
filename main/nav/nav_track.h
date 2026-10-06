#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nav_state.h"

/* Sürüş kaydı: rota başlayınca telefonun GPS konumları ve geçilen yollar
 * PSRAM'de tutulur; harita / özet sayfaları buradan çizer. Kayıt bir sonraki
 * rota başlayana kadar durur (rota bitince de incelenebilsin). */

typedef struct {
    int32_t lat_e6;
    int32_t lon_e6;
} nav_track_pt_t;

#define NAV_TRACK_MAX_ROADS 48

void nav_track_init(void);
void nav_track_reset(void);
void nav_track_add(double lat, double lon);
void nav_track_add_road(const char *road);

uint32_t nav_track_rev(void);          /* her değişiklikte artar */
float    nav_track_distance_m(void);
uint32_t nav_track_duration_s(void);   /* ilk noktadan bu yana */

/* En fazla max noktayı eşit aralıkla seçip kopyalar (son nokta her zaman dahil). */
size_t nav_track_copy(nav_track_pt_t *out, size_t max);
/* Geçilen yollar, eskiden yeniye. */
int nav_track_roads(char out[][NAV_TEXT_LEN], int max);
