#include "ui.h"
#include "theme.h"
#include "app_mode.h"
#include "nav_state.h"
#include "screen_nav_pages.h"
#include "clock.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

/* Navigasyon — round-panel layout (y = merkezden ofset).
 *
 *   -186  bağlantı durumu
 *    -88  büyük manevra oku (160×160, lv_line ile çizilir — görsel varlık yok)
 *    +26  manevraya mesafe  "800 m"
 *    +70  manevra metni     "SAĞA DÖN"
 *   +100  yol adı
 *   +150  VARIŞ · KALAN · HIZ (renkli cam haplar)
 *   +207  ana menü düğmesi (OBD'deki nokta çubuğuyla aynı hizada)
 *
 * Arkada canlı fx3d sahnesi (screen_nav_bg.c); kenarda manevraya yaklaşma
 * halkası. Boştayken (telefon / rota yok) büyük saat + tarih + hız ve yol.
 *
 * Yalnızca nav_state_rev() değişince ve metin gerçekten farklıysa yazılır;
 * kayan yol adının animasyonu her güncellemede sıfırlanmaz. */

#define NV_ARROW_SZ     160
#define NV_ARROW_Y      (-88)
#define NV_LINE_W       20
#define NV_RING_R       38
#define NV_RING_W       14
#define NV_STATUS_Y     (-186)
#define NV_DIST_Y       26
#define NV_MAN_Y        70
#define NV_ROAD_Y       100
#define NV_ROAD_W       320
#define NV_INFO_Y       146
#define NV_INFO_DX      100
#define NV_INFO_W       92
#define NV_PILL_H       52
#define NV_PROX_D       448     /* yaklaşma halkası (sayfa noktalarının dışında) */
#define NV_PROX_W       6
#define NV_PROX_NEAR_M  100
#define NV_PROX_MID_M   300
#define NV_IDLE_TIME_Y  (-70)
#define NV_IDLE_DATE_Y  (-6)
#define NV_IDLE_TITLE_Y 40
#define NV_IDLE_SUB_Y   68
#define NV_IDLE_LIVE_Y  112
#define NV_STALE_MS     5000
#define NV_POLL_MS      250
#define NV_ALERT_Y      (-174)
#define NV_ALERT_W      260
#define NV_ALERT_H      36
#define NV_ALERT_MS     8000    /* yenilenmeyen uyarı bu kadar sonra kalkar */

/* --- Manevra okları (sağ elli tanımlar; sol olanlar aynalanır) ------------ */

static const lv_point_t P_STRAIGHT[] = { {80, 150}, {80, 36} };
static const lv_point_t H_STRAIGHT[] = { {40, 76}, {80, 30}, {120, 76} };
static const lv_point_t P_RIGHT[]    = { {52, 150}, {52, 84}, {126, 84} };
static const lv_point_t H_RIGHT[]    = { {94, 44}, {134, 84}, {94, 124} };
static const lv_point_t P_SRIGHT[]   = { {64, 150}, {64, 104}, {120, 48} };
static const lv_point_t H_SRIGHT[]   = { {76, 42}, {126, 42}, {126, 92} };
static const lv_point_t P_UTURN[]    = { {112, 150}, {112, 72}, {108, 56}, {97, 45}, {80, 40},
                                         {63, 45}, {52, 56}, {48, 72}, {48, 112} };
static const lv_point_t H_UTURN[]    = { {20, 88}, {48, 118}, {76, 88} };
static const lv_point_t P_ROUND[]    = { {80, 156}, {80, 128} };   /* giriş; çıkış = H_ROUND */
static const lv_point_t H_ROUND[]    = { {80, 50}, {80, 20} };
static const lv_point_t H_ROUND2[]   = { {54, 42}, {80, 16}, {106, 42} };
static const lv_point_t P_ARRIVE[]   = { {56, 152}, {56, 20} };
static const lv_point_t H_ARRIVE[]   = { {56, 24}, {128, 48}, {56, 72} };

static lv_point_t P_LEFT[3], H_LEFT[3], P_SLEFT[3], H_SLEFT[3];

#define N(a) (uint16_t)(sizeof(a) / sizeof((a)[0]))

static void mirror(lv_point_t *dst, const lv_point_t *src, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i].x = NV_ARROW_SZ - src[i].x;
        dst[i].y = src[i].y;
    }
}

/* --- Nesneler -------------------------------------------------------------- */

static lv_obj_t *s_guide;           /* rehberlik grubu */
static lv_obj_t *s_line_path;
static lv_obj_t *s_line_head;
static lv_obj_t *s_line_extra;      /* döner kavşak çıkış başı */
static lv_obj_t *s_glow[3];           /* oklar için neon hale (aynı noktalar, kalın, saydam) */
static lv_obj_t *s_ring;
static lv_obj_t *s_dist_val;
static lv_obj_t *s_dist_unit;
static lv_obj_t *s_man_lbl;
static lv_obj_t *s_road_lbl;
static lv_obj_t *s_eta_val;
static lv_obj_t *s_rem_val;
static lv_obj_t *s_spd_val;
static lv_obj_t *s_spd_pill;
static lv_obj_t *s_prox;            /* manevraya yaklaşma halkası */
static uint32_t  s_prox_start;      /* manevra ilk görüldüğündeki mesafe */
static int       s_prev_over = -1;  /* hız radar limitini aşıyor */
static lv_obj_t *s_status;
static lv_obj_t *s_msg;             /* bağlantı yok / rota yok grubu */
static lv_obj_t *s_msg_title;
static lv_obj_t *s_msg_sub;
static lv_obj_t *s_idle_time;
static lv_obj_t *s_idle_date;
static lv_obj_t *s_idle_live;       /* hız · yol (telefon GPS) */
static int       s_bg_mode = NAV_BG_IDLE;
static lv_obj_t *s_alert;           /* radar / trafik şeridi */
static lv_obj_t *s_alert_lbl;
static char      s_prev_alert[96];

/* Sayfalar: 0 rehberlik, 1 harita, 2 rota özeti — ekrana dokununca sıradaki */
#define NV_PAGES        3
#define NV_DOTS_X       212     /* sayfa noktaları: sağ kenar */
#define NV_HOME_Y       194     /* MENÜ hapı: alt kenar */
#define NV_HOME_W       112
#define NV_HOME_H       32
static lv_obj_t *s_page_root[NV_PAGES];
static lv_obj_t *s_page_dot[NV_PAGES];
static int       s_page;
static bool      s_page_dirty = true;

/* Son yazılanlar — yalnızca değişince yeniden çizilir. */
static uint32_t s_prev_rev = UINT32_MAX;
static uint32_t s_last_poll;
static int      s_prev_view = -1;
static int      s_prev_man = -1;
static int      s_prev_tone = -1;
static uint32_t s_prev_sc = UINT32_MAX;
static nav_state_t s_snap;

enum { VIEW_SWITCHING = 0, VIEW_NO_PHONE, VIEW_NO_ROUTE, VIEW_GUIDE };

static void set_text_if(lv_obj_t *lbl, const char *txt)
{
    const char *cur = lv_label_get_text(lbl);
    if (!cur || strcmp(cur, txt) != 0) {
        lv_label_set_text(lbl, txt);
    }
}

static const char *maneuver_text(nav_maneuver_t m)
{
    switch (m) {
    case NAV_MAN_STRAIGHT:     return "DÜZ DEVAM";
    case NAV_MAN_LEFT:         return "SOLA DÖN";
    case NAV_MAN_RIGHT:        return "SAĞA DÖN";
    case NAV_MAN_SLIGHT_LEFT:  return "HAFİF SOLA";
    case NAV_MAN_SLIGHT_RIGHT: return "HAFİF SAĞA";
    case NAV_MAN_UTURN:        return "U DÖNÜŞÜ";
    case NAV_MAN_ROUNDABOUT:   return "DÖNEL KAVŞAK";
    case NAV_MAN_ARRIVAL:      return "VARIŞ";
    default:                   return "";
    }
}

/* "800" + "m" / "1.2" + "km" / "12" + "km" */
static void format_dist(uint32_t m, char *val, size_t vlen, const char **unit)
{
    if (m < 1000) {
        snprintf(val, vlen, "%u", (unsigned)(m < 100 ? m : (m + 5) / 10 * 10));
        *unit = "m";
    } else if (m < 10000) {
        snprintf(val, vlen, "%u.%u", (unsigned)(m / 1000), (unsigned)(m % 1000 / 100));
        *unit = "km";
    } else {
        snprintf(val, vlen, "%u", (unsigned)((m + 500) / 1000));
        *unit = "km";
    }
}

static lv_obj_t *make_line(lv_obj_t *parent)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, NV_LINE_W, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

static void apply_maneuver(nav_maneuver_t m)
{
    const lv_point_t *p = NULL, *h = NULL, *x = NULL;
    uint16_t np = 0, nh = 0, nx = 0;

    switch (m) {
    case NAV_MAN_LEFT:         p = P_LEFT;   np = N(P_LEFT);   h = H_LEFT;   nh = N(H_LEFT);   break;
    case NAV_MAN_RIGHT:        p = P_RIGHT;  np = N(P_RIGHT);  h = H_RIGHT;  nh = N(H_RIGHT);  break;
    case NAV_MAN_SLIGHT_LEFT:  p = P_SLEFT;  np = N(P_SLEFT);  h = H_SLEFT;  nh = N(H_SLEFT);  break;
    case NAV_MAN_SLIGHT_RIGHT: p = P_SRIGHT; np = N(P_SRIGHT); h = H_SRIGHT; nh = N(H_SRIGHT); break;
    case NAV_MAN_UTURN:        p = P_UTURN;  np = N(P_UTURN);  h = H_UTURN;  nh = N(H_UTURN);  break;
    case NAV_MAN_ROUNDABOUT:   p = P_ROUND;  np = N(P_ROUND);  h = H_ROUND;  nh = N(H_ROUND);
                               x = H_ROUND2; nx = N(H_ROUND2); break;
    case NAV_MAN_ARRIVAL:      p = P_ARRIVE; np = N(P_ARRIVE); h = H_ARRIVE; nh = N(H_ARRIVE); break;
    default:                   p = P_STRAIGHT; np = N(P_STRAIGHT); h = H_STRAIGHT; nh = N(H_STRAIGHT); break;
    }

    lv_line_set_points(s_line_path, p, np);
    lv_line_set_points(s_line_head, h, nh);
    lv_line_set_points(s_glow[0], p, np);
    lv_line_set_points(s_glow[1], h, nh);
    if (x) {
        lv_line_set_points(s_line_extra, x, nx);
        lv_line_set_points(s_glow[2], x, nx);
        lv_obj_clear_flag(s_line_extra, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_glow[2], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_line_extra, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_glow[2], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    }
}

/* tone: 0 = normal, 1 = varış, 2 = soluk (eski veri / bilinmeyen manevra) */
static void apply_tone(int tone)
{
    const ui_theme_t *t = theme_get();
    lv_color_t c = tone == 1 ? t->ok : tone == 2 ? t->text_dim : t->primary;
    lv_obj_set_style_line_color(s_line_path, c, 0);
    lv_obj_set_style_line_color(s_line_head, c, 0);
    lv_obj_set_style_line_color(s_line_extra, c, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_line_color(s_glow[i], c, 0);
    }
    lv_obj_set_style_arc_color(s_ring, c, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_man_lbl, c, 0);
}

static void pill_accent(lv_obj_t *pill, lv_color_t accent)
{
    lv_obj_set_style_border_color(pill, accent, 0);
    lv_obj_set_style_bg_grad_color(pill, lv_color_mix(accent, theme_get()->surface, LV_OPA_30), 0);
}

/* Cam hap: üstte değer, altta vurgu renginde başlık; kenarlık ve alttan
 * renkli gradyan aynı vurguda. */
static lv_obj_t *make_info_cell(lv_obj_t *parent, lv_coord_t x, const char *caption,
                                lv_color_t accent, lv_obj_t **pill_out)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, NV_INFO_W, NV_PILL_H);
    lv_obj_align(pill, LV_ALIGN_CENTER, x, NV_INFO_Y + 11);
    lv_obj_set_style_radius(pill, NV_PILL_H / 2, 0);
    lv_obj_set_style_bg_color(pill, t->surface, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_70, 0);
    lv_obj_set_style_bg_grad_dir(pill, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_60, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    pill_accent(pill, accent);
    if (pill_out) {
        *pill_out = pill;
    }

    lv_obj_t *val = lv_label_create(pill);
    lv_label_set_text_static(val, "--");
    lv_obj_set_width(val, NV_INFO_W);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(val, t->font_md, 0);
    lv_obj_set_style_text_color(val, t->text, 0);
    lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *cap = lv_label_create(pill);
    lv_label_set_text_static(cap, caption);
    lv_obj_set_style_text_font(cap, t->font_tr_sm, 0);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_obj_set_style_text_color(cap, accent, 0);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -4);
    return val;
}

static void home_click_cb(lv_event_t *e)
{
    (void)e;
    ui_show_view(UI_VIEW_HOME);
}

static lv_obj_t *make_group(lv_obj_t *parent)
{
    lv_obj_t *g = lv_obj_create(parent);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(g, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return g;
}

static void page_click_cb(lv_event_t *e)
{
    (void)e;
    const ui_theme_t *t = theme_get();
    s_page = (s_page + 1) % NV_PAGES;
    for (int i = 0; i < NV_PAGES; i++) {
        lv_obj_set_style_bg_color(s_page_dot[i], i == s_page ? t->primary : t->text_dim, 0);
        if (s_page_root[i]) {
            if (i == s_page) {
                lv_obj_clear_flag(s_page_root[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_page_root[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (s_page != 0) {
        lv_obj_add_flag(s_guide, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }
    nav_bg_set_visible(s_page == 0);   /* harita / özet sade kalsın */
    s_prev_view = -1;      /* rehberliğe dönünce grup görünürlüğü yeniden kurulsun */
    s_page_dirty = true;   /* bir sonraki update beklemeden çizsin */
}

static void alert_create(lv_obj_t *root)
{
    const ui_theme_t *t = theme_get();

    s_alert = lv_obj_create(root);
    lv_obj_remove_style_all(s_alert);
    lv_obj_set_size(s_alert, NV_ALERT_W, NV_ALERT_H);
    lv_obj_align(s_alert, LV_ALIGN_CENTER, 0, NV_ALERT_Y);
    lv_obj_set_style_radius(s_alert, NV_ALERT_H / 2, 0);
    lv_obj_set_style_bg_opa(s_alert, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_alert, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN);

    s_alert_lbl = lv_label_create(s_alert);
    lv_label_set_text_static(s_alert_lbl, "");
    lv_label_set_long_mode(s_alert_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_alert_lbl, NV_ALERT_W - 24);
    lv_obj_set_style_text_align(s_alert_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_alert_lbl, t->font_tr_md, 0);
    lv_obj_center(s_alert_lbl);
}

/* Uyarı şeridi; görünürse durum satırının yerini alır. */
static bool alert_update(bool connected, uint32_t now)
{
    const ui_theme_t *t = theme_get();
    const nav_alert_t *a = &s_snap.alert;
    bool show = connected && a->kind != NAV_ALERT_NONE && now - a->ts_ms < NV_ALERT_MS;

    char txt[96] = "";
    if (show) {
        char dist[16] = "";
        if (a->dist_m) {
            snprintf(dist, sizeof(dist), "  %u m", (unsigned)a->dist_m);
        }
        switch (a->kind) {
        case NAV_ALERT_CAMERA:
            if (a->limit_kmh) {
                snprintf(txt, sizeof(txt), LV_SYMBOL_WARNING " RADAR%s  %u km/h", dist, a->limit_kmh);
            } else {
                snprintf(txt, sizeof(txt), LV_SYMBOL_WARNING " RADAR%s", dist);
            }
            break;
        case NAV_ALERT_TRAFFIC:
            snprintf(txt, sizeof(txt), "TRAFİK  %s", a->text[0] ? a->text : "yoğun");
            break;
        case NAV_ALERT_HAZARD:
            snprintf(txt, sizeof(txt), LV_SYMBOL_WARNING " %s%s", a->text[0] ? a->text : "DİKKAT", dist);
            break;
        default:
            snprintf(txt, sizeof(txt), "%s", a->text);
            break;
        }
    }

    if (strcmp(txt, s_prev_alert) != 0) {
        snprintf(s_prev_alert, sizeof(s_prev_alert), "%s", txt);
        if (!show) {
            lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_color_t bg = a->kind == NAV_ALERT_CAMERA  ? t->crit
                          : a->kind == NAV_ALERT_TRAFFIC ? t->warn
                          : a->kind == NAV_ALERT_HAZARD  ? t->secondary
                                                         : t->surface_hi;
            lv_obj_set_style_bg_color(s_alert, bg, 0);
            lv_obj_set_style_text_color(s_alert_lbl,
                                        a->kind == NAV_ALERT_TRAFFIC ? t->bg : t->text, 0);
            lv_label_set_text(s_alert_lbl, txt);
            lv_obj_clear_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
        }
    }
    return show;
}

void screen_nav_next_page(void)
{
    page_click_cb(NULL);
}

lv_obj_t *screen_nav_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    mirror(P_LEFT, P_RIGHT, N(P_RIGHT));
    mirror(H_LEFT, H_RIGHT, N(H_RIGHT));
    mirror(P_SLEFT, P_SRIGHT, N(P_SRIGHT));
    mirror(H_SLEFT, H_SRIGHT, N(H_SRIGHT));

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    nav_bg_create(root);   /* ilk: en arkada */

    s_status = lv_label_create(root);
    lv_label_set_text_static(s_status, "");
    lv_obj_set_style_text_font(s_status, t->font_tr_sm, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, NV_STATUS_Y);

    /* Rehberlik grubu */
    s_guide = make_group(root);

    /* yaklaşma halkası: 300° (altta MENÜ için boşluk), manevraya yaklaştıkça dolar */
    s_prox = lv_arc_create(s_guide);
    lv_obj_remove_style_all(s_prox);
    lv_obj_set_size(s_prox, NV_PROX_D, NV_PROX_D);
    lv_obj_center(s_prox);
    lv_arc_set_rotation(s_prox, UI_RING_ROT);
    lv_arc_set_bg_angles(s_prox, 0, UI_RING_SWEEP);
    lv_arc_set_range(s_prox, 0, 1000);
    lv_arc_set_value(s_prox, 0);
    lv_obj_set_style_arc_width(s_prox, NV_PROX_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_prox, t->surface_hi, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_prox, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_prox, NV_PROX_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_prox, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_prox, t->primary, LV_PART_INDICATOR);
    lv_obj_clear_flag(s_prox, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = lv_obj_create(s_guide);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, NV_ARROW_SZ, NV_ARROW_SZ);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, NV_ARROW_Y);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   /* yuvarlak uçlar kutudan taşar */

    s_ring = lv_arc_create(box);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, NV_RING_R * 2 + NV_RING_W, NV_RING_R * 2 + NV_RING_W);
    lv_obj_set_pos(s_ring, 80 - NV_RING_R - NV_RING_W / 2, 88 - NV_RING_R - NV_RING_W / 2);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_obj_set_style_arc_width(s_ring, NV_RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_ring, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 3; i++) {
        s_glow[i] = make_line(box);
        lv_obj_set_style_line_width(s_glow[i], NV_LINE_W + 16, 0);
        lv_obj_set_style_line_opa(s_glow[i], LV_OPA_20, 0);
    }
    s_line_path = make_line(box);
    s_line_head = make_line(box);
    s_line_extra = make_line(box);

    lv_obj_t *dist = lv_obj_create(s_guide);
    lv_obj_remove_style_all(dist);
    lv_obj_set_size(dist, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dist, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dist, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(dist, 6, 0);
    lv_obj_align(dist, LV_ALIGN_CENTER, 0, NV_DIST_Y);
    lv_obj_clear_flag(dist, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_dist_val = lv_label_create(dist);
    lv_label_set_text_static(s_dist_val, "--");
    lv_obj_set_style_text_font(s_dist_val, t->font_xxl, 0);
    lv_obj_set_style_text_color(s_dist_val, t->text, 0);

    s_dist_unit = lv_label_create(dist);
    lv_label_set_text_static(s_dist_unit, "");
    lv_obj_set_style_text_font(s_dist_unit, t->font_lg, 0);
    lv_obj_set_style_text_color(s_dist_unit, t->text_dim, 0);
    lv_obj_set_style_pad_bottom(s_dist_unit, 8, 0);

    s_man_lbl = lv_label_create(s_guide);
    lv_label_set_text_static(s_man_lbl, "");
    lv_obj_set_style_text_font(s_man_lbl, t->font_tr_md, 0);
    lv_obj_set_style_text_letter_space(s_man_lbl, 2, 0);
    lv_obj_align(s_man_lbl, LV_ALIGN_CENTER, 0, NV_MAN_Y);

    s_road_lbl = lv_label_create(s_guide);
    lv_label_set_text_static(s_road_lbl, "");
    lv_label_set_long_mode(s_road_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_road_lbl, NV_ROAD_W);
    lv_obj_set_style_text_align(s_road_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_road_lbl, t->font_tr_md, 0);
    lv_obj_set_style_text_color(s_road_lbl, t->text, 0);
    lv_obj_align(s_road_lbl, LV_ALIGN_CENTER, 0, NV_ROAD_Y);

    s_eta_val = make_info_cell(s_guide, -NV_INFO_DX, "VARIŞ", t->ok, NULL);
    s_rem_val = make_info_cell(s_guide, 0, "KALAN", t->secondary, NULL);
    s_spd_val = make_info_cell(s_guide, NV_INFO_DX, "HIZ", t->primary, &s_spd_pill);

    /* Boşta grubu (bağlantı yok / rota yok / geçiş): büyük saat + tarih,
     * durum mesajı, telefondan gelen anlık hız ve yol */
    s_msg = make_group(root);

    s_idle_time = lv_label_create(s_msg);
    lv_label_set_text_static(s_idle_time, "--:--");
    lv_obj_set_style_text_font(s_idle_time, t->font_value, 0);
    lv_obj_set_style_text_color(s_idle_time, t->text, 0);
    lv_obj_align(s_idle_time, LV_ALIGN_CENTER, 0, NV_IDLE_TIME_Y);

    s_idle_date = lv_label_create(s_msg);
    lv_label_set_text_static(s_idle_date, "");
    lv_obj_set_style_text_font(s_idle_date, t->font_tr_sm, 0);
    lv_obj_set_style_text_letter_space(s_idle_date, 3, 0);
    lv_obj_set_style_text_color(s_idle_date, t->primary, 0);
    lv_obj_align(s_idle_date, LV_ALIGN_CENTER, 0, NV_IDLE_DATE_Y);

    s_msg_title = lv_label_create(s_msg);
    lv_label_set_text_static(s_msg_title, "");
    lv_obj_set_style_text_font(s_msg_title, t->font_tr_md, 0);
    lv_obj_set_style_text_color(s_msg_title, t->secondary, 0);
    lv_obj_align(s_msg_title, LV_ALIGN_CENTER, 0, NV_IDLE_TITLE_Y);

    s_msg_sub = lv_label_create(s_msg);
    lv_label_set_text_static(s_msg_sub, "");
    lv_obj_set_width(s_msg_sub, 300);
    lv_obj_set_style_text_align(s_msg_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_msg_sub, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_msg_sub, t->text_dim, 0);
    lv_obj_align(s_msg_sub, LV_ALIGN_CENTER, 0, NV_IDLE_SUB_Y);

    s_idle_live = lv_label_create(s_msg);
    lv_label_set_text_static(s_idle_live, "");
    lv_label_set_long_mode(s_idle_live, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_idle_live, NV_ROAD_W - 40);
    lv_obj_set_style_text_align(s_idle_live, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_idle_live, t->font_tr_md, 0);
    lv_obj_set_style_text_color(s_idle_live, t->text, 0);
    lv_obj_align(s_idle_live, LV_ALIGN_CENTER, 0, NV_IDLE_LIVE_Y);

    s_page_root[1] = nav_map_page_create(root);
    s_page_root[2] = nav_trip_page_create(root);
    lv_obj_add_flag(s_page_root[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_root[2], LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *dots = lv_obj_create(root);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_COLUMN);   /* sağ kenarda dikey: alt alan menüye */
    lv_obj_set_style_pad_row(dots, 8, 0);
    lv_obj_align(dots, LV_ALIGN_CENTER, NV_DOTS_X, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < NV_PAGES; i++) {
        s_page_dot[i] = lv_obj_create(dots);
        lv_obj_remove_style_all(s_page_dot[i]);
        lv_obj_set_size(s_page_dot[i], 7, 7);
        lv_obj_set_style_radius(s_page_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_page_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_page_dot[i], i == 0 ? t->primary : t->text_dim, 0);
        lv_obj_clear_flag(s_page_dot[i], LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    /* Kısa dokunuş: sonraki sayfa. Uzun basış serbest: 4 sn tutuş saati açar
     * (ui.c); SHORT_CLICKED uzun basıştan sonra gelmez, sayfa değişmez. */
    lv_obj_add_event_cb(root, page_click_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* Ana menü düğmesi — alt kenarda geniş hap */
    lv_obj_t *home = lv_obj_create(root);
    lv_obj_remove_style_all(home);
    lv_obj_set_size(home, NV_HOME_W, NV_HOME_H);
    lv_obj_set_style_radius(home, NV_HOME_H / 2, 0);
    lv_obj_set_style_bg_color(home, t->surface_hi, 0);
    lv_obj_set_style_bg_opa(home, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(home, t->primary, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(home, t->border, 0);
    lv_obj_set_style_border_width(home, 1, 0);
    lv_obj_align(home, LV_ALIGN_CENTER, 0, NV_HOME_Y);
    lv_obj_clear_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(home, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(home, 14);
    lv_obj_add_event_cb(home, home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text_static(hl, LV_SYMBOL_HOME "  MENÜ");
    lv_obj_set_style_text_font(hl, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(hl, t->text, 0);
    lv_obj_center(hl);

    alert_create(root);

    apply_maneuver(NAV_MAN_STRAIGHT);
    apply_tone(0);
    lv_obj_add_flag(s_guide, LV_OBJ_FLAG_HIDDEN);
    return root;
}

static void apply_snapshot(const nav_state_t *ns)
{
    char val[16], buf[24];
    const char *unit;
    bool man_changed = (int)ns->maneuver != s_prev_man;

    if (man_changed) {
        s_prev_man = (int)ns->maneuver;
        apply_maneuver(ns->maneuver);
        set_text_if(s_man_lbl, maneuver_text(ns->maneuver));
    }

    format_dist(ns->maneuver == NAV_MAN_ARRIVAL ? ns->remain_m : ns->maneuver_dist_m,
                val, sizeof(val), &unit);
    set_text_if(s_dist_val, val);
    set_text_if(s_dist_unit, unit);

    set_text_if(s_road_lbl, ns->next_road[0] ? ns->next_road : ns->cur_road);

    if (ns->eta_min >= 0) {
        snprintf(buf, sizeof(buf), "%02d:%02d", ns->eta_min / 60, ns->eta_min % 60);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    set_text_if(s_eta_val, buf);

    format_dist(ns->remain_m, val, sizeof(val), &unit);
    snprintf(buf, sizeof(buf), "%s %s", val, unit);
    set_text_if(s_rem_val, buf);

    if (ns->speed_kmh >= 0.0f) {
        snprintf(buf, sizeof(buf), "%d km/h", (int)(ns->speed_kmh + 0.5f));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    set_text_if(s_spd_val, buf);

    /* Yaklaşma halkası: manevra değişince ya da mesafe büyüyünce (yeniden
     * rota) başlangıç sıfırlanır; renk yakınlıkla camgöbeği → turuncu → yeşil. */
    const ui_theme_t *t = theme_get();
    uint32_t d = ns->maneuver_dist_m;
    if (d > s_prox_start || man_changed || s_prox_start == 0) {
        s_prox_start = d > 0 ? d : 1;
    }
    int prog = d >= s_prox_start ? 0 : (int)(1000 - (uint64_t)d * 1000 / s_prox_start);
    lv_arc_set_value(s_prox, prog);
    lv_obj_set_style_arc_color(s_prox, d < NV_PROX_NEAR_M ? t->ok : d < NV_PROX_MID_M ? t->secondary
                                                                                         : t->primary,
                               LV_PART_INDICATOR);

    /* Radar uyarısı limitliyse ve hız üstündeyse hız hapı kırmızı */
    const nav_alert_t *a = &ns->alert;
    int over = a->kind == NAV_ALERT_CAMERA && a->limit_kmh && ns->speed_kmh > a->limit_kmh &&
               nav_state_now_ms() - a->ts_ms < NV_ALERT_MS;
    if (over != s_prev_over) {
        s_prev_over = over;
        pill_accent(s_spd_pill, over ? t->crit : t->primary);
        lv_obj_set_style_text_color(s_spd_val, over ? t->crit : t->text, 0);
    }
}

/* Boşta grubu: saat / tarih ve telefondan gelen anlık hız · yol */
static void idle_update(void)
{
    char buf[NAV_TEXT_LEN + 24];
    struct tm tm;
    if (clock_now(&tm)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        set_text_if(s_idle_time, buf);
        snprintf(buf, sizeof(buf), "%s  \xE2\x80\xA2  %d %s", clock_day_name(tm.tm_wday), tm.tm_mday,
                 clock_month_name(tm.tm_mon));
        set_text_if(s_idle_date, buf);
    } else {
        set_text_if(s_idle_time, "--:--");
        set_text_if(s_idle_date, "");
    }

    buf[0] = '\0';
    if (s_snap.connected && s_snap.speed_kmh >= 0.0f) {
        snprintf(buf, sizeof(buf), "%d km/h", (int)(s_snap.speed_kmh + 0.5f));
    }
    if (s_snap.connected && s_snap.cur_road[0]) {
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof(buf) - n, "%s%s", n ? "  \xE2\x80\xA2  " : "", s_snap.cur_road);
    }
    set_text_if(s_idle_live, buf);
}

void screen_nav_update(void)
{
    const ui_theme_t *t = theme_get();
    uint32_t rev = nav_state_rev();
    uint32_t now = nav_state_now_ms();

    if (s_page == 0) {
        nav_bg_update(&s_snap, s_bg_mode);   /* kendi kare hızında (~15 fps) */
    }

    /* Veri yalnızca değişince; durum (eskime / geçiş) NV_POLL_MS'de bir. */
    if (rev == s_prev_rev && now - s_last_poll < NV_POLL_MS && !s_page_dirty) {
        return;
    }
    bool page_force = s_page_dirty;
    s_page_dirty = false;
    s_last_poll = now;
    bool changed = rev != s_prev_rev;
    if (changed) {
        s_prev_rev = rev;
        nav_state_snapshot(&s_snap);
    }

    int view;
    if (app_mode_is_switching()) {
        view = VIEW_SWITCHING;
    } else if (!s_snap.connected) {
        view = VIEW_NO_PHONE;
    } else if (!s_snap.active) {
        view = VIEW_NO_ROUTE;
    } else {
        view = VIEW_GUIDE;
    }
    bool stale = view == VIEW_GUIDE && now - s_snap.last_rx_ms > NV_STALE_MS;
    s_bg_mode = view != VIEW_GUIDE ? NAV_BG_IDLE : stale ? NAV_BG_STALE : NAV_BG_GUIDE;
    if (view != VIEW_GUIDE && s_page == 0) {
        idle_update();
    }

    bool view_changed = view != s_prev_view;
    if (view_changed && s_page == 0) {
        s_prev_view = view;
        if (view == VIEW_GUIDE) {
            lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_guide, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_guide, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
            if (view == VIEW_SWITCHING) {
                lv_label_set_text_static(s_msg_title, "Geçiliyor...");
                lv_label_set_text_static(s_msg_sub, "OBD bağlantısı kapatılıyor");
            } else if (view == VIEW_NO_PHONE) {
                lv_label_set_text_static(s_msg_title, "Telefon bağlı değil");
                lv_label_set_text_static(s_msg_sub, "AURA Köprü uygulaması bekleniyor");
            } else {
                lv_label_set_text_static(s_msg_title, "Rota yok");
                lv_label_set_text_static(s_msg_sub, "Telefonda navigasyonu başlatın");
            }
        }
    }

    /* Durum satırı */
    const char *st;
    lv_color_t sc;
    if (view == VIEW_SWITCHING) {
        st = "";
        sc = t->text_dim;
    } else if (!s_snap.connected) {
        st = LV_SYMBOL_BLUETOOTH " Bağlantı yok";
        sc = t->text_dim;
    } else if (stale) {
        st = LV_SYMBOL_WARNING " Veri eski";
        sc = t->warn;
    } else {
#ifdef CONFIG_NAV_MOCK
        st = LV_SYMBOL_BLUETOOTH " Telefon bağlı - DEMO";
#else
        st = LV_SYMBOL_BLUETOOTH " Telefon bağlı";
#endif
        sc = t->ok;
    }
    if (alert_update(view != VIEW_SWITCHING && s_snap.connected, now)) {
        st = "";
    }
    /* Rehberlikte saat durum satırında (boşta zaten büyük saat var) */
    char st_buf[64];
    struct tm tm;
    if (st[0] && view == VIEW_GUIDE && clock_now(&tm)) {
        snprintf(st_buf, sizeof(st_buf), "%02d:%02d  \xE2\x80\xA2  %s", tm.tm_hour, tm.tm_min, st);
        st = st_buf;
    }
    set_text_if(s_status, st);
    if (lv_color_to32(sc) != s_prev_sc) {
        s_prev_sc = lv_color_to32(sc);
        lv_obj_set_style_text_color(s_status, sc, 0);
    }

    if (s_page == 1) {
        nav_map_page_update(&s_snap, page_force);
    } else if (s_page == 2) {
        nav_trip_page_update(page_force);
    }

    if (view == VIEW_GUIDE && s_page == 0) {
        if (changed || view_changed) {
            apply_snapshot(&s_snap);
        }
        int tone = stale || s_snap.maneuver == NAV_MAN_UNKNOWN ? 2
                 : s_snap.maneuver == NAV_MAN_ARRIVAL ? 1 : 0;
        if (tone != s_prev_tone) {
            s_prev_tone = tone;
            apply_tone(tone);
        }
    }
}
