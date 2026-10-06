#include "ui.h"
#include "theme.h"
#include "app_mode.h"
#include "fx3d.h"
#include "esp_heap_caps.h"
#include <math.h>

/* Mod seçimi — iki büyük yuvarlak karo: OBD (araç verisi) ve NAV (telefon
 * navigasyonu). Seçilen mod vurgulanır; dokununca radyo geçişi arka planda
 * başlar ve ilgili görünüm hemen açılır. Arka planda yavaş dönen sönük tel
 * kafes (fx3d) — yalnızca ekran görünürken, 15 fps. */

#define HM_TILE_D   168
#define HM_TILE_DX  92
#define HM_TITLE_Y  (-158)
#define HM_SUB_Y    (-130)
#define HM_RING_D   (HM_TILE_D + 16)
#define HM_BG_SZ    360
#define HM_BG_MS    66
#define HM_INFO_Y   150

typedef struct {
    lv_obj_t  *obj;
    lv_obj_t  *ring;
    lv_obj_t  *icon;
    lv_color_t accent;
} home_tile_t;

static home_tile_t s_tiles[2];
static lv_obj_t *s_info;
static int s_prev_sel = -1;
static int s_prev_busy = -1;
static lv_obj_t *s_bg;
static uint16_t *s_bg_buf;
static fx_canvas_t s_bg_fx;
static uint32_t s_bg_last;

static void tile_click_cb(lv_event_t *e)
{
    app_mode_t m = (app_mode_t)(intptr_t)lv_event_get_user_data(e);
    app_mode_set(m);
    ui_show_view(m == APP_MODE_NAV ? UI_VIEW_NAV : UI_VIEW_OBD);
}

static void create_tile(lv_obj_t *root, app_mode_t m, lv_coord_t x, const char *icon,
                        const char *name, const char *sub, lv_color_t accent)
{
    const ui_theme_t *t = theme_get();
    home_tile_t *tl = &s_tiles[m];

    tl->accent = accent;

    /* ışıltı halkası (seçiliyken vurgu renginde) */
    tl->ring = lv_obj_create(root);
    lv_obj_remove_style_all(tl->ring);
    lv_obj_set_size(tl->ring, HM_RING_D, HM_RING_D);
    lv_obj_set_style_radius(tl->ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(tl->ring, 2, 0);
    lv_obj_align(tl->ring, LV_ALIGN_CENTER, x, 0);
    lv_obj_clear_flag(tl->ring, LV_OBJ_FLAG_CLICKABLE);

    tl->obj = lv_obj_create(root);
    theme_apply_lens(tl->obj, HM_TILE_D);
    lv_obj_align(tl->obj, LV_ALIGN_CENTER, x, 0);
    lv_obj_set_style_border_width(tl->obj, 3, 0);
    lv_obj_set_style_border_opa(tl->obj, LV_OPA_COVER, 0);
    lv_obj_add_flag(tl->obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tl->obj, tile_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)m);

    tl->icon = lv_label_create(tl->obj);
    lv_label_set_text_static(tl->icon, icon);
    lv_obj_set_style_text_font(tl->icon, t->font_lg, 0);
    lv_obj_align(tl->icon, LV_ALIGN_CENTER, 0, -36);

    lv_obj_t *nm = lv_label_create(tl->obj);
    lv_label_set_text_static(nm, name);
    lv_obj_set_style_text_font(nm, t->font_lg, 0);
    lv_obj_set_style_text_color(nm, t->text, 0);
    lv_obj_align(nm, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *sb = lv_label_create(tl->obj);
    lv_label_set_text_static(sb, sub);
    lv_obj_set_style_text_font(sb, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(sb, t->text_dim, 0);
    lv_obj_align(sb, LV_ALIGN_CENTER, 0, 42);
}

lv_obj_t *screen_home_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_bg_buf = heap_caps_malloc(HM_BG_SZ * HM_BG_SZ * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_bg_buf) {
        s_bg_fx = (fx_canvas_t){ .px = s_bg_buf, .w = HM_BG_SZ, .h = HM_BG_SZ };
        fx_clear(&s_bg_fx, FX_RGB(2, 6, 10));
        s_bg = lv_canvas_create(root);
        lv_canvas_set_buffer(s_bg, s_bg_buf, HM_BG_SZ, HM_BG_SZ, LV_IMG_CF_TRUE_COLOR);
        lv_obj_center(s_bg);
    }

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text_static(title, "AURA");
    lv_obj_set_style_text_font(title, t->font_md, 0);
    lv_obj_set_style_text_color(title, t->text, 0);
    lv_obj_set_style_text_letter_space(title, 8, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, HM_TITLE_Y);

    lv_obj_t *sub = lv_label_create(root);
    lv_label_set_text_static(sub, "Mod seçin");
    lv_obj_set_style_text_font(sub, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(sub, t->text_dim, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, HM_SUB_Y);

    create_tile(root, APP_MODE_OBD, -HM_TILE_DX, LV_SYMBOL_CHARGE, "OBD", "Araç verisi", t->primary);
    create_tile(root, APP_MODE_NAV, HM_TILE_DX, LV_SYMBOL_GPS, "NAV", "Navigasyon", t->secondary);

    s_info = lv_label_create(root);
    lv_label_set_text_static(s_info, "");
    lv_obj_set_style_text_font(s_info, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_info, t->text_dim, 0);
    lv_obj_align(s_info, LV_ALIGN_CENTER, 0, HM_INFO_Y);

    screen_home_update();
    return root;
}

/* Yavaş dönen sönük ikozahedron + çevresinde yörünge noktaları */
static void render_bg(void)
{
    if (!s_bg_buf) {
        return;
    }
    float ts = lv_tick_get() / 1000.0f;
    const float c = HM_BG_SZ / 2.0f;
    fx_clear(&s_bg_fx, FX_RGB(2, 6, 10));
    float sx[12], sy[12];
    for (int i = 0; i < 12; i++) {
        fx_v3 v = fx_rotate(FX_ICO_V[i], ts * 0.21f, ts * 0.33f, 0.4f);
        fx_project(v, 2.4f, 400.0f, c, c, &sx[i], &sy[i]);
    }
    for (int e = 0; e < 30; e++) {
        int a = FX_ICO_E[e][0], b = FX_ICO_E[e][1];
        fx_line(&s_bg_fx, sx[a], sy[a], sx[b], sy[b], FX_RGB(0, 200, 255), 70);
    }
    for (int i = 0; i < 12; i++) {
        fx_dot(&s_bg_fx, sx[i], sy[i], 2.0f, FX_RGB(255, 145, 0), 110);
    }
    for (int i = 0; i < 24; i++) {   /* eğik yörünge halkası */
        float a = ts * 0.5f + i * 0.2618f;
        fx_v3 p = fx_rotate((fx_v3){ cosf(a) * 1.35f, 0, sinf(a) * 1.35f }, 0.5f, 0, 0.3f);
        float px, py;
        if (fx_project(p, 2.4f, 400.0f, c, c, &px, &py)) {
            fx_dot(&s_bg_fx, px, py, 1.2f, FX_RGB(170, 220, 255), 90);
        }
    }
    lv_obj_invalidate(s_bg);
}

void screen_home_update(void)
{
    const ui_theme_t *t = theme_get();
    int sel = (int)app_mode_get();
    int busy = app_mode_is_switching() ? 1 : 0;

    if (lv_tick_elaps(s_bg_last) >= HM_BG_MS) {
        s_bg_last = lv_tick_get();
        render_bg();
    }

    if (sel != s_prev_sel) {
        s_prev_sel = sel;
        for (int i = 0; i < 2; i++) {
            bool on = i == sel;
            lv_obj_set_style_border_color(s_tiles[i].obj, on ? s_tiles[i].accent : t->border, 0);
            lv_obj_set_style_text_color(s_tiles[i].icon, on ? s_tiles[i].accent : t->text_dim, 0);
            lv_obj_set_style_border_color(s_tiles[i].ring, on ? s_tiles[i].accent : t->border, 0);
            lv_obj_set_style_border_opa(s_tiles[i].ring, on ? LV_OPA_50 : LV_OPA_20, 0);
        }
    }
    if (busy != s_prev_busy) {
        s_prev_busy = busy;
        lv_label_set_text_static(s_info, busy ? "Geçiliyor..." : "");
    }
}
