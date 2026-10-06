#include "ui.h"
#include "theme.h"
#include "fx3d.h"
#include "clock.h"
#include "esp_heap_caps.h"
#include <math.h>

/* Saat ekran koruyucusu — üst katmanda tam ekran. Arkada fx3d ile çizilen
 * holografik kadran: geriye yatık, yavaşça salınan, derinlik katmanlarına
 * ayrılmış halkalar (dakika çentikleri en arkada, saniye kuyrukluyıldızı en
 * önde) — salınım katmanlar arasında paralaks yaratır. Saat ve dakika ilerleme
 * yayları neon, uçlarında parlayan boncuk. Önde kabartma (katmanlı) rakamlar.
 * Herhangi bir dokunuş kapatır. */

#define CK_CANVAS     440
#define CK_FRAME_MS   50
#define CK_CAM        3.0f
#define CK_F          700.0f
#define CK_STARS      64
#define CK_RING_SEGS  72
#define CK_EXTRUDE    6       /* rakam kabartma katman sayısı */
#define CK_TIME_Y     (-6)
#define CK_DAY_Y      (-88)
#define CK_DATE_Y     62
#define CK_HINT_Y     96
#define CK_BRAND_Y    168

/* Katman: yarıçap, derinlik (z+ = uzak) */
#define R_TICK   1.00f
#define Z_TICK   0.34f
#define R_HOUR   0.93f
#define Z_HOUR   0.20f
#define R_MIN    0.85f
#define Z_MIN    0.06f
#define R_SEC    0.77f
#define Z_SEC   (-0.08f)

#define C_TICK   FX_RGB(80, 150, 210)
#define C_HOUR   FX_RGB(255, 140, 30)
#define C_MIN    FX_RGB(0, 200, 255)
#define C_SEC    FX_RGB(190, 230, 255)
#define C_STAR   FX_RGB(150, 190, 255)
#define C_BG     FX_RGB(1, 4, 9)

static lv_obj_t *s_root;
static lv_obj_t *s_canvas;
static uint16_t *s_buf;
static fx_canvas_t s_fx;
static lv_obj_t *s_time[CK_EXTRUDE];
static lv_obj_t *s_day;
static lv_obj_t *s_date;
static lv_obj_t *s_hint;
static uint32_t s_last_frame;
static int s_prev_min = -1;
static int s_prev_valid = -1;
static fx_v3 s_stars[CK_STARS];

/* Kadranın salınım açıları (rad) — her karede bir kez hesaplanır */
static float s_pitch, s_yaw;

static void root_pressed_cb(lv_event_t *e)
{
    (void)e;
    screen_clock_hide();
    lv_indev_wait_release(lv_indev_get_act());   /* bırakış alttaki ekrana tıklama olmasın */
}

/* Kadran düzleminde a açısındaki nokta (0 = saat 12, saat yönünde) → ekran */
static bool dial_pt(float r, float z, float a, float *sx, float *sy, float *depth)
{
    fx_v3 v = { r * sinf(a), r * cosf(a), z };
    v = fx_rotate(v, s_pitch, s_yaw, 0);
    *depth = v.z;
    const float c = CK_CANVAS / 2.0f;
    return fx_project(v, CK_CAM, CK_F, c, c, sx, sy);
}

/* Yakın kısım parlak, uzak kısım sönük */
static int depth_k(int k, float z)
{
    float f = 0.62f - z * 0.75f;
    if (f < 0.22f) f = 0.22f;
    if (f > 1.0f)  f = 1.0f;
    return (int)(k * f);
}

static void ring(float r, float z, float a0, float a1, uint32_t rgb, int k, bool glow)
{
    int n = (int)(CK_RING_SEGS * (a1 - a0) / (2 * (float)M_PI)) + 1;
    float px = 0, py = 0, pd = 0;
    bool have = false;
    for (int i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        float x, y, d;
        if (!dial_pt(r, z, a, &x, &y, &d)) {
            have = false;
            continue;
        }
        if (have) {
            int kk = depth_k(k, (d + pd) * 0.5f);
            if (glow) {
                fx_glow_line(&s_fx, px, py, x, y, rgb, kk);
            } else {
                fx_line(&s_fx, px, py, x, y, rgb, kk);
            }
        }
        px = x; py = y; pd = d;
        have = true;
    }
}

static void bead(float r, float z, float a, uint32_t rgb)
{
    float x, y, d;
    if (dial_pt(r, z, a, &x, &y, &d)) {
        fx_dot(&s_fx, x, y, 7.0f, rgb, depth_k(40, d));
        fx_dot(&s_fx, x, y, 4.0f, rgb, depth_k(110, d));
        fx_dot(&s_fx, x, y, 2.0f, FX_RGB(255, 255, 255), 255);
    }
}

static void stars_init(void)
{
    uint32_t seed = 0xC10C;
    for (int i = 0; i < CK_STARS; i++) {
        s_stars[i].x = (fx_rand(&seed) - 0.5f) * 4.4f;
        s_stars[i].y = (fx_rand(&seed) - 0.5f) * 4.4f;
        s_stars[i].z = 0.6f + fx_rand(&seed) * 4.0f;
    }
}

static void render(const struct tm *tm, bool valid)
{
    float ts = lv_tick_get() / 1000.0f;
    const float c = CK_CANVAS / 2.0f;
    const float TAU = 2 * (float)M_PI;

    s_pitch = 0.44f + 0.07f * sinf(ts * 0.29f);
    s_yaw = 0.20f * sinf(ts * 0.21f);

    fx_clear(&s_fx, C_BG);

    /* yıldız alanı: yavaşça kameraya doğru akar */
    for (int i = 0; i < CK_STARS; i++) {
        fx_v3 *s = &s_stars[i];
        s->z -= 0.012f;
        if (s->z < 0.4f) {
            s->z += 4.0f;
        }
        float x, y;
        if (fx_project(*s, CK_CAM, CK_F * 0.55f, c, c, &x, &y)) {
            int k = (int)(150 - s->z * 28);
            fx_dot(&s_fx, x, y, s->z < 1.4f ? 1.4f : 0.8f, C_STAR, k);
        }
    }

    /* dakika çentikleri (en arka katman) */
    for (int i = 0; i < 60; i++) {
        float a = i * TAU / 60;
        bool major = i % 5 == 0;
        float x0, y0, x1, y1, d;
        if (dial_pt(major ? R_TICK - 0.09f : R_TICK - 0.04f, Z_TICK, a, &x0, &y0, &d) &&
            dial_pt(R_TICK, Z_TICK, a, &x1, &y1, &d)) {
            fx_line(&s_fx, x0, y0, x1, y1, C_TICK, depth_k(major ? 170 : 80, d));
        }
    }

    /* sönük tam halkalar — katmanların yerini gösterir */
    ring(R_HOUR, Z_HOUR, 0, TAU, C_HOUR, 26, false);
    ring(R_MIN, Z_MIN, 0, TAU, C_MIN, 26, false);
    ring(R_SEC, Z_SEC, 0, TAU, C_SEC, 18, false);

    float fs, fm, fh;
    if (valid) {
        float sec = tm->tm_sec + (lv_tick_get() % 1000) / 1000.0f;
        fs = sec / 60.0f;
        fm = (tm->tm_min + sec / 60.0f) / 60.0f;
        fh = ((tm->tm_hour % 12) + fm) / 12.0f;
    } else {
        fs = fmodf(ts / 60.0f, 1.0f);   /* ayarsız: yalnız saniye yörüngesi döner */
        fm = fh = 0;
    }

    if (fh > 0.003f) {
        ring(R_HOUR, Z_HOUR, 0, fh * TAU, C_HOUR, 150, true);
        bead(R_HOUR, Z_HOUR, fh * TAU, C_HOUR);
    }
    if (fm > 0.003f) {
        ring(R_MIN, Z_MIN, 0, fm * TAU, C_MIN, 150, true);
        bead(R_MIN, Z_MIN, fm * TAU, C_MIN);
    }

    /* saniye: kuyruklu yıldız — 70° iz, başa doğru parlaklaşır */
    float head = fs * TAU;
    const int tail = 14;
    for (int i = 0; i < tail; i++) {
        float a0 = head - (i + 1) * 0.085f, a1 = head - i * 0.085f;
        float x0, y0, x1, y1, d0, d1;
        if (dial_pt(R_SEC, Z_SEC, a0, &x0, &y0, &d0) && dial_pt(R_SEC, Z_SEC, a1, &x1, &y1, &d1)) {
            fx_glow_line(&s_fx, x0, y0, x1, y1, C_SEC, depth_k(230 * (tail - i) / tail, d1));
        }
    }
    bead(R_SEC, Z_SEC, head, C_SEC);

    lv_obj_invalidate(s_canvas);
}

static void refresh_text(const struct tm *tm, bool valid)
{
    if (valid) {
        for (int i = 0; i < CK_EXTRUDE; i++) {
            lv_label_set_text_fmt(s_time[i], "%02d:%02d", tm->tm_hour, tm->tm_min);
        }
        lv_label_set_text_static(s_day, clock_day_name(tm->tm_wday));
        lv_label_set_text_fmt(s_date, "%d %s %d", tm->tm_mday, clock_month_name(tm->tm_mon),
                              tm->tm_year + 1900);
        lv_label_set_text_static(s_hint, "");
    } else {
        for (int i = 0; i < CK_EXTRUDE; i++) {
            lv_label_set_text_static(s_time[i], "--:--");
        }
        lv_label_set_text_static(s_day, "");
        lv_label_set_text_static(s_date, "SAAT AYARLANMADI");
        lv_label_set_text_static(s_hint, "NAV modunda telefonu ba\xC4\x9Flay\xC4\xB1n");
    }
}

static lv_obj_t *mk_label(const lv_font_t *font, lv_color_t color, lv_coord_t y, int spacing)
{
    lv_obj_t *l = lv_label_create(s_root);
    lv_label_set_text_static(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, spacing, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

void screen_clock_create(void)
{
    const ui_theme_t *t = theme_get();

    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, root_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_buf = heap_caps_malloc(CK_CANVAS * CK_CANVAS * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_buf) {
        s_fx = (fx_canvas_t){ .px = s_buf, .w = CK_CANVAS, .h = CK_CANVAS };
        fx_clear(&s_fx, C_BG);
        s_canvas = lv_canvas_create(s_root);
        lv_canvas_set_buffer(s_canvas, s_buf, CK_CANVAS, CK_CANVAS, LV_IMG_CF_TRUE_COLOR);
        lv_obj_center(s_canvas);
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    }
    stars_init();

    s_day = mk_label(t->font_tr_sm, t->text_dim, CK_DAY_Y, 6);

    /* kabartma: arkadan öne, her katman 1 px aşağı-sağa kaymış ve koyudan
     * açığa — en öndeki parlak yüz */
    for (int i = 0; i < CK_EXTRUDE; i++) {
        int back = CK_EXTRUDE - 1 - i;
        lv_color_t col = i == CK_EXTRUDE - 1
                             ? lv_color_hex(0xEAF7FF)
                             : lv_color_mix(lv_color_hex(0x0A6E9C), lv_color_hex(0x02121C),
                                            (lv_opa_t)(255 * i / (CK_EXTRUDE - 1)));
        s_time[i] = mk_label(t->font_value, col, CK_TIME_Y, 2);
        lv_obj_align(s_time[i], LV_ALIGN_CENTER, back, CK_TIME_Y + back);
    }

    s_date = mk_label(t->font_tr_md, t->primary, CK_DATE_Y, 3);
    s_hint = mk_label(t->font_tr_sm, t->text_dim, CK_HINT_Y, 0);

    lv_obj_t *brand = mk_label(&lv_font_montserrat_14, t->text_dim, CK_BRAND_Y, 8);
    lv_label_set_text_static(brand, "AURA");
}

static void fade_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void screen_clock_show(void)
{
    if (!s_root || screen_clock_visible()) {
        return;
    }
    s_prev_min = -1;
    s_prev_valid = -1;
    s_last_frame = 0;
    screen_clock_update();

    lv_obj_set_style_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_root);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 450);
    lv_anim_set_exec_cb(&a, fade_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void screen_clock_hide(void)
{
    if (!s_root) {
        return;
    }
    lv_anim_del(s_root, fade_cb);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

bool screen_clock_visible(void)
{
    return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void screen_clock_update(void)
{
    if (lv_tick_elaps(s_last_frame) < CK_FRAME_MS) {
        return;
    }
    s_last_frame = lv_tick_get();

    struct tm tm;
    bool valid = clock_now(&tm);
    int key = valid ? tm.tm_yday * 1440 + tm.tm_hour * 60 + tm.tm_min : -1;
    if (key != s_prev_min || (int)valid != s_prev_valid) {
        s_prev_min = key;
        s_prev_valid = valid;
        refresh_text(&tm, valid);
    }
    if (s_buf) {
        render(&tm, valid);
    }
}
