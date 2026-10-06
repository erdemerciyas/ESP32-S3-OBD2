#include "screen_gyro.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Eğim ölçer + G-metre (yuvarlak panel).
 *
 * EĞİM: sol yay pitch (yukarı = burun yukarı), sağ yay roll (aşağı = sağ
 * taraf aşağı), ±45°; mercekte rolle dönen, pitch'le kayan ufuk çizgisi.
 * G-METRE: yaylar boyuna / yanal ivme (±1 g); mercekte 0.5 g ve 1 g
 * halkaları, hissedilen kuvveti gösteren top ve kısa iz.
 * Merceğe dokun: mod değiştir. SIFIRLA: şu anki duruş = düz + tepe değerleri
 * sıfır. KALİBRE: 3 sn hareketsiz örnekleme (sapma + yukarı yön).
 * Füzyon, montaj dönüşümü ve hız telafisi imu_data'da. */

#define GY_ARC_D        UI_RING_D
#define GY_ARC_W        10
#define GY_RANGE        45      /* ± derece / ±1 g yaylarda */
#define GY_LENS_D       236
#define GY_HZ_PX_DEG    2.0f
#define GY_HZ_MAX       40.0f
#define GY_VAL_X        162
#define GY_SMOOTH       0.35f   /* ekran EMA (füzyon zaten filtreli) */
#define GY_BTN_Y        (-164)
#define GY_BTN_DX       56
#define GY_BTN_W        100
#define GY_INCL_Y       150
#define GY_HINT_Y       176
#define GY_G_RING       (GY_LENS_D / 2 - 10)   /* 1 g yarıçapı */
#define GY_TRAIL        24
#define GY_BALL_D       18
#define GY_CAL_D        380

enum { MODE_TILT = 0, MODE_G };

/* --- durum -------------------------------------------------------------------- */
static lv_obj_t *s_arc_pitch;
static lv_obj_t *s_arc_roll;
static lv_obj_t *s_val_pitch;
static lv_obj_t *s_val_roll;
static lv_obj_t *s_name_l;
static lv_obj_t *s_name_r;
static lv_obj_t *s_val_incl;
static lv_obj_t *s_hint;
static lv_obj_t *s_mode_lbl;
static lv_obj_t *s_horizon;
static lv_obj_t *s_wings[3];
static lv_obj_t *s_glayer;
static lv_obj_t *s_ball;
static lv_obj_t *s_trail;
static lv_obj_t *s_zero_btn;
static lv_point_t s_hz_pts[2];
static lv_point_t s_tr_pts[GY_TRAIL];
static int s_tr_n;
static char s_prev_val[5][32];
static int16_t s_prev_hz_p = INT16_MIN, s_prev_hz_r = INT16_MIN;
static int8_t s_prev_lvl[3] = { -1, -1, -1 };
static float s_fp, s_fr, s_fl, s_ft;
static bool s_have_f;
static int s_mode = MODE_TILT;
static bool s_mode_dirty = true;
static uint32_t s_flash_until;
static uint32_t s_last_trail;

/* kalibrasyon paneli */
static lv_obj_t *s_cal;
static lv_obj_t *s_cal_arc;
static lv_obj_t *s_cal_msg;
static lv_obj_t *s_cal_go;
static lv_obj_t *s_cal_go_lbl;
static int s_cal_shown_state = -1;

/* --- yardımcılar ------------------------------------------------------------- */
static int8_t angle_level(float deg)
{
    float a = fabsf(deg);
    return a < 15.0f ? 0 : a < 30.0f ? 1 : 2;
}

static int8_t g_level(float g)
{
    float a = fabsf(g);
    return a < 0.3f ? 0 : a < 0.6f ? 1 : 2;
}

static lv_color_t level_color(int8_t lvl)
{
    const ui_theme_t *t = theme_get();
    return lvl == 0 ? t->ok : lvl == 1 ? t->warn : t->crit;
}

static void set_text_cached(lv_obj_t *l, int idx, const char *txt)
{
    if (strcmp(txt, s_prev_val[idx]) != 0) {
        snprintf(s_prev_val[idx], sizeof(s_prev_val[idx]), "%s", txt);
        lv_label_set_text(l, txt);
    }
}

static lv_obj_t *side_arc(lv_obj_t *root, uint16_t rot)
{
    lv_obj_t *arc = theme_create_arc(root, GY_ARC_D, GY_ARC_W);
    lv_arc_set_rotation(arc, rot);
    lv_arc_set_bg_angles(arc, 0, 90);
    lv_arc_set_mode(arc, LV_ARC_MODE_SYMMETRICAL);
    lv_arc_set_range(arc, -GY_RANGE, GY_RANGE);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    return arc;
}

static lv_obj_t *side_value(lv_obj_t *root, lv_coord_t x, lv_obj_t **name_out)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *val = lv_label_create(root);
    lv_label_set_text_static(val, "--");
    lv_obj_set_style_text_font(val, t->font_lg, 0);
    lv_obj_set_style_text_color(val, t->text, 0);
    lv_obj_align(val, LV_ALIGN_CENTER, x, -8);

    lv_obj_t *nm = lv_label_create(root);
    lv_label_set_text_static(nm, "");
    lv_obj_set_style_text_font(nm, t->font_tr_sm, 0);
    lv_obj_set_style_text_letter_space(nm, 1, 0);
    lv_obj_set_style_text_color(nm, t->text_dim, 0);
    lv_obj_align(nm, LV_ALIGN_CENTER, x, 18);
    *name_out = nm;
    return val;
}

static void set_horizon(float pitch, float roll)
{
    const float r = GY_LENS_D / 2.0f - 6.0f;
    float p = fmaxf(-GY_HZ_MAX, fminf(GY_HZ_MAX, pitch));
    float off = p * GY_HZ_PX_DEG;
    float a = -roll * (float)M_PI / 180.0f;
    float c = cosf(a), s = sinf(a);
    float half = sqrtf(r * r - off * off);
    float cx = GY_LENS_D / 2.0f - s * off;
    float cy = GY_LENS_D / 2.0f + c * off;
    s_hz_pts[0].x = (lv_coord_t)lroundf(cx - c * half);
    s_hz_pts[0].y = (lv_coord_t)lroundf(cy - s * half);
    s_hz_pts[1].x = (lv_coord_t)lroundf(cx + c * half);
    s_hz_pts[1].y = (lv_coord_t)lroundf(cy + s * half);
    lv_line_set_points(s_horizon, s_hz_pts, 2);
}

static lv_obj_t *pill_btn(lv_obj_t *root, const char *txt, lv_coord_t x, lv_event_cb_t cb)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *b = lv_btn_create(root);
    lv_obj_set_size(b, GY_BTN_W, 30);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, t->primary, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_border_color(b, t->primary, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_align(b, LV_ALIGN_CENTER, x, GY_BTN_Y);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_static(l, txt);
    lv_obj_set_style_text_font(l, t->font_tr_sm, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_style_text_color(l, t->primary, 0);
    lv_obj_center(l);
    return b;
}

/* --- olaylar --------------------------------------------------------------- */
static void zero_cb(lv_event_t *e)
{
    (void)e;
    imu_level_zero();
    s_flash_until = lv_tick_get() + 500;
    s_have_f = false;
    s_tr_n = 0;
    lv_obj_set_style_bg_color(s_zero_btn, theme_get()->ok, 0);
    lv_obj_set_style_bg_opa(s_zero_btn, LV_OPA_50, 0);
}

static void lens_cb(lv_event_t *e)
{
    (void)e;
    s_mode = s_mode == MODE_TILT ? MODE_G : MODE_TILT;
    s_mode_dirty = true;
}

static void cal_open_cb(lv_event_t *e)
{
    (void)e;
    s_cal_shown_state = -1;
    lv_label_set_text_static(s_cal_msg, "Aracı düz zemine park edin,\nmotor çalışabilir, kıpırdamayın.");
    lv_label_set_text_static(s_cal_go_lbl, "BAŞLAT");
    lv_arc_set_value(s_cal_arc, 0);
    lv_obj_clear_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
}

static void cal_go_cb(lv_event_t *e)
{
    (void)e;
    imu_snapshot_t s;
    imu_get_snapshot(&s);
    if (s.calib_state == IMU_CAL_RUNNING) {
        return;
    }
    if (s_cal_shown_state == IMU_CAL_OK) {
        lv_obj_add_flag(s_cal, LV_OBJ_FLAG_HIDDEN);   /* TAMAM */
        return;
    }
    imu_calib_start();
    s_cal_shown_state = IMU_CAL_RUNNING;
}

static void cal_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
}

static void cal_clear_cb(lv_event_t *e)
{
    (void)e;
    imu_calib_clear();
    lv_label_set_text_static(s_cal_msg, "Kalibrasyon silindi.\nDikey montaj varsayılıyor.");
    s_cal_shown_state = -1;
}

/* --- oluşturma ------------------------------------------------------------- */
static void create_cal_panel(lv_obj_t *root)
{
    const ui_theme_t *t = theme_get();

    s_cal = lv_obj_create(root);
    theme_apply_lens(s_cal, GY_CAL_D);
    lv_obj_set_style_bg_color(s_cal, t->bg, 0);
    lv_obj_set_style_bg_opa(s_cal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_cal, t->primary, 0);
    lv_obj_set_style_border_width(s_cal, 2, 0);
    lv_obj_add_flag(s_cal, LV_OBJ_FLAG_CLICKABLE);   /* alttakilere dokunuş geçmesin */
    lv_obj_add_flag(s_cal, LV_OBJ_FLAG_HIDDEN);

    s_cal_arc = theme_create_arc(s_cal, GY_CAL_D - 24, 6);
    lv_arc_set_rotation(s_cal_arc, 270);
    lv_arc_set_bg_angles(s_cal_arc, 0, 360);
    lv_arc_set_range(s_cal_arc, 0, 100);
    lv_arc_set_value(s_cal_arc, 0);

    lv_obj_t *title = lv_label_create(s_cal);
    lv_label_set_text_static(title, "KALİBRASYON");
    lv_obj_set_style_text_font(title, t->font_tr_md, 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_set_style_text_color(title, t->primary, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -110);

    s_cal_msg = lv_label_create(s_cal);
    lv_label_set_text_static(s_cal_msg, "");
    lv_obj_set_width(s_cal_msg, 280);
    lv_obj_set_style_text_align(s_cal_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_cal_msg, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_cal_msg, t->text, 0);
    lv_obj_align(s_cal_msg, LV_ALIGN_CENTER, 0, -36);

    s_cal_go = lv_btn_create(s_cal);
    lv_obj_set_size(s_cal_go, 150, 46);
    lv_obj_set_style_radius(s_cal_go, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_cal_go, t->primary, 0);
    lv_obj_set_style_shadow_width(s_cal_go, 0, 0);
    lv_obj_align(s_cal_go, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(s_cal_go, cal_go_cb, LV_EVENT_CLICKED, NULL);
    s_cal_go_lbl = lv_label_create(s_cal_go);
    lv_obj_set_style_text_font(s_cal_go_lbl, t->font_tr_md, 0);
    lv_obj_set_style_text_color(s_cal_go_lbl, t->bg, 0);
    lv_obj_center(s_cal_go_lbl);

    lv_obj_t *close = lv_label_create(s_cal);
    lv_label_set_text_static(close, "KAPAT");
    lv_obj_set_style_text_font(close, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(close, t->text_dim, 0);
    lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(close, 14);
    lv_obj_align(close, LV_ALIGN_CENTER, -60, 104);
    lv_obj_add_event_cb(close, cal_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clr = lv_label_create(s_cal);
    lv_label_set_text_static(clr, "SİL");
    lv_obj_set_style_text_font(clr, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(clr, t->crit, 0);
    lv_obj_add_flag(clr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(clr, 14);
    lv_obj_align(clr, LV_ALIGN_CENTER, 60, 104);
    lv_obj_add_event_cb(clr, cal_clear_cb, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *ring(lv_obj_t *parent, lv_coord_t d, lv_opa_t opa)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, d, d);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, theme_get()->text_dim, 0);
    lv_obj_set_style_border_opa(r, opa, 0);
    lv_obj_align(r, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return r;
}

void screen_gyro_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *root = theme_create_root(parent);

    s_arc_pitch = side_arc(root, 135);
    s_arc_roll  = side_arc(root, 315);

    lv_obj_t *lens = lv_obj_create(root);
    theme_apply_lens(lens, GY_LENS_D);
    lv_obj_add_flag(lens, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lens, lens_cb, LV_EVENT_CLICKED, NULL);

    /* EĞİM katmanı */
    s_horizon = lv_line_create(lens);
    lv_obj_set_size(s_horizon, GY_LENS_D, GY_LENS_D);
    lv_obj_set_pos(s_horizon, 0, 0);
    lv_obj_set_style_line_width(s_horizon, 3, 0);
    lv_obj_set_style_line_color(s_horizon, t->primary, 0);
    lv_obj_set_style_line_rounded(s_horizon, true, 0);
    lv_obj_add_flag(s_horizon, LV_OBJ_FLAG_IGNORE_LAYOUT);
    set_horizon(0, 0);

    static const lv_point_t wing_l[] = { { 0, 0 }, { 34, 0 }, { 44, 10 } };
    static const lv_point_t wing_r[] = { { 44, 0 }, { 10, 0 }, { 0, 10 } };
    s_wings[0] = lv_line_create(lens);
    lv_line_set_points(s_wings[0], wing_l, 3);
    lv_obj_align(s_wings[0], LV_ALIGN_CENTER, -40, 5);
    s_wings[1] = lv_line_create(lens);
    lv_line_set_points(s_wings[1], wing_r, 3);
    lv_obj_align(s_wings[1], LV_ALIGN_CENTER, 40, 5);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_line_width(s_wings[i], 4, 0);
        lv_obj_set_style_line_color(s_wings[i], t->secondary, 0);
        lv_obj_set_style_line_rounded(s_wings[i], true, 0);
    }
    s_wings[2] = lv_obj_create(lens);
    lv_obj_remove_style_all(s_wings[2]);
    lv_obj_set_size(s_wings[2], 8, 8);
    lv_obj_set_style_radius(s_wings[2], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_wings[2], t->secondary, 0);
    lv_obj_set_style_bg_opa(s_wings[2], LV_OPA_COVER, 0);
    lv_obj_align(s_wings[2], LV_ALIGN_CENTER, 0, 0);

    /* G katmanı */
    s_glayer = lv_obj_create(lens);
    lv_obj_remove_style_all(s_glayer);
    lv_obj_set_size(s_glayer, GY_LENS_D, GY_LENS_D);
    lv_obj_center(s_glayer);
    lv_obj_clear_flag(s_glayer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    ring(s_glayer, GY_G_RING * 2, LV_OPA_60);   /* 1 g */
    ring(s_glayer, GY_G_RING, LV_OPA_40);       /* 0.5 g */
    static lv_point_t cross_h[2] = { { 0, GY_LENS_D / 2 }, { GY_LENS_D, GY_LENS_D / 2 } };
    static lv_point_t cross_v[2] = { { GY_LENS_D / 2, 0 }, { GY_LENS_D / 2, GY_LENS_D } };
    lv_obj_t *lines[2] = { lv_line_create(s_glayer), lv_line_create(s_glayer) };
    lv_line_set_points(lines[0], cross_h, 2);
    lv_line_set_points(lines[1], cross_v, 2);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_line_color(lines[i], t->text_dim, 0);
        lv_obj_set_style_line_opa(lines[i], LV_OPA_30, 0);
        lv_obj_set_style_line_width(lines[i], 1, 0);
    }
    s_trail = lv_line_create(s_glayer);
    lv_obj_set_size(s_trail, GY_LENS_D, GY_LENS_D);
    lv_obj_set_pos(s_trail, 0, 0);
    lv_obj_set_style_line_width(s_trail, 4, 0);
    lv_obj_set_style_line_rounded(s_trail, true, 0);
    lv_obj_set_style_line_color(s_trail, t->secondary, 0);
    lv_obj_set_style_line_opa(s_trail, LV_OPA_40, 0);
    lv_line_set_points(s_trail, s_tr_pts, 0);
    s_ball = lv_obj_create(s_glayer);
    lv_obj_remove_style_all(s_ball);
    lv_obj_set_size(s_ball, GY_BALL_D, GY_BALL_D);
    lv_obj_set_style_radius(s_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ball, t->secondary, 0);
    lv_obj_set_style_bg_opa(s_ball, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ball, t->text, 0);
    lv_obj_set_style_border_width(s_ball, 2, 0);
    lv_obj_clear_flag(s_ball, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_glayer, LV_OBJ_FLAG_HIDDEN);

    s_mode_lbl = lv_label_create(lens);
    lv_obj_set_style_text_font(s_mode_lbl, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_mode_lbl, t->text_dim, 0);
    lv_obj_set_style_text_letter_space(s_mode_lbl, 2, 0);
    lv_obj_align(s_mode_lbl, LV_ALIGN_BOTTOM_MID, 0, -26);

    s_val_pitch = side_value(root, -GY_VAL_X, &s_name_l);
    s_val_roll  = side_value(root,  GY_VAL_X, &s_name_r);

    s_val_incl = lv_label_create(root);
    lv_label_set_text_static(s_val_incl, "--");
    lv_obj_set_style_text_font(s_val_incl, t->font_tr_md, 0);
    lv_obj_set_style_text_color(s_val_incl, t->text, 0);
    lv_obj_align(s_val_incl, LV_ALIGN_CENTER, 0, GY_INCL_Y);

    s_hint = lv_label_create(root);
    lv_label_set_text_static(s_hint, "");
    lv_obj_set_style_text_font(s_hint, t->font_tr_sm, 0);
    lv_obj_set_style_text_color(s_hint, t->text_dim, 0);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, GY_HINT_Y);

    s_zero_btn = pill_btn(root, "SIFIRLA", -GY_BTN_DX, zero_cb);
    pill_btn(root, "KALİBRE", GY_BTN_DX, cal_open_cb);

    create_cal_panel(root);
}

/* --- güncelleme ---------------------------------------------------------- */
static void apply_mode(void)
{
    bool g = s_mode == MODE_G;
    lv_obj_t *tilt[] = { s_horizon, s_wings[0], s_wings[1], s_wings[2] };
    for (int i = 0; i < 4; i++) {
        if (g) lv_obj_add_flag(tilt[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(tilt[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (g) lv_obj_clear_flag(s_glayer, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_glayer, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_static(s_mode_lbl, g ? "G-METRE" : "EĞİM");
    lv_label_set_text_static(s_name_l, g ? "İVME" : "PITCH");
    lv_label_set_text_static(s_name_r, g ? "YANAL" : "ROLL");
    memset(s_prev_val, 0, sizeof(s_prev_val));
    memset(s_prev_lvl, -1, sizeof(s_prev_lvl));
    s_prev_hz_p = s_prev_hz_r = INT16_MIN;
    s_tr_n = 0;
}

static void update_cal_panel(const imu_snapshot_t *s)
{
    if (lv_obj_has_flag(s_cal, LV_OBJ_FLAG_HIDDEN) || s_cal_shown_state < 0) {
        return;
    }
    if (s->calib_state == IMU_CAL_RUNNING) {
        lv_arc_set_value(s_cal_arc, (int16_t)(s->calib_progress * 100));
        if (s_cal_shown_state != IMU_CAL_RUNNING + 100) {
            s_cal_shown_state = IMU_CAL_RUNNING + 100;
            lv_label_set_text_static(s_cal_msg, "Ölçülüyor...\nKıpırdamayın.");
            lv_label_set_text_static(s_cal_go_lbl, "...");
        }
    } else if (s->calib_state != (int)s_cal_shown_state && s_cal_shown_state >= IMU_CAL_RUNNING) {
        s_cal_shown_state = s->calib_state;
        if (s->calib_state == IMU_CAL_OK) {
            lv_arc_set_value(s_cal_arc, 100);
            lv_label_set_text_static(s_cal_msg, "Kalibre edildi.\nİleri yön sürüşte otomatik\niyileştirilir.");
            lv_label_set_text_static(s_cal_go_lbl, "TAMAM");
        } else if (s->calib_state == IMU_CAL_MOVED) {
            lv_arc_set_value(s_cal_arc, 0);
            lv_label_set_text_static(s_cal_msg, "Hareket algılandı.\nAraç dururken tekrar deneyin.");
            lv_label_set_text_static(s_cal_go_lbl, "TEKRAR");
        }
    }
}

void screen_gyro_update(const imu_snapshot_t *snap)
{
    const ui_theme_t *t = theme_get();
    uint32_t now = lv_tick_get();

    if (s_mode_dirty) {
        s_mode_dirty = false;
        apply_mode();
    }
    if (s_flash_until && now > s_flash_until) {
        s_flash_until = 0;
        lv_obj_set_style_bg_color(s_zero_btn, t->primary, 0);
        lv_obj_set_style_bg_opa(s_zero_btn, LV_OPA_20, 0);
    }
    update_cal_panel(snap);

    if (!snap || !snap->fresh) {
        set_text_cached(s_val_pitch, 0, "--");
        set_text_cached(s_val_roll, 1, "--");
        set_text_cached(s_val_incl, 2, "--");
        set_text_cached(s_hint, 3, "Sensör yok");
        s_have_f = false;
        return;
    }

    /* Ekran yumuşatması (füzyon zaten filtreli; sayıların titremesini keser) */
    if (!s_have_f) {
        s_fp = snap->pitch_deg; s_fr = snap->roll_deg;
        s_fl = snap->g_long;    s_ft = snap->g_lat;
        s_have_f = true;
    } else {
        s_fp += GY_SMOOTH * (snap->pitch_deg - s_fp);
        s_fr += GY_SMOOTH * (snap->roll_deg - s_fr);
        s_fl += GY_SMOOTH * (snap->g_long - s_fl);
        s_ft += GY_SMOOTH * (snap->g_lat - s_ft);
    }

    char b[32];
    int8_t lv[3];
    if (s_mode == MODE_TILT) {
        int pv = (int)lroundf(fmaxf(-GY_RANGE, fminf(GY_RANGE, s_fp)));
        int rv = (int)lroundf(fmaxf(-GY_RANGE, fminf(GY_RANGE, s_fr)));
        lv_arc_set_value(s_arc_pitch, (int16_t)pv);
        lv_arc_set_value(s_arc_roll, (int16_t)rv);
        int16_t hp = (int16_t)lroundf(s_fp), hr = (int16_t)lroundf(s_fr);
        if (hp != s_prev_hz_p || hr != s_prev_hz_r) {
            s_prev_hz_p = hp;
            s_prev_hz_r = hr;
            set_horizon(hp, hr);
        }
        float incl = sqrtf(s_fp * s_fp + s_fr * s_fr);
        snprintf(b, sizeof(b), "%+d\xC2\xB0", (int)lroundf(s_fp));
        set_text_cached(s_val_pitch, 0, b);
        snprintf(b, sizeof(b), "%+d\xC2\xB0", (int)lroundf(s_fr));
        set_text_cached(s_val_roll, 1, b);
        snprintf(b, sizeof(b), "EĞİM %d\xC2\xB0  MAKS %d\xC2\xB0/%d\xC2\xB0", (int)lroundf(incl),
                 (int)lroundf(snap->peak_pitch), (int)lroundf(snap->peak_roll));
        set_text_cached(s_val_incl, 2, b);
        lv[0] = angle_level(s_fp);
        lv[1] = angle_level(s_fr);
        lv[2] = angle_level(incl);
    } else {
        /* G: yaylar ±1 g → ±45; top hissedilen kuvvet (fren → ileri/yukarı) */
        int lv_ = (int)lroundf(fmaxf(-1, fminf(1, s_fl)) * GY_RANGE);
        int tv = (int)lroundf(fmaxf(-1, fminf(1, -s_ft)) * GY_RANGE);
        lv_arc_set_value(s_arc_pitch, (int16_t)lv_);
        lv_arc_set_value(s_arc_roll, (int16_t)tv);
        float gx = fmaxf(-1.2f, fminf(1.2f, s_ft));    /* sola ivme → kuvvet sağa → +x */
        float gy = fmaxf(-1.2f, fminf(1.2f, s_fl));    /* fren (−) → kuvvet ileri → yukarı (−y) */
        lv_coord_t bx = (lv_coord_t)(GY_LENS_D / 2 + gx * GY_G_RING);
        lv_coord_t by = (lv_coord_t)(GY_LENS_D / 2 + gy * GY_G_RING);
        lv_obj_set_pos(s_ball, bx - GY_BALL_D / 2, by - GY_BALL_D / 2);
        if (now - s_last_trail > 60) {
            s_last_trail = now;
            if (s_tr_n == GY_TRAIL) {
                memmove(s_tr_pts, s_tr_pts + 1, (GY_TRAIL - 1) * sizeof(lv_point_t));
                s_tr_n--;
            }
            s_tr_pts[s_tr_n++] = (lv_point_t){ bx, by };
            lv_line_set_points(s_trail, s_tr_pts, (uint16_t)s_tr_n);
        }
        snprintf(b, sizeof(b), "%.2f g", fabsf(s_fl));
        set_text_cached(s_val_pitch, 0, b);
        snprintf(b, sizeof(b), "%.2f g", fabsf(s_ft));
        set_text_cached(s_val_roll, 1, b);
        snprintf(b, sizeof(b), "MAKS %.2f g", snap->peak_g);
        set_text_cached(s_val_incl, 2, b);
        lv[0] = g_level(s_fl);
        lv[1] = g_level(s_ft);
        lv[2] = g_level(sqrtf(s_fl * s_fl + s_ft * s_ft));
    }

    if (lv[0] != s_prev_lvl[0]) {
        s_prev_lvl[0] = lv[0];
        lv_obj_set_style_arc_color(s_arc_pitch, level_color(lv[0]), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(s_val_pitch, level_color(lv[0]), 0);
    }
    if (lv[1] != s_prev_lvl[1]) {
        s_prev_lvl[1] = lv[1];
        lv_obj_set_style_arc_color(s_arc_roll, level_color(lv[1]), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(s_val_roll, level_color(lv[1]), 0);
    }
    if (lv[2] != s_prev_lvl[2]) {
        s_prev_lvl[2] = lv[2];
        lv_obj_set_style_line_color(s_horizon, lv[2] == 0 ? t->primary : level_color(lv[2]), 0);
        lv_obj_set_style_bg_color(s_ball, level_color(lv[2]), 0);
    }

    const char *hint = !snap->calibrated ? "Kalibre edilmedi"
                     : snap->stationary ? "Duruyor"
                     : snap->speed_comp ? "Hız telafisi açık"
                     : "";
    set_text_cached(s_hint, 3, hint);
}
