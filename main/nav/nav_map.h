#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Telefonun gönderdiği harita resmi (karanlık temalı OSM, JPEG).
 *
 *   Başlık (JSON):  {"t":"map","id":7,"len":34567,"w":460,"h":460,
 *                    "clat":39.92,"clon":32.85,"z":15}
 *   Parçalar (MAP karakteristiği): [id:1][offset:3 LE][JPEG baytları]
 *
 * Merkez + zoom Web Mercator (256 px karo) — ESP rota çizgisini ve konumu
 * aynı projeksiyonla resmin üzerine çizer. Çözme ayrı görevde; UI hazır
 * resmi nav_map_take() ile alır (çift tampon, UI kullanırken üzerine yazılmaz). */

typedef struct {
    uint8_t  id;
    uint32_t len;
    uint16_t w, h;
    uint8_t  zoom;
    double   lat, lon;          /* resim merkezi */
} nav_map_hdr_t;

typedef struct {
    const uint16_t *pixels;     /* RGB565, w*h */
    uint16_t w, h;
    uint8_t  zoom;
    double   lat, lon;
} nav_map_img_t;

#define NAV_MAP_MAX_W   480
#define NAV_MAP_MAX_H   480
#define NAV_MAP_MAX_LEN (160 * 1024)

/* Her resmin sonucu (why == NULL: gösterime hazır). Çözme görevinden / BLE
 * host görevinden çağrılır. */
typedef void (*nav_map_result_cb_t)(uint8_t id, const char *why);
void nav_map_set_result_cb(nav_map_result_cb_t cb);

void nav_map_init(void);
void nav_map_begin(const nav_map_hdr_t *hdr);
void nav_map_chunk(const uint8_t *data, size_t len);
/* Yeni resim çözüldüyse true; *out geçerli kalır (bir sonraki take'e kadar). */
bool nav_map_take(nav_map_img_t *out);
