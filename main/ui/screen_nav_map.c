#include "screen_nav_pages.h"
#include "theme.h"
#include "nav_map.h"
#include "nav_track.h"
#include "nav_service.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Harita sayfası: telefonun gönderdiği karanlık OSM resmi (yoksa düz zemin),
 * üzerinde sürüş kaydının çizgisi ve konum işareti. Projeksiyon Web Mercator;
 * resim yoksa rota ekrana sığdırılır. Çizgi saniyede en çok bir kez yeniden
 * hesaplanır, resim yalnızca yenisi gelince değişir. */

#define MP_SZ          UI_VIEWPORT_SZ
#define MP_LINE_MAX    300
#define MP_LINE_W      6
#define MP_ME_D        22
#define MP_FIT_PX      340     /* resimsiz modda rotanın sığdırıldığı kutu */
#define MP_REDRAW_MS   1000
#define MP_TOP_Y       (-146)
#define MP_BOT_Y       150
#define MP_BTN_D       60
#define MP_BTN_DX      150
#define MP_BTN_Y       96
#define MP_FIT_Y       146
#define MP_ZOOM_MIN    8
#define MP_ZOOM_MAX    18

static lv_obj_t *s_img;
static lv_img_dsc_t s_dsc[2];
static int s_dsc_i;
static lv_obj_t *s_wait;
static lv_obj_t *s_line;
static lv_obj_t *s_me;
static lv_obj_t *s_top;
static lv_obj_t *s_top_lbl;

static nav_map_img_t s_map;
static bool s_has_map;
static lv_point_t s_pts[MP_LINE_MAX];
static nav_track_pt_t s_trk[MP_LINE_MAX];
static uint32_t s_prev_track_rev = UINT32_MAX;
static uint32_t s_last_draw;

/* Yakınlaştırma: varsayılan "tümü" (telefon rotayı sığdırır). +/- telefondan o
 * zoom'da araca ortalı harita ister; gelene kadar mevcut resim LVGL ile
 * büyütülür (s_preview), çizgi de aynı oranla çizilir. */
static int    s_zoom_target = -1;   /* -1: tümü (fit) */
static double s_preview = 1.0;
static bool   s_force;

/* Projeksiyon: dünya pikseli → sayfa pikseli */
static double s_cx, s_cy, s_scale;

static void merc(double lat, double lon, double *x, double *y)
{
    double s = sin(lat * M_PI / 180.0);
    *x = (lon + 180.0) / 360.0;
    *y = 0.5 - log((1 + s) / (1 - s)) / (4 * M_PI);
}

static void to_screen(double lat, double lon, lv_coord_t *px, lv_coord_t *py)
{
    double x, y;
    merc(lat, lon, &x, &y);
    double sx = (x - s_cx) * s_scale + MP_SZ / 2.0;
    double sy = (y - s_cy) * s_scale + MP_SZ / 2.0;
    *px = (lv_coord_t)fmax(-4000, fmin(4000, sx));
    *py = (lv_coord_t)fmax(-4000, fmin(4000, sy));
}

/* Resim varsa onun merkez/zoom'u; yoksa rotanın sınır kutusuna sığdır. */
static void setup_projection(size_t n, const nav_state_t *ns)
{
    if (s_has_map) {
        merc(s_map.lat, s_map.lon, &s_cx, &s_cy);
        s_scale = 256.0 * (double)(1u << s_map.zoom) * s_preview;
        return;
    }
    double minx = 1, maxx = 0, miny = 1, maxy = 0;
    for (size_t i = 0; i < n; i++) {
        double x, y;
        merc(s_trk[i].lat_e6 / 1e6, s_trk[i].lon_e6 / 1e6, &x, &y);
        minx = fmin(minx, x); maxx = fmax(maxx, x);
        miny = fmin(miny, y); maxy = fmax(maxy, y);
    }
    if (n == 0 && ns->has_fix) {
        merc(ns->lat, ns->lon, &minx, &miny);
        maxx = minx;
        maxy = miny;
    }
    s_cx = (minx + maxx) / 2;
    s_cy = (miny + maxy) / 2;
    double span = fmax(maxx - minx, maxy - miny);
    double max_scale = 256.0 * (1u << 17);       /* çok yakına girme (~1 m/px) */
    s_scale = span > 0 ? fmin(MP_FIT_PX / span, max_scale) : max_scale;
}

static void apply_preview(void)
{
    s_preview = (s_has_map && s_zoom_target >= 0) ? pow(2.0, s_zoom_target - s_map.zoom) : 1.0;
    double z = 256.0 * s_preview;
    lv_img_set_zoom(s_img, (uint16_t)fmax(32, fmin(2048, z)));
    s_force = true;
}

static void zoom_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int base = s_zoom_target >= 0 ? s_zoom_target : (s_has_map ? s_map.zoom : 15);
    int z = base + d;
    if (z < MP_ZOOM_MIN) z = MP_ZOOM_MIN;
    if (z > MP_ZOOM_MAX) z = MP_ZOOM_MAX;
    if (z == s_zoom_target) {
        return;
    }
    s_zoom_target = z;
    apply_preview();
    nav_service_request_map(false, z);
}

static void fit_cb(lv_event_t *e)
{
    (void)e;
    s_zoom_target = -1;
    apply_preview();
    nav_service_request_map(true, 0);
}

static lv_obj_t *round_btn(lv_obj_t *root, const char *txt, lv_coord_t x, lv_coord_t y,
                           lv_event_cb_t cb, intptr_t arg)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *b = lv_obj_create(root);
    theme_apply_lens(b, MP_BTN_D);
    lv_obj_set_style_bg_color(b, t->bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_border_color(b, t->primary, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_align(b, LV_ALIGN_CENTER, x, y);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 10);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)arg);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_static(l, txt);
    lv_obj_set_style_text_font(l, t->font_lg, 0);
    lv_obj_set_style_text_color(l, t->text, 0);
    lv_obj_center(l);
    return b;
}

static void create_zoom_controls(lv_obj_t *root)
{
    const ui_theme_t *t = theme_get();
    round_btn(root, "-", -MP_BTN_DX, MP_BTN_Y, zoom_cb, -1);
    round_btn(root, "+", MP_BTN_DX, MP_BTN_Y, zoom_cb, 1);

    lv_obj_t *fit = lv_obj_create(root);
    lv_obj_remove_style_all(fit);
    lv_obj_set_size(fit, 96, 34);
    lv_obj_set_style_radius(fit, 17, 0);
    lv_obj_set_style_bg_color(fit, t->bg, 0);
    lv_obj_set_style_bg_opa(fit, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(fit, t->surface_hi, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(fit, t->primary, 0);
    lv_obj_set_style_border_width(fit, 2, 0);
    lv_obj_align(fit, LV_ALIGN_CENTER, 0, MP_FIT_Y);
    lv_obj_clear_flag(fit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(fit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(fit, 8);
    lv_obj_add_event_cb(fit, fit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(fit);
    lv_label_set_text_static(l, "TÜMÜ");
    lv_obj_set_style_text_font(l, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(l, t->text, 0);
    lv_obj_center(l);
}

lv_obj_t *nav_map_page_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, MP_SZ, MP_SZ);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_img = lv_img_create(root);
    lv_obj_align(s_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);

    s_wait = lv_label_create(root);
    lv_label_set_text_static(s_wait, "Harita bekleniyor");
    lv_obj_set_style_text_font(s_wait, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_wait, t->text_dim, 0);
    lv_obj_align(s_wait, LV_ALIGN_CENTER, 0, 110);

    s_line = lv_line_create(root);
    lv_obj_set_size(s_line, MP_SZ, MP_SZ);
    lv_obj_set_pos(s_line, 0, 0);
    lv_obj_set_style_line_width(s_line, MP_LINE_W, 0);
    lv_obj_set_style_line_rounded(s_line, true, 0);
    lv_obj_set_style_line_color(s_line, t->primary, 0);
    lv_line_set_points(s_line, s_pts, 0);

    s_me = lv_obj_create(root);
    lv_obj_remove_style_all(s_me);
    lv_obj_set_size(s_me, MP_ME_D, MP_ME_D);
    lv_obj_set_style_radius(s_me, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_me, t->secondary, 0);
    lv_obj_set_style_bg_opa(s_me, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_me, t->text, 0);
    lv_obj_set_style_border_width(s_me, 3, 0);
    lv_obj_add_flag(s_me, LV_OBJ_FLAG_HIDDEN);

    s_top = lv_obj_create(root);
    lv_obj_remove_style_all(s_top);
    lv_obj_set_size(s_top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_top, t->bg, 0);
    lv_obj_set_style_bg_opa(s_top, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_top, 16, 0);
    lv_obj_set_style_pad_hor(s_top, 14, 0);
    lv_obj_set_style_pad_ver(s_top, 6, 0);
    lv_obj_align(s_top, LV_ALIGN_CENTER, 0, MP_TOP_Y);
    lv_obj_clear_flag(s_top, LV_OBJ_FLAG_CLICKABLE);
    s_top_lbl = lv_label_create(s_top);
    lv_label_set_text_static(s_top_lbl, "");
    lv_obj_set_style_text_font(s_top_lbl, t->font_tr_md, 0);
    lv_obj_set_style_text_color(s_top_lbl, t->text, 0);

    create_zoom_controls(root);
    return root;
}

static void place_me(const nav_state_t *ns)
{
    static lv_coord_t px = INT16_MIN, py = INT16_MIN;
    if (!ns->has_fix) {
        lv_obj_add_flag(s_me, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_coord_t x, y;
    to_screen(ns->lat, ns->lon, &x, &y);
    if (x != px || y != py) {   /* yalnızca hareket edince yeniden çiz */
        px = x;
        py = y;
        lv_obj_set_pos(s_me, x - MP_ME_D / 2, y - MP_ME_D / 2);
    }
    lv_obj_clear_flag(s_me, LV_OBJ_FLAG_HIDDEN);
}

void nav_map_page_update(const nav_state_t *ns, bool force)
{
    uint32_t now = lv_tick_get();
    bool new_map = false;

    if (nav_map_take(&s_map)) {
        s_has_map = true;
        new_map = true;
        s_dsc_i ^= 1;
        lv_img_dsc_t *d = &s_dsc[s_dsc_i];
        memset(d, 0, sizeof(*d));
        d->header.cf = LV_IMG_CF_TRUE_COLOR;
        d->header.w = s_map.w;
        d->header.h = s_map.h;
        d->data_size = (uint32_t)s_map.w * s_map.h * sizeof(uint16_t);
        d->data = (const uint8_t *)s_map.pixels;
        lv_img_set_src(s_img, d);
        apply_preview();   /* istenen zoom geldiyse önizleme biter */
        if (s_zoom_target >= 0 && s_map.zoom != s_zoom_target) {
            /* Telefon (ör. yeniden başlayıp) başka modda: seçili zoom'u yeniden iste */
            nav_service_request_map(false, s_zoom_target);
        }
        lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wait, LV_OBJ_FLAG_HIDDEN);
    }

    uint32_t rev = nav_track_rev();
    bool redraw = force || s_force || new_map ||
                  (rev != s_prev_track_rev && now - s_last_draw >= MP_REDRAW_MS);
    if (redraw) {
        s_force = false;
        s_prev_track_rev = rev;
        s_last_draw = now;
        size_t n = nav_track_copy(s_trk, MP_LINE_MAX);
        setup_projection(n, ns);
        for (size_t i = 0; i < n; i++) {
            to_screen(s_trk[i].lat_e6 / 1e6, s_trk[i].lon_e6 / 1e6, &s_pts[i].x, &s_pts[i].y);
        }
        lv_line_set_points(s_line, s_pts, (uint16_t)(n > 1 ? n : 0));

        char buf[48], dur[20];
        float km = nav_track_distance_m() / 1000.0f;
        nav_format_duration(nav_track_duration_s(), dur, sizeof(dur));
        snprintf(buf, sizeof(buf), "%.1f km   %s", km, dur);
        lv_label_set_text(s_top_lbl, buf);
    }
    place_me(ns);
}
