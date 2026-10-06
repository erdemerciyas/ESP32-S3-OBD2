#include "screen_splash.h"
#include "theme.h"
#include "fx3d.h"
#include "bsp.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>

/* Açılış — three.js tarzı neon sahne, kendi yazılım 3D çizicimizle (fx3d):
 *
 *   0    – 1500 ms  yıldız akışı (warp), giderek hızlanır
 *   700  – 4000     ufka akan perspektif neon ızgara + çizgili gün batımı
 *   1100 – 2400     ortada dönen, parlayan tel kafes ikozahedron
 *   2400 – 3000     ikozahedron yukarı süzülüp amblem olur, AURA yazısı belirir
 *   2450 – 3010     buzzer: ta-ta-taaam (aktif buzzer: perde yok, ritim var)
 *   2700 – 3200     profil + canlı açılış durumu
 *   4000 – 4700     arka plana karar, sonra arayüz belirir
 *
 * Sahne 460×460 RGB565 tuvalde (PSRAM, yalnız açılışta) her karede yeniden
 * çizilir; etiketler LVGL nesneleri olarak üstte. */

#define SP_TOTAL_MS     4700
#define SP_FADE_IN_MS   300
#define SP_FRAME_MS     33
#define SP_SZ           UI_VIEWPORT_SZ
#define SP_HORIZON      300            /* tuval y: ufuk çizgisi */
#define SP_SUN_R        112
#define SP_STARS        150
#define SP_TITLE_Y      (-6)
#define SP_SUB_Y        54
#define SP_MOD_Y        82
#define SP_PROFILE_Y    114
#define SP_STATUS_Y     140

#define C_CYAN    FX_RGB(0, 230, 255)
#define C_MAGENTA FX_RGB(255, 40, 200)
#define C_ORANGE  FX_RGB(255, 145, 0)
#define C_STAR    FX_RGB(170, 220, 255)
#define C_BG      FX_RGB(2, 6, 10)

static lv_obj_t     *s_root;
static lv_obj_t     *s_canvas;
static uint16_t     *s_buf;
static fx_canvas_t   s_fx;
static lv_obj_t     *s_title;
static lv_obj_t     *s_sub;
static lv_obj_t     *s_mod;
static lv_obj_t     *s_profile;
static lv_obj_t     *s_status;
static lv_obj_t     *s_veil;
static lv_timer_t   *s_timer;
static lv_timer_cb_t s_finish_cb;
static uint32_t      s_t0;
static char          s_prev_status[64];
static bool          s_buzz;

static int16_t s_p_title = -1, s_p_ls = -1, s_p_sub = -1, s_p_info = -1, s_p_veil = -1;

typedef struct { float x, y, z; } star_t;
static star_t s_stars[SP_STARS];
static uint32_t s_seed = 0x0BD2u;

/* Buzzer pencereleri (ms): ta – ta – taaam */
static const uint16_t JINGLE[][2] = { {2450, 2510}, {2580, 2640}, {2750, 3010} };

/* 0..1 progress of t within [a, b] */
static float span(uint32_t t, uint32_t a, uint32_t b)
{
    if (t <= a) return 0.0f;
    if (t >= b) return 1.0f;
    return (float)(t - a) / (float)(b - a);
}

static float ease_out(float x)    { return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); }
static float ease_in_out(float x) { return x < 0.5f ? 4.0f * x * x * x
                                                    : 1.0f - (-2.0f * x + 2.0f) * (-2.0f * x + 2.0f) *
                                                             (-2.0f * x + 2.0f) / 2.0f; }

static lv_opa_t opa_of(float x) { return (lv_opa_t)(x * 255.0f); }

static bool changed(int16_t *prev, int16_t v)
{
    if (*prev == v) {
        return false;
    }
    *prev = v;
    return true;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text_static(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

/* --- sahne ------------------------------------------------------------------ */

static void star_reset(star_t *s, bool anywhere)
{
    float a = fx_rand(&s_seed) * 6.2831853f;
    float r = 0.15f + fx_rand(&s_seed) * 1.6f;
    s->x = cosf(a) * r;
    s->y = sinf(a) * r;
    s->z = anywhere ? 0.2f + fx_rand(&s_seed) * 3.8f : 4.0f;
}

static void draw_stars(float t_s, float fade)
{
    if (fade <= 0) return;
    /* hız: 1.5 → 9 birim/sn (warp) */
    float v = 1.5f + 7.5f * ease_in_out(fminf(1.0f, t_s / 1.3f));
    float dt = SP_FRAME_MS / 1000.0f;
    for (int i = 0; i < SP_STARS; i++) {
        star_t *s = &s_stars[i];
        float z0 = s->z;
        s->z -= v * dt;
        if (s->z < 0.12f) {
            star_reset(s, false);
            continue;
        }
        float x0, y0, x1, y1;
        fx_v3 a = { s->x, s->y, z0 + 0.10f * v };   /* iz: hızla uzar */
        fx_v3 b = { s->x, s->y, s->z };
        if (fx_project(a, 0, 150, SP_SZ / 2, SP_SZ / 2, &x0, &y0) &&
            fx_project(b, 0, 150, SP_SZ / 2, SP_SZ / 2, &x1, &y1)) {
            int k = (int)(fade * 255 * fminf(1.0f, 1.2f - s->z / 4.0f));
            fx_line(&s_fx, x0, y0, x1, y1, C_STAR, k);
        }
    }
}

static void draw_sun(float fade)
{
    if (fade <= 0) return;
    const int cx = SP_SZ / 2;
    for (int dy = 0; dy < SP_SUN_R; dy++) {
        int y = SP_HORIZON - dy;
        float h = (float)dy / SP_SUN_R;                 /* 0 ufuk, 1 tepe */
        /* alt yarıda klasik şeritler: boşluklar ufka doğru genişler */
        if (h < 0.5f && (dy % 12) < (int)(6.0f * (1.0f - 2.0f * h))) {
            continue;
        }
        float half = sqrtf((float)(SP_SUN_R * SP_SUN_R - dy * dy));
        int r = 255;
        int g = (int)(40 + 150 * h);                    /* tepe sarı-turuncu, alt pembe */
        int b = (int)(170 * (1 - h));
        fx_hspan(&s_fx, y, cx - half, cx + half, FX_RGB(r, g, b), (int)(fade * 120));
    }
}

static void draw_grid(float t_s, float fade)
{
    if (fade <= 0) return;
    const float cx = SP_SZ / 2.0f, cam_h = 1.0f, f = 180.0f;
    int k = (int)(fade * 200);
    /* ufuk parlaması */
    fx_glow_line(&s_fx, 0, SP_HORIZON, SP_SZ, SP_HORIZON, C_MAGENTA, k);
    /* boyuna çizgiler: kaybolma noktasına yakınsar */
    for (int i = -7; i <= 7; i++) {
        float x_near = cx + i * 1.0f * f / 0.6f;
        float x_far = cx + i * 1.0f * f / 30.0f;
        float y_near = SP_HORIZON + cam_h * f / 0.6f;
        float y_far = SP_HORIZON + cam_h * f / 30.0f;
        fx_line(&s_fx, x_far, y_far, x_near, y_near, C_MAGENTA, k * 2 / 3);
    }
    /* enine çizgiler: bize doğru akar */
    float off = fmodf(t_s * 2.2f, 1.0f);
    for (int j = 0; j < 16; j++) {
        float z = 0.6f + j - off;
        if (z < 0.6f) continue;
        float y = SP_HORIZON + cam_h * f / z;
        int kk = (int)(k * fminf(1.0f, 3.0f / z));
        fx_line(&s_fx, 0, y, SP_SZ, y, C_MAGENTA, kk);
    }
}

static void draw_ico(float t_s, float grow, float lift, float fade)
{
    if (fade <= 0 || grow <= 0) return;
    float scale = (0.15f + 0.85f * grow) * (1.0f - 0.62f * lift);
    float cx = SP_SZ / 2.0f;
    float cy = 210.0f - 104.0f * lift;
    float sx[12], sy[12];
    bool ok[12];
    for (int i = 0; i < 12; i++) {
        fx_v3 v = FX_ICO_V[i];
        v.x *= scale; v.y *= scale; v.z *= scale;
        v = fx_rotate(v, t_s * 0.7f, t_s * 1.1f, t_s * 0.25f);
        ok[i] = fx_project(v, 2.6f, 260.0f, cx, cy, &sx[i], &sy[i]);
    }
    int k = (int)(fade * 255);
    for (int e = 0; e < 30; e++) {
        int a = FX_ICO_E[e][0], b = FX_ICO_E[e][1];
        if (ok[a] && ok[b]) {
            fx_glow_line(&s_fx, sx[a], sy[a], sx[b], sy[b], C_CYAN, k);
        }
    }
    for (int i = 0; i < 12; i++) {
        if (ok[i]) {
            fx_dot(&s_fx, sx[i], sy[i], 2.5f * scale + 1.0f, C_ORANGE, k);
        }
    }
}

static void render(uint32_t t)
{
    float t_s = t / 1000.0f;
    float out = 1.0f - span(t, 3900, 4600);             /* sahne kararması */
    fx_clear(&s_fx, C_BG);
    draw_stars(t_s, (1.0f - span(t, 1300, 2200)) * span(t, 0, 300));
    draw_sun(span(t, 900, 1900) * out);
    draw_grid(t_s, span(t, 700, 1500) * out * (1.0f - 0.45f * span(t, 2500, 3200)));
    draw_ico(t_s, ease_out(span(t, 1100, 2000)), ease_in_out(span(t, 2400, 3000)), out);
    lv_obj_invalidate(s_canvas);
}

/* --- akış ----------------------------------------------------------------- */

static void buzz_update(uint32_t t)
{
    bool on = false;
    for (size_t i = 0; i < sizeof(JINGLE) / sizeof(JINGLE[0]); i++) {
        if (t >= JINGLE[i][0] && t < JINGLE[i][1]) {
            on = true;
        }
    }
    if (on != s_buzz) {
        s_buzz = on;
        if (on) bsp_buzzer_on(); else bsp_buzzer_off();
    }
}

static void veil_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void finish(void)
{
    lv_timer_del(s_timer);
    s_timer = NULL;
    if (s_buzz) {
        bsp_buzzer_off();
        s_buzz = false;
    }

    /* Hand over: the UI appears under a full veil on the top layer that then
     * fades out, so the cut from splash to dashboard is never visible. */
    lv_obj_t *veil = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(veil);
    lv_obj_set_size(veil, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(veil, theme_get()->bg, 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_COVER, 0);
    lv_obj_clear_flag(veil, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, veil);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, SP_FADE_IN_MS);
    lv_anim_set_exec_cb(&a, veil_opa_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, lv_obj_del_anim_ready_cb);
    lv_anim_start(&a);

    if (s_finish_cb) {
        s_finish_cb(NULL);      /* splash kökünü (tuval dahil) siler */
    }
    heap_caps_free(s_buf);      /* tuval nesnesi artık yok */
    s_buf = NULL;
}

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t t = lv_tick_get() - s_t0;
    if (t >= SP_TOTAL_MS) {
        finish();
        return;
    }

    if (s_buf) {
        render(t);
    }
    buzz_update(t);

    /* Yazı: harf aralığı daralarak belirir */
    float tf = ease_out(span(t, 2400, 3000));
    if (changed(&s_p_title, opa_of(tf))) {
        lv_obj_set_style_text_opa(s_title, (lv_opa_t)s_p_title, 0);
    }
    if (changed(&s_p_ls, (int16_t)(4 + (1.0f - tf) * 24.0f))) {
        lv_obj_set_style_text_letter_space(s_title, s_p_ls, 0);
    }
    if (changed(&s_p_sub, opa_of(span(t, 2600, 3100)))) {
        lv_obj_set_style_text_opa(s_sub, (lv_opa_t)s_p_sub, 0);
        lv_obj_set_style_text_opa(s_mod, (lv_opa_t)s_p_sub, 0);
    }
    if (changed(&s_p_info, opa_of(span(t, 2700, 3200)))) {
        lv_obj_set_style_text_opa(s_profile, (lv_opa_t)s_p_info, 0);
        lv_obj_set_style_text_opa(s_status, (lv_opa_t)s_p_info, 0);
    }

    const char *msg = vehicle_data_get()->status_msg;
    if (!msg[0]) {
        msg = "Starting";
    }
    if (strncmp(msg, s_prev_status, sizeof(s_prev_status) - 1) != 0) {
        strncpy(s_prev_status, msg, sizeof(s_prev_status) - 1);
        s_prev_status[sizeof(s_prev_status) - 1] = '\0';
        lv_label_set_text(s_status, s_prev_status);
    }

    /* Yazılar da kararır */
    if (changed(&s_p_veil, opa_of(span(t, 4000, SP_TOTAL_MS)))) {
        lv_obj_set_style_bg_opa(s_veil, (lv_opa_t)s_p_veil, 0);
    }
}

lv_obj_t *screen_splash_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 0);
    theme_apply_screen(s_root);

    s_buf = heap_caps_malloc(SP_SZ * SP_SZ * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_buf) {
        s_fx = (fx_canvas_t){ .px = s_buf, .w = SP_SZ, .h = SP_SZ };
        fx_clear(&s_fx, C_BG);
        s_canvas = lv_canvas_create(s_root);
        lv_canvas_set_buffer(s_canvas, s_buf, SP_SZ, SP_SZ, LV_IMG_CF_TRUE_COLOR);
        lv_obj_center(s_canvas);
        for (int i = 0; i < SP_STARS; i++) {
            star_reset(&s_stars[i], true);
        }
    }

    s_title = mk_label(s_root, t->font_value, t->text, SP_TITLE_Y);
    lv_label_set_text_static(s_title, "AURA");

    s_sub = mk_label(s_root, t->font_tr_sm, t->primary, SP_SUB_Y);
    lv_obj_set_style_text_letter_space(s_sub, 5, 0);
    lv_label_set_text_static(s_sub, "AKILLI ARAÇ SİSTEMİ");

    s_mod = mk_label(s_root, t->font_tr_sm, t->text_dim, SP_MOD_Y);
    lv_obj_set_style_text_letter_space(s_mod, 2, 0);
    lv_label_set_text_static(s_mod, "OBD  |  NAV  |  SENSÖR");

    s_profile = mk_label(s_root, t->font_md, t->text, SP_PROFILE_Y);
    lv_label_set_text(s_profile, vehicle_profile_get()->display_name);

    s_status = mk_label(s_root, &lv_font_montserrat_14, t->text_dim, SP_STATUS_Y);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status, 240);
    s_prev_status[0] = '\0';

    /* Veil for the closing fade (topmost child of the splash) */
    s_veil = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_veil);
    lv_obj_set_size(s_veil, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_veil, t->bg, 0);
    lv_obj_set_style_bg_opa(s_veil, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_veil, LV_OBJ_FLAG_CLICKABLE);

    return s_root;
}

void screen_splash_start(lv_obj_t *splash, lv_timer_cb_t on_finish)
{
    (void)splash;
    s_finish_cb = on_finish;
    s_t0 = lv_tick_get();
    s_timer = lv_timer_create(frame_cb, SP_FRAME_MS, NULL);
}
