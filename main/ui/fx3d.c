#include "fx3d.h"

#include <math.h>
#include <string.h>

/* Toplamalı piksel: mevcut renge (rgb * a/255) ekle, doygunlukta kırp. */
static inline void plot(fx_canvas_t *c, int x, int y, uint32_t rgb, int a)
{
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h || a <= 0) {
        return;
    }
    uint16_t *p = &c->px[y * c->w + x];
    uint16_t v = *p;
    int r = ((v >> 11) & 0x1F) + ((int)((rgb >> 16) & 0xFF) * a >> 11);
    int g = ((v >> 5) & 0x3F) + ((int)((rgb >> 8) & 0xFF) * a >> 10);
    int b = (v & 0x1F) + ((int)(rgb & 0xFF) * a >> 11);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    *p = (uint16_t)((r << 11) | (g << 5) | b);
}

void fx_clear(fx_canvas_t *c, uint32_t rgb)
{
    uint16_t v = (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
    uint32_t vv = ((uint32_t)v << 16) | v;
    uint32_t *p = (uint32_t *)c->px;
    size_t n = (size_t)c->w * c->h / 2;
    for (size_t i = 0; i < n; i++) {
        p[i] = vv;
    }
}

/* Xiaolin Wu çizgisi, toplamalı */
void fx_line(fx_canvas_t *c, float x0, float y0, float x1, float y1, uint32_t rgb, int k)
{
    if (k <= 0) {
        return;
    }
    bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    if (steep) {
        float t;
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        float t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    /* görünür alana kırp (uzun ızgara çizgileri için) */
    float lim = (float)(steep ? c->h : c->w);
    float dx = x1 - x0;
    float grad = dx < 0.001f ? 1.0f : (y1 - y0) / dx;
    if (x0 < 0) { y0 += grad * (0 - x0); x0 = 0; }
    if (x1 > lim - 1) { x1 = lim - 1; }
    if (x0 > x1) {
        return;
    }

    float y = y0;
    for (int x = (int)x0; x <= (int)x1; x++, y += grad) {
        int yi = (int)floorf(y);
        float f = y - yi;
        int a1 = (int)(k * (1.0f - f));
        int a2 = (int)(k * f);
        if (steep) {
            plot(c, yi, x, rgb, a1);
            plot(c, yi + 1, x, rgb, a2);
        } else {
            plot(c, x, yi, rgb, a1);
            plot(c, x, yi + 1, rgb, a2);
        }
    }
}

void fx_glow_line(fx_canvas_t *c, float x0, float y0, float x1, float y1, uint32_t rgb, int k)
{
    /* hale: dik yönde ±2 px kaydırılmış sönük kopyalar */
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) {
        return;
    }
    float nx = -dy / len, ny = dx / len;
    fx_line(c, x0 + nx * 2, y0 + ny * 2, x1 + nx * 2, y1 + ny * 2, rgb, k / 5);
    fx_line(c, x0 - nx * 2, y0 - ny * 2, x1 - nx * 2, y1 - ny * 2, rgb, k / 5);
    fx_line(c, x0 + nx, y0 + ny, x1 + nx, y1 + ny, rgb, k / 3);
    fx_line(c, x0 - nx, y0 - ny, x1 - nx, y1 - ny, rgb, k / 3);
    fx_line(c, x0, y0, x1, y1, rgb, k);
}

void fx_dot(fx_canvas_t *c, float x, float y, float r, uint32_t rgb, int k)
{
    int r2 = (int)ceilf(r + 1);
    for (int j = -r2; j <= r2; j++) {
        for (int i = -r2; i <= r2; i++) {
            float d = sqrtf((float)(i * i + j * j));
            float a = r + 0.5f - d;           /* yumuşak kenar */
            if (a > 1) a = 1;
            if (a > 0) {
                plot(c, (int)x + i, (int)y + j, rgb, (int)(k * a));
            }
        }
    }
}

void fx_hspan(fx_canvas_t *c, int y, float x0, float x1, uint32_t rgb, int k)
{
    if (y < 0 || y >= c->h) {
        return;
    }
    int a = (int)fmaxf(0, x0), b = (int)fminf((float)c->w - 1, x1);
    for (int x = a; x <= b; x++) {
        plot(c, x, y, rgb, k);
    }
}

fx_v3 fx_rotate(fx_v3 v, float ax, float ay, float az)
{
    float s, co, t;
    s = sinf(ax); co = cosf(ax);
    t = v.y * co - v.z * s; v.z = v.y * s + v.z * co; v.y = t;
    s = sinf(ay); co = cosf(ay);
    t = v.x * co + v.z * s; v.z = -v.x * s + v.z * co; v.x = t;
    s = sinf(az); co = cosf(az);
    t = v.x * co - v.y * s; v.y = v.x * s + v.y * co; v.x = t;
    return v;
}

bool fx_project(fx_v3 v, float cam, float f, float cx, float cy, float *sx, float *sy)
{
    float z = v.z + cam;
    if (z < 0.05f) {
        return false;
    }
    *sx = cx + v.x * f / z;
    *sy = cy - v.y * f / z;
    return true;
}

#define P 0.8506508f   /* altın oran normalize */
#define Q 0.5257311f
const fx_v3 FX_ICO_V[12] = {
    {-Q,  P, 0}, { Q,  P, 0}, {-Q, -P, 0}, { Q, -P, 0},
    {0, -Q,  P}, {0,  Q,  P}, {0, -Q, -P}, {0,  Q, -P},
    { P, 0, -Q}, { P, 0,  Q}, {-P, 0, -Q}, {-P, 0,  Q},
};
const uint8_t FX_ICO_E[30][2] = {
    {0, 1}, {0, 5}, {0, 7}, {0, 10}, {0, 11}, {1, 5}, {1, 7}, {1, 8}, {1, 9}, {2, 3},
    {2, 4}, {2, 6}, {2, 10}, {2, 11}, {3, 4}, {3, 6}, {3, 8}, {3, 9}, {4, 5}, {4, 9},
    {4, 11}, {5, 9}, {5, 11}, {6, 7}, {6, 8}, {6, 10}, {7, 8}, {7, 10}, {8, 9}, {10, 11},
};

float fx_rand(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return (float)(*seed >> 8) / 16777216.0f;
}
