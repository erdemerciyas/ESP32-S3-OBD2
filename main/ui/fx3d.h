#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Küçük yazılım 3D çizici (three.js tarzı tel kafes / neon sahneler için).
 * RGB565 tampona toplamalı (additive) kenar yumuşatmalı çizgi çizer — üst
 * üste binen çizgiler parlar. LVGL canvas tamponuyla kullanılır. */

typedef struct {
    uint16_t *px;      /* LV_COLOR_DEPTH 16, bayt takası yok */
    int w, h;
} fx_canvas_t;

typedef struct {
    float x, y, z;
} fx_v3;

#define FX_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

void fx_clear(fx_canvas_t *c, uint32_t rgb);
/* Kenar yumuşatmalı toplamalı çizgi; k = yoğunluk 0..255 */
void fx_line(fx_canvas_t *c, float x0, float y0, float x1, float y1, uint32_t rgb, int k);
/* Neon: geniş sönük hale + parlak çekirdek */
void fx_glow_line(fx_canvas_t *c, float x0, float y0, float x1, float y1, uint32_t rgb, int k);
void fx_dot(fx_canvas_t *c, float x, float y, float r, uint32_t rgb, int k);
/* Yatay dolu çizgi (güneş tarama çizgileri) */
void fx_hspan(fx_canvas_t *c, int y, float x0, float x1, uint32_t rgb, int k);

fx_v3 fx_rotate(fx_v3 v, float ax, float ay, float az);
/* Perspektif: kamera z = -cam, odak f; false: kameranın arkasında */
bool fx_project(fx_v3 v, float cam, float f, float cx, float cy, float *sx, float *sy);

/* Birim ikozahedron: 12 köşe, 30 kenar */
extern const fx_v3 FX_ICO_V[12];
extern const uint8_t FX_ICO_E[30][2];

/* Belirlenimci sözde rastgele (0..1) */
float fx_rand(uint32_t *seed);
