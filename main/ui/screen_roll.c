#include "ui.h"
#include "theme.h"
#include "roll_feed.h"
#include "roll_cfg.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ROLL — performans ölçümü. F0: canlı hız + kaynak/sinyal durumu + kayıt.
 *
 *   dış halka   300° hız yayı; renk kaynağı söyler (GPS camgöbeği, OBD turuncu)
 *   üst         ROLL + araç adı + kaynak rozeti (birim ve ad ROLL ayarlarından)
 *   orta        büyük hız, altında kaynağın doğruluk notu
 *   alt         GPS / OBD / IMU hapları, kayıt durumu, MENÜ */

#define RL_RING_D      440
#define RL_RING_W      10
#define RL_TITLE_Y     (-178)
#define RL_NAME_Y      (-154)
#define RL_BADGE_Y     (-124)
#define RL_BADGE_W     148
#define RL_BADGE_H     28
#define RL_SPEED_Y     (-30)
#define RL_UNIT_Y      34
#define RL_NOTE_Y      62
#define RL_PILL_Y      112
#define RL_PILL_W      108
#define RL_PILL_H      52
#define RL_PILL_DX     116
#define RL_REC_Y       160
#define RL_HOME_Y      194
#define RL_HOME_W      112
#define RL_HOME_H      32
#define RL_SCALE_MIN   200       /* gösterim biriminde; aşılınca 260 / 320'ye büyür */
#define KMH_TO_MPH     0.621371f

typedef struct {
    lv_obj_t *pill;
    lv_obj_t *val;
    lv_obj_t *cap;
} rl_pill_t;

static lv_obj_t *s_ring;
static lv_obj_t *s_badge, *s_badge_lbl;
static lv_obj_t *s_speed, *s_unit, *s_note, *s_name;
static rl_pill_t s_gps, s_obd, s_imu;
static lv_obj_t *s_rec_dot, *s_rec_lbl;
static int s_scale = RL_SCALE_MIN;
static int s_prev_src = -1;
static int s_prev_kmh = -1;
static uint32_t s_slow_last;
static uint32_t s_cfg_rev = UINT32_MAX;
static bool s_mph;

static void home_click_cb(lv_event_t *e)
{
    (void)e;
    ui_show_view(UI_VIEW_HOME);
}

static void set_text_if(lv_obj_t *lbl, const char *txt)
{
    if (strcmp(lv_label_get_text(lbl), txt) != 0) {
        lv_label_set_text(lbl, txt);
    }
}

static void pill_accent(rl_pill_t *p, lv_color_t accent, bool on)
{
    const ui_theme_t *t = theme_get();
    lv_color_t c = on ? accent : t->border;
    lv_obj_set_style_border_color(p->pill, c, 0);
    lv_obj_set_style_bg_grad_color(p->pill, lv_color_mix(c, t->surface, on ? LV_OPA_30 : LV_OPA_10), 0);
    lv_obj_set_style_text_color(p->cap, on ? accent : t->text_dim, 0);
    lv_obj_set_style_text_color(p->val, on ? t->text : t->text_dim, 0);
}

static void make_pill(lv_obj_t *root, rl_pill_t *p, lv_coord_t x, const char *caption)
{
    const ui_theme_t *t = theme_get();
    p->pill = lv_obj_create(root);
    lv_obj_remove_style_all(p->pill);
    lv_obj_set_size(p->pill, RL_PILL_W, RL_PILL_H);
    lv_obj_align(p->pill, LV_ALIGN_CENTER, x, RL_PILL_Y);
    lv_obj_set_style_radius(p->pill, RL_PILL_H / 2, 0);
    lv_obj_set_style_bg_color(p->pill, t->surface, 0);
    lv_obj_set_style_bg_opa(p->pill, LV_OPA_70, 0);
    lv_obj_set_style_bg_grad_dir(p->pill, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(p->pill, 1, 0);
    lv_obj_set_style_border_opa(p->pill, LV_OPA_60, 0);
    lv_obj_clear_flag(p->pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    p->val = lv_label_create(p->pill);
    lv_label_set_text_static(p->val, "--");
    lv_obj_set_width(p->val, RL_PILL_W);
    lv_obj_set_style_text_align(p->val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(p->val, t->font_md, 0);
    lv_obj_align(p->val, LV_ALIGN_TOP_MID, 0, 5);

    p->cap = lv_label_create(p->pill);
    lv_label_set_text(p->cap, caption);
    lv_obj_set_width(p->cap, RL_PILL_W);
    lv_obj_set_style_text_align(p->cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(p->cap, t->font_tr_sm, 0);
    lv_obj_align(p->cap, LV_ALIGN_BOTTOM_MID, 0, -5);
}

lv_obj_t *screen_roll_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, UI_VIEWPORT_SZ, UI_VIEWPORT_SZ);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root, t->bg, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* iç ışıma: merkezde hafif sıcak disk */
    lv_obj_t *glow = lv_obj_create(root);
    theme_apply_lens(glow, 300);
    lv_obj_set_style_bg_opa(glow, LV_OPA_40, 0);
    lv_obj_set_style_border_opa(glow, LV_OPA_20, 0);
    lv_obj_clear_flag(glow, LV_OBJ_FLAG_CLICKABLE);

    s_ring = theme_create_arc(root, RL_RING_D, RL_RING_W);
    lv_arc_set_rotation(s_ring, UI_RING_ROT);
    lv_arc_set_bg_angles(s_ring, 0, UI_RING_SWEEP);
    lv_arc_set_range(s_ring, 0, s_scale);
    lv_arc_set_value(s_ring, 0);
    lv_obj_set_style_arc_rounded(s_ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_ring, LV_OPA_50, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text_static(title, "ROLL");
    lv_obj_set_style_text_font(title, t->font_md, 0);
    lv_obj_set_style_text_color(title, t->text, 0);
    lv_obj_set_style_text_letter_space(title, 8, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 4, RL_TITLE_Y);

    s_name = lv_label_create(root);
    lv_label_set_text_static(s_name, "");
    lv_obj_set_style_text_font(s_name, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_name, t->text_dim, 0);
    lv_obj_align(s_name, LV_ALIGN_CENTER, 0, RL_NAME_Y);

    s_badge = lv_obj_create(root);
    lv_obj_remove_style_all(s_badge);
    lv_obj_set_size(s_badge, RL_BADGE_W, RL_BADGE_H);
    lv_obj_set_style_radius(s_badge, RL_BADGE_H / 2, 0);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_20, 0);
    lv_obj_set_style_border_width(s_badge, 1, 0);
    lv_obj_align(s_badge, LV_ALIGN_CENTER, 0, RL_BADGE_Y);
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_badge_lbl = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_badge_lbl, t->font_tr_sm, 0);
    lv_obj_set_style_text_letter_space(s_badge_lbl, 1, 0);
    lv_obj_center(s_badge_lbl);

    s_speed = lv_label_create(root);
    lv_label_set_text_static(s_speed, "0");
    lv_obj_set_style_text_font(s_speed, t->font_value, 0);
    lv_obj_set_style_text_color(s_speed, t->text, 0);
    lv_obj_align(s_speed, LV_ALIGN_CENTER, 0, RL_SPEED_Y);

    s_unit = lv_label_create(root);
    lv_label_set_text_static(s_unit, "km/h");
    lv_obj_set_style_text_font(s_unit, t->font_md, 0);
    lv_obj_set_style_text_color(s_unit, t->text_dim, 0);
    lv_obj_set_style_text_letter_space(s_unit, 2, 0);
    lv_obj_align(s_unit, LV_ALIGN_CENTER, 0, RL_UNIT_Y);

    s_note = lv_label_create(root);
    lv_label_set_text_static(s_note, "");
    lv_obj_set_style_text_font(s_note, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_note, t->text_dim, 0);
    lv_obj_align(s_note, LV_ALIGN_CENTER, 0, RL_NOTE_Y);

    make_pill(root, &s_gps, -RL_PILL_DX, "GPS");
    make_pill(root, &s_obd, 0, "OBD");
    make_pill(root, &s_imu, RL_PILL_DX, "IMU");

    s_rec_dot = lv_obj_create(root);
    lv_obj_remove_style_all(s_rec_dot);
    lv_obj_set_size(s_rec_dot, 8, 8);
    lv_obj_set_style_radius(s_rec_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_rec_dot, LV_OPA_COVER, 0);
    s_rec_lbl = lv_label_create(root);
    lv_label_set_text_static(s_rec_lbl, "");
    lv_obj_set_style_text_font(s_rec_lbl, t->font_tr_sm, 0);
    lv_obj_align(s_rec_lbl, LV_ALIGN_CENTER, 6, RL_REC_Y);
    lv_obj_align_to(s_rec_dot, s_rec_lbl, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    lv_obj_t *home = lv_obj_create(root);
    lv_obj_remove_style_all(home);
    lv_obj_set_size(home, RL_HOME_W, RL_HOME_H);
    lv_obj_set_style_radius(home, RL_HOME_H / 2, 0);
    lv_obj_set_style_bg_color(home, t->surface_hi, 0);
    lv_obj_set_style_bg_opa(home, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(home, t->primary, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(home, t->border, 0);
    lv_obj_set_style_border_width(home, 1, 0);
    lv_obj_align(home, LV_ALIGN_CENTER, 0, RL_HOME_Y);
    lv_obj_clear_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(home, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(home, 14);
    lv_obj_add_event_cb(home, home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hl = lv_label_create(home);
    lv_label_set_text_static(hl, LV_SYMBOL_HOME "  MENÜ");
    lv_obj_set_style_text_font(hl, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(hl, t->text, 0);
    lv_obj_center(hl);

    screen_roll_update();
    return root;
}

/* Ayar değişti: birim + araç adı */
static void apply_cfg(void)
{
    roll_cfg_t c;
    roll_cfg_get(&c);
    s_mph = c.mph;
    set_text_if(s_name, c.name);
    lv_label_set_text_static(s_unit, s_mph ? "mph" : "km/h");
    s_scale = RL_SCALE_MIN;
    lv_arc_set_range(s_ring, 0, s_scale);
    s_prev_kmh = -1;
}

static const char *unit_str(void)
{
    return s_mph ? "mph" : "km/h";
}

static float disp(float kmh)
{
    return s_mph ? kmh * KMH_TO_MPH : kmh;
}

static lv_color_t src_color(roll_src_t src)
{
    const ui_theme_t *t = theme_get();
    return src == ROLL_SRC_GPS ? t->primary : src == ROLL_SRC_OBD ? t->secondary : t->text_dim;
}

static void apply_source(const roll_snapshot_t *s)
{
    lv_color_t c = src_color(s->src);
    lv_obj_set_style_arc_color(s_ring, c, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_badge, c, 0);
    lv_obj_set_style_border_color(s_badge, c, 0);
    lv_obj_set_style_text_color(s_badge_lbl, c, 0);
    lv_obj_set_style_text_color(s_speed, s->src == ROLL_SRC_NONE ? theme_get()->text_dim
                                                                 : theme_get()->text, 0);
    lv_label_set_text_static(s_badge_lbl, s->src == ROLL_SRC_GPS   ? LV_SYMBOL_GPS "  GPS"
                                        : s->src == ROLL_SRC_OBD   ? LV_SYMBOL_CHARGE "  OBD"
                                                                   : "SİNYAL YOK");
}

/* Saniyede birkaç kez yeterli olan metinler. */
static void apply_slow(const roll_snapshot_t *s)
{
    const ui_theme_t *t = theme_get();
    char buf[32];

    if (s->src == ROLL_SRC_GPS) {
        if (s->gps_acc_kmh >= 0) {
            snprintf(buf, sizeof(buf), "doğruluk ~%.1f %s", disp(s->gps_acc_kmh), unit_str());
        } else {
            snprintf(buf, sizeof(buf), "telefon GPS");
        }
    } else if (s->src == ROLL_SRC_OBD) {
        snprintf(buf, sizeof(buf), "ECU hızı");
    } else {
        snprintf(buf, sizeof(buf), s->phone ? "GPS fix bekleniyor" : "Telefon veya OBD bekleniyor");
    }
    set_text_if(s_note, buf);

    /* GPS hapı */
    if (s->gps_hz > 0.05f) {
        snprintf(buf, sizeof(buf), "%.1f Hz", s->gps_hz);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    set_text_if(s_gps.val, buf);
    if (s->gps_sats >= 0 && s->gps_hz > 0.05f) {
        snprintf(buf, sizeof(buf), "GPS %d uydu", s->gps_sats);
    } else {
        snprintf(buf, sizeof(buf), s->phone ? "GPS" : "GPS yok");
    }
    set_text_if(s_gps.cap, buf);
    pill_accent(&s_gps, t->primary, s->gps_ok);

    /* OBD hapı */
    if (s->obd_ok) {
        snprintf(buf, sizeof(buf), "%.1f Hz", s->obd_hz);
        set_text_if(s_obd.val, buf);
        snprintf(buf, sizeof(buf), "OBD %d", (int)lroundf(disp(s->obd_kmh)));
        set_text_if(s_obd.cap, buf);
    } else {
        set_text_if(s_obd.val, "--");
        set_text_if(s_obd.cap, "OBD");
    }
    pill_accent(&s_obd, t->secondary, s->obd_ok);

    /* IMU hapı */
    snprintf(buf, sizeof(buf), "%+.2f g", s->a_long / 9.80665f);
    set_text_if(s_imu.val, buf);
    snprintf(buf, sizeof(buf), "Eğim %.1f°", s->pitch_deg);
    set_text_if(s_imu.cap, buf);
    pill_accent(&s_imu, t->ok, true);

    /* kayıt */
    lv_color_t rc;
    const char *rt;
    if (s->tel) {
        rc = t->accent;
        rt = "Telefona kaydediliyor";
    } else if (s->phone) {
        rc = t->warn;
        rt = "Telefon bağlı";
    } else {
        rc = t->text_dim;
        rt = "Telefon bekleniyor";
    }
    if (strcmp(lv_label_get_text(s_rec_lbl), rt) != 0) {
        lv_label_set_text_static(s_rec_lbl, rt);
        lv_obj_align_to(s_rec_dot, s_rec_lbl, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    }
    lv_obj_set_style_text_color(s_rec_lbl, rc, 0);
    lv_obj_set_style_bg_color(s_rec_dot, rc, 0);
}

void screen_roll_update(void)
{
    if (s_cfg_rev != roll_cfg_rev()) {
        s_cfg_rev = roll_cfg_rev();
        apply_cfg();
    }
    roll_snapshot_t s;
    roll_feed_snapshot(&s);

    if ((int)s.src != s_prev_src) {
        s_prev_src = (int)s.src;
        apply_source(&s);
    }

    int kmh = (int)lroundf(disp(s.speed_kmh));   /* gösterim biriminde */
    if (kmh != s_prev_kmh) {
        s_prev_kmh = kmh;
        lv_label_set_text_fmt(s_speed, "%d", kmh);
        int scale = kmh > 260 ? 320 : kmh > RL_SCALE_MIN ? 260 : s_scale;
        if (scale != s_scale) {
            s_scale = scale;   /* yalnız büyür: koşu boyunca ölçek zıplamasın */
            lv_arc_set_range(s_ring, 0, s_scale);
        }
        lv_arc_set_value(s_ring, kmh > s_scale ? s_scale : kmh);
    }

    /* kayıt noktası yanıp söner */
    if (s.tel) {
        lv_obj_set_style_bg_opa(s_rec_dot, (lv_tick_get() / 500) % 2 ? LV_OPA_COVER : LV_OPA_30, 0);
    } else {
        lv_obj_set_style_bg_opa(s_rec_dot, LV_OPA_COVER, 0);
    }

    if (lv_tick_elaps(s_slow_last) >= 250) {
        s_slow_last = lv_tick_get();
        apply_slow(&s);
    }
}
