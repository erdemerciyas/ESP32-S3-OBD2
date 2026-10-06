#include "screen_nav_pages.h"
#include "theme.h"
#include "nav_track.h"
#include <stdio.h>

/* Rota özeti: gidilen mesafe, süre, ortalama hız ve geçilen yollar
 * (en yenisi üstte). Sürüş kaydı değişince saniyede en çok bir kez yazılır. */

#define TP_ROADS      6
#define TP_TITLE_Y    (-160)
#define TP_DIST_Y     (-92)
#define TP_STAT_Y     (-24)
#define TP_STAT_DX    80
#define TP_ROAD_Y0    32
#define TP_ROAD_DY    25
#define TP_ROAD_W     300
#define TP_REDRAW_MS  1000

static lv_obj_t *s_dist;
static lv_obj_t *s_dur;
static lv_obj_t *s_avg;
static lv_obj_t *s_roads[TP_ROADS];
static lv_obj_t *s_empty;
static char s_names[NAV_TRACK_MAX_ROADS][NAV_TEXT_LEN];
static uint32_t s_prev_rev = UINT32_MAX;
static uint32_t s_last_draw;

void nav_format_duration(uint32_t s, char *buf, size_t len)
{
    uint32_t m = s / 60;
    if (m < 60) {
        snprintf(buf, len, "%u dk", (unsigned)m);
    } else {
        snprintf(buf, len, "%u sa %02u dk", (unsigned)(m / 60), (unsigned)(m % 60));
    }
}

static lv_obj_t *stat(lv_obj_t *root, lv_coord_t x, const char *caption)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *v = lv_label_create(root);
    lv_label_set_text_static(v, "--");
    lv_obj_set_style_text_font(v, t->font_md, 0);
    lv_obj_set_style_text_color(v, t->text, 0);
    lv_obj_align(v, LV_ALIGN_CENTER, x, TP_STAT_Y);

    lv_obj_t *c = lv_label_create(root);
    lv_label_set_text_static(c, caption);
    lv_obj_set_style_text_font(c, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(c, t->text_dim, 0);
    lv_obj_align(c, LV_ALIGN_CENTER, x, TP_STAT_Y + 22);
    return v;
}

lv_obj_t *nav_trip_page_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text_static(title, "ROTA");
    lv_obj_set_style_text_font(title, t->font_md, 0);
    lv_obj_set_style_text_color(title, t->text_dim, 0);
    lv_obj_set_style_text_letter_space(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, TP_TITLE_Y);

    s_dist = lv_label_create(root);
    lv_label_set_text_static(s_dist, "0.0 km");
    lv_obj_set_style_text_font(s_dist, t->font_xl, 0);
    lv_obj_set_style_text_color(s_dist, t->primary, 0);
    lv_obj_align(s_dist, LV_ALIGN_CENTER, 0, TP_DIST_Y);

    s_dur = stat(root, -TP_STAT_DX, "SÜRE");
    s_avg = stat(root, TP_STAT_DX, "ORT. HIZ");

    for (int i = 0; i < TP_ROADS; i++) {
        s_roads[i] = lv_label_create(root);
        lv_label_set_text_static(s_roads[i], "");
        lv_label_set_long_mode(s_roads[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_roads[i], TP_ROAD_W - i * 20);   /* daire daralıyor */
        lv_obj_set_style_text_align(s_roads[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s_roads[i], t->font_tr_sm, 0);
        lv_obj_set_style_text_color(s_roads[i], i == 0 ? t->text : t->text_dim, 0);
        lv_obj_align(s_roads[i], LV_ALIGN_CENTER, 0, TP_ROAD_Y0 + i * TP_ROAD_DY);
    }

    s_empty = lv_label_create(root);
    lv_label_set_text_static(s_empty, "Rota başlayınca kaydedilir");
    lv_obj_set_style_text_font(s_empty, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_empty, t->text_dim, 0);
    lv_obj_align(s_empty, LV_ALIGN_CENTER, 0, TP_ROAD_Y0 + TP_ROAD_DY);

    return root;
}

void nav_trip_page_update(bool force)
{
    uint32_t now = lv_tick_get();
    uint32_t rev = nav_track_rev();
    if (!force && (rev == s_prev_rev || now - s_last_draw < TP_REDRAW_MS)) {
        return;
    }
    s_prev_rev = rev;
    s_last_draw = now;

    char buf[32];
    float m = nav_track_distance_m();
    uint32_t s = nav_track_duration_s();
    snprintf(buf, sizeof(buf), "%.1f km", m / 1000.0f);
    lv_label_set_text(s_dist, buf);
    nav_format_duration(s, buf, sizeof(buf));
    lv_label_set_text(s_dur, buf);
    if (s >= 30) {
        snprintf(buf, sizeof(buf), "%d km/h", (int)(m / s * 3.6f + 0.5f));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_avg, buf);

    int n = nav_track_roads(s_names, NAV_TRACK_MAX_ROADS);
    for (int i = 0; i < TP_ROADS; i++) {
        /* en yenisi üstte */
        lv_label_set_text(s_roads[i], i < n ? s_names[n - 1 - i] : "");
    }
    if (n) {
        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    }
}
