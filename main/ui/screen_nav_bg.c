#include "screen_nav_pages.h"
#include "fx3d.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>

/* NAV rehberlik sayfasının canlı arka planı (fx3d, ~15 fps):
 *   - sabit taban: lacivert merkez, sıcak (turuncu-mor) ufuk ışıltısı —
 *     bir kez hesaplanır, her karede kopyalanır
 *   - üstte dalgalanan kuzey ışığı (aurora) perdeleri
 *   - altta perspektif yol: kenar çizgileri, hızla akan orta şerit ve ızgara;
 *     yaklaşan dönüşün yönüne doğru kıvrılır (400 m'den itibaren)
 * Metinler canvas'ın önünde; yoğunluklar okunurluğu bozmayacak kadar düşük. */

#define BG_SZ        440
#define BG_C         (BG_SZ / 2.0f)
#define BG_FRAME_MS  66
#define BG_HOR_Y     304.0f     /* ufuk (canvas y) — ekran merkezinden +84 */
#define BG_BEND_M    400        /* dönüş kıvrımı bu mesafeden başlar */
#define BG_BEND_PX   90.0f

static lv_obj_t *s_canvas;
static uint16_t *s_buf;
static uint16_t *s_base;
static fx_canvas_t s_fx;
static uint32_t s_last;
static float s_travel;
static float s_bend;

static uint16_t rgb565(float r, float g, float b)
{
    int ri = r > 255 ? 255 : (int)r, gi = g > 255 ? 255 : (int)g, bi = b > 255 ? 255 : (int)b;
    return (uint16_t)(((ri & 0xF8) << 8) | ((gi & 0xFC) << 3) | (bi >> 3));
}

static void build_base(void)
{
    for (int y = 0; y < BG_SZ; y++) {
        for (int x = 0; x < BG_SZ; x++) {
            float dx = (x - BG_C) / BG_C, dy = (y - BG_C) / BG_C;
            float v = 1.0f - 0.8f * (dx * dx + dy * dy);   /* vinyet */
            if (v < 0) v = 0;
            float r = 5 * v, g = 14 * v, b = 30 * v;
            if (y < BG_HOR_Y) {                             /* gökte mor ton */
                float s = 1.0f - y / BG_HOR_Y;
                r += 14 * s * v;
                b += 16 * s * v;
            }
            float hy = (y - BG_HOR_Y) / 46.0f;              /* ufuk ışıltısı */
            float glow = expf(-hy * hy) * (1.0f - 0.7f * fabsf(dx));
            r += 70 * glow;
            g += 22 * glow;
            b += 36 * glow;
            s_base[y * BG_SZ + x] = rgb565(r, g, b);
        }
    }
}

lv_obj_t *nav_bg_create(lv_obj_t *parent)
{
    s_buf = heap_caps_malloc(BG_SZ * BG_SZ * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_base = heap_caps_malloc(BG_SZ * BG_SZ * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_buf || !s_base) {
        heap_caps_free(s_buf);
        heap_caps_free(s_base);
        s_buf = s_base = NULL;
        return NULL;
    }
    build_base();
    memcpy(s_buf, s_base, BG_SZ * BG_SZ * sizeof(uint16_t));
    s_fx = (fx_canvas_t){ .px = s_buf, .w = BG_SZ, .h = BG_SZ };

    s_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(s_canvas, s_buf, BG_SZ, BG_SZ, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    return s_canvas;
}

/* Kuzey ışığı: dalgalı taban çizgisinden yukarı sönen dikey perdeler */
static void aurora(float t, float gain)
{
    static const struct { float y, ph, sp; uint32_t rgb; } R[] = {
        { 150, 0.0f, 0.55f, FX_RGB(0, 255, 170) },
        { 120, 2.1f, 0.42f, FX_RGB(0, 170, 255) },
        { 178, 4.0f, 0.70f, FX_RGB(190, 80, 255) },
    };
    for (size_t i = 0; i < sizeof(R) / sizeof(R[0]); i++) {
        for (int x = 20; x < BG_SZ - 20; x += 3) {
            float y = R[i].y + 16 * sinf(x * 0.017f + t * R[i].sp + R[i].ph)
                            + 7 * sinf(x * 0.043f - t * 0.8f);
            float fold = 0.5f + 0.5f * sinf(x * 0.05f + t * 0.9f + R[i].ph);
            int k = (int)((14 + 30 * fold) * gain);
            fx_line(&s_fx, x, y, x, y - 10, R[i].rgb, k);
            fx_line(&s_fx, x, y - 10, x, y - 26, R[i].rgb, k * 2 / 3);
            fx_line(&s_fx, x, y - 26, x, y - 46, R[i].rgb, k / 3);
        }
    }
}

/* Yol üzerindeki nokta: u = 0 alt kenar, 1 ufuk; lane = yanal konum (-1..1 kenarlar) */
static void road_pt(float lane, float u, float *x, float *y)
{
    float z = 1.0f + u * 11.0f;                       /* derinlik 1..12 */
    *y = BG_HOR_Y + (BG_SZ - BG_HOR_Y) / z;
    *x = BG_C + lane * 150.0f / z + s_bend * (1.0f - 1.0f / z) * (1.0f - 1.0f / z);
}

static void road_line(float lane, uint32_t rgb, int k)
{
    float px, py;
    road_pt(lane, 0, &px, &py);
    for (int i = 1; i <= 8; i++) {
        float x, y;
        road_pt(lane, i / 8.0f, &x, &y);
        fx_line(&s_fx, px, py, x, y, rgb, k);
        px = x;
        py = y;
    }
}

static void road(float gain, bool dashes)
{
    /* ızgara: yanal çizgiler + akan derinlik çizgileri */
    for (int l = -4; l <= 4; l++) {
        if (l != -1 && l != 1) {
            road_line(l * 1.0f, FX_RGB(150, 70, 255), (int)(18 * gain));
        }
    }
    float ph = s_travel - floorf(s_travel);
    for (int j = 0; j < 11; j++) {
        float z = 1.0f + j + (1.0f - ph);
        float u = (z - 1.0f) / 11.0f;
        if (u > 1) continue;
        float x0, y0, x1, y1;
        road_pt(-4, u, &x0, &y0);
        road_pt(4, u, &x1, &y1);
        fx_line(&s_fx, x0, y0, x1, y1, FX_RGB(150, 70, 255), (int)(34 * gain / sqrtf(z)));
    }
    /* yol kenarları */
    road_line(-1, FX_RGB(0, 220, 255), (int)(70 * gain));
    road_line(1, FX_RGB(0, 220, 255), (int)(70 * gain));

    /* orta şerit: harekete göre akan kesikler */
    if (dashes) {
        for (int j = 0; j < 12; j++) {
            float z0 = 1.0f + j * 1.0f + (1.0f - ph), z1 = z0 + 0.45f;
            float u0 = (z0 - 1.0f) / 11.0f, u1 = (z1 - 1.0f) / 11.0f;
            if (u1 > 1) continue;
            float x0, y0, x1, y1;
            road_pt(0, u0, &x0, &y0);
            road_pt(0, u1, &x1, &y1);
            fx_glow_line(&s_fx, x0, y0, x1, y1, FX_RGB(255, 220, 160), (int)(110 * gain / sqrtf(z0)));
        }
    }
}

void nav_bg_set_visible(bool on)
{
    if (!s_canvas) {
        return;
    }
    if (on) {
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }
}

void nav_bg_update(const nav_state_t *ns, int mode)
{
    if (!s_canvas || lv_obj_has_flag(s_canvas, LV_OBJ_FLAG_HIDDEN) ||
        lv_tick_elaps(s_last) < BG_FRAME_MS) {
        return;
    }
    float dt = lv_tick_elaps(s_last) / 1000.0f;
    if (dt > 0.2f) dt = 0.2f;
    s_last = lv_tick_get();
    float t = s_last / 1000.0f;

    bool guide = mode == NAV_BG_GUIDE;
    float gain = mode == NAV_BG_STALE ? 0.45f : 1.0f;

    /* akış hızı: telefon hızı; yoksa / boştayken yavaş süzülme */
    float kmh = guide && ns->speed_kmh > 0 ? ns->speed_kmh : 12.0f;
    if (mode != NAV_BG_STALE) {
        s_travel += kmh * dt * 0.045f;
    }

    /* dönüş kıvrımı: sağ +, sol −; yaklaştıkça artar, yumuşak geçiş */
    float target = 0;
    if (guide && ns->maneuver_dist_m < BG_BEND_M) {
        float side = 0;
        switch (ns->maneuver) {
        case NAV_MAN_RIGHT:        side = 1.0f;  break;
        case NAV_MAN_SLIGHT_RIGHT: side = 0.5f;  break;
        case NAV_MAN_LEFT:         side = -1.0f; break;
        case NAV_MAN_SLIGHT_LEFT:  side = -0.5f; break;
        default: break;
        }
        target = side * BG_BEND_PX * (1.0f - (float)ns->maneuver_dist_m / BG_BEND_M);
    }
    s_bend += (target - s_bend) * 0.08f;

    memcpy(s_buf, s_base, BG_SZ * BG_SZ * sizeof(uint16_t));
    aurora(t, guide ? gain : 1.4f);
    road(gain, guide);
    lv_obj_invalidate(s_canvas);
}
