#include "screen_gyro.h"
#include "theme.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define GYRO_MOUNT_VERTICAL  1

#define NVS_NS       "gyro"
#define NVS_KEY_P    "poff"
#define NVS_KEY_R    "roff"

/* Round inclinometer.
 * Ring sides: pitch arc on the left (9 o'clock = level, up = nose up) and
 * roll arc on the right (3 o'clock = level, down = right side down), both
 * centre-zero over ±45°. Centre: attitude lens with a horizon line that
 * tilts with roll and shifts with pitch against a fixed vehicle marker.
 * Values sit between the lens and the arcs; total incline below. */

#define GY_ARC_D        UI_RING_D
#define GY_ARC_W        10
#define GY_RANGE        45      /* ± degrees shown on the arcs */
#define GY_LENS_D       236
#define GY_HZ_PX_DEG    2.0f    /* horizon shift per degree of pitch */
#define GY_HZ_MAX       40.0f   /* pitch clamp for the horizon (deg) */
#define GY_VAL_X        162
#define GY_SMOOTH       0.25f   /* EMA factor per frame */
#define GY_RESET_Y      (-164)
#define GY_INCL_Y       152

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
static lv_obj_t *s_arc_pitch;
static lv_obj_t *s_arc_roll;
static lv_obj_t *s_val_pitch;
static lv_obj_t *s_val_roll;
static lv_obj_t *s_val_incl;
static lv_obj_t *s_horizon;
static lv_obj_t *s_reset_btn;
static lv_point_t s_hz_pts[2];
static char     s_prev_val[3][16];
static int16_t  s_prev_hz_p = INT16_MIN;
static int16_t  s_prev_hz_r = INT16_MIN;
static int8_t   s_prev_lvl[3] = { -1, -1, -1 };
static float    s_poff = 0.0f;
static float    s_roff = 0.0f;
static float    s_raw_p = 0.0f;
static float    s_raw_r = 0.0f;
static float    s_fp = 0.0f;
static float    s_fr = 0.0f;
static bool     s_have_f = false;
static bool     s_flash = false;

/* ---------------------------------------------------------------------------
 * NVS
 * ------------------------------------------------------------------------- */
static void calib_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_P, (int32_t)(s_poff * 100.0f));
        nvs_set_i32(h, NVS_KEY_R, (int32_t)(s_roff * 100.0f));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void calib_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(h, NVS_KEY_P, &v) == ESP_OK) s_poff = (float)v / 100.0f;
        if (nvs_get_i32(h, NVS_KEY_R, &v) == ESP_OK) s_roff = (float)v / 100.0f;
        nvs_close(h);
    }
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static int8_t angle_level(float deg)
{
    float a = fabsf(deg);
    if (a < 15.0f) return 0;
    if (a < 30.0f) return 1;
    return 2;
}

static lv_color_t level_color(int8_t lvl)
{
    const ui_theme_t *t = theme_get();
    return lvl == 0 ? t->ok : lvl == 1 ? t->warn : t->crit;
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    s_poff = s_raw_p;
    s_roff = s_raw_r;
    s_flash = true;
    calib_save();
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

static lv_obj_t *side_value(lv_obj_t *root, lv_coord_t x, const char *name)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *val = lv_label_create(root);
    lv_label_set_text_static(val, "--");
    lv_obj_set_style_text_font(val, t->font_lg, 0);
    lv_obj_set_style_text_color(val, t->text, 0);
    lv_obj_align(val, LV_ALIGN_CENTER, x, -8);

    lv_obj_t *nm = lv_label_create(root);
    lv_label_set_text_static(nm, name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(nm, 1, 0);
    lv_obj_set_style_text_color(nm, t->text_dim, 0);
    lv_obj_align(nm, LV_ALIGN_CENTER, x, 18);
    return val;
}

/* Horizon chord through the lens: tilted by roll, offset by pitch, clipped to
 * the circle so the line never leaves the lens. */
static void set_horizon(float pitch, float roll)
{
    const float r = GY_LENS_D / 2.0f - 6.0f;
    float p = pitch;
    if (p > GY_HZ_MAX)  p = GY_HZ_MAX;
    if (p < -GY_HZ_MAX) p = -GY_HZ_MAX;
    float off = p * GY_HZ_PX_DEG;           /* nose up → horizon drops */
    float a = -roll * (float)M_PI / 180.0f; /* right side down → horizon turns CCW */
    float c = cosf(a), s = sinf(a);
    float half = sqrtf(r * r - off * off);
    float cx = GY_LENS_D / 2.0f - s * off;  /* normal (−sin, cos) × off */
    float cy = GY_LENS_D / 2.0f + c * off;

    s_hz_pts[0].x = (lv_coord_t)lroundf(cx - c * half);
    s_hz_pts[0].y = (lv_coord_t)lroundf(cy - s * half);
    s_hz_pts[1].x = (lv_coord_t)lroundf(cx + c * half);
    s_hz_pts[1].y = (lv_coord_t)lroundf(cy + s * half);
    lv_line_set_points(s_horizon, s_hz_pts, 2);
}

static void set_text_cached(lv_obj_t *l, int idx, const char *txt)
{
    if (strcmp(txt, s_prev_val[idx]) != 0) {
        snprintf(s_prev_val[idx], sizeof(s_prev_val[idx]), "%s", txt);
        lv_label_set_text(l, txt);
    }
}

/* ---------------------------------------------------------------------------
 * Create
 * ------------------------------------------------------------------------- */
void screen_gyro_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();
    calib_load();

    lv_obj_t *root = theme_create_root(parent);

    s_arc_pitch = side_arc(root, 135);      /* 135°..225° — left side */
    s_arc_roll  = side_arc(root, 315);      /* 315°..45°  — right side */

    /* Attitude lens */
    lv_obj_t *lens = lv_obj_create(root);
    theme_apply_lens(lens, GY_LENS_D);
    lv_obj_clear_flag(lens, LV_OBJ_FLAG_CLICKABLE);

    s_horizon = lv_line_create(lens);
    lv_obj_set_size(s_horizon, GY_LENS_D, GY_LENS_D);
    lv_obj_set_pos(s_horizon, 0, 0);
    lv_obj_set_style_line_width(s_horizon, 3, 0);
    lv_obj_set_style_line_color(s_horizon, t->primary, 0);
    lv_obj_set_style_line_rounded(s_horizon, true, 0);
    lv_obj_add_flag(s_horizon, LV_OBJ_FLAG_IGNORE_LAYOUT);
    set_horizon(0.0f, 0.0f);

    /* Fixed vehicle marker: wings + centre dot */
    static const lv_point_t wing_l[] = { { 0, 0 }, { 34, 0 }, { 44, 10 } };
    static const lv_point_t wing_r[] = { { 44, 0 }, { 10, 0 }, { 0, 10 } };
    lv_obj_t *wl = lv_line_create(lens);
    lv_line_set_points(wl, wing_l, 3);
    lv_obj_align(wl, LV_ALIGN_CENTER, -40, 5);
    lv_obj_t *wr = lv_line_create(lens);
    lv_line_set_points(wr, wing_r, 3);
    lv_obj_align(wr, LV_ALIGN_CENTER, 40, 5);
    lv_obj_t *wings[] = { wl, wr };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_line_width(wings[i], 4, 0);
        lv_obj_set_style_line_color(wings[i], t->secondary, 0);
        lv_obj_set_style_line_rounded(wings[i], true, 0);
    }
    lv_obj_t *dot = lv_obj_create(lens);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, t->secondary, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);

    s_val_pitch = side_value(root, -GY_VAL_X, "PITCH");
    s_val_roll  = side_value(root,  GY_VAL_X, "ROLL");

    s_val_incl = lv_label_create(root);
    lv_label_set_text_static(s_val_incl, "--");
    lv_obj_set_style_text_font(s_val_incl, t->font_md, 0);
    lv_obj_set_style_text_color(s_val_incl, t->text, 0);
    lv_obj_align(s_val_incl, LV_ALIGN_CENTER, 0, GY_INCL_Y);

    /* SIFIRLA: zero the current attitude */
    s_reset_btn = lv_btn_create(root);
    lv_obj_set_size(s_reset_btn, 96, 30);
    lv_obj_set_style_radius(s_reset_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_reset_btn, t->primary, 0);
    lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_color(s_reset_btn, t->primary, 0);
    lv_obj_set_style_border_width(s_reset_btn, 1, 0);
    lv_obj_set_style_shadow_width(s_reset_btn, 0, 0);
    lv_obj_set_style_pad_all(s_reset_btn, 0, 0);
    lv_obj_add_flag(s_reset_btn, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_align(s_reset_btn, LV_ALIGN_CENTER, 0, GY_RESET_Y);
    lv_obj_add_event_cb(s_reset_btn, reset_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(s_reset_btn);
    lv_label_set_text_static(bl, "SIFIRLA");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(bl, 1, 0);
    lv_obj_set_style_text_color(bl, t->primary, 0);
    lv_obj_center(bl);
}

/* ---------------------------------------------------------------------------
 * Update
 * ------------------------------------------------------------------------- */
void screen_gyro_update(const imu_snapshot_t *snap)
{
    const ui_theme_t *t = theme_get();

    if (!snap || !snap->fresh) {
        set_text_cached(s_val_pitch, 0, "--");
        set_text_cached(s_val_roll, 1, "--");
        set_text_cached(s_val_incl, 2, "--");
        s_have_f = false;
        return;
    }

    float pitch, roll;
#if GYRO_MOUNT_VERTICAL
    {
        float ax = snap->accel_x, ay = snap->accel_y, az = snap->accel_z;
        float cu = -ax, cf = az, cr = ay;
        float n = sqrtf(cf * cf + cu * cu);
        pitch = (n > 0.1f) ? atan2f(cf, cu) * 180.0f / M_PI : 0.0f;
        n = sqrtf(cr * cr + cu * cu);
        roll  = (n > 0.1f) ? atan2f(cr, cu) * 180.0f / M_PI : 0.0f;
    }
#else
    pitch = snap->pitch_deg;
    roll  = snap->roll_deg;
#endif

    s_raw_p = pitch;
    s_raw_r = roll;

    /* Light smoothing: kills accelerometer jitter without visible lag. */
    float dp = pitch - s_poff;
    float dr = roll  - s_roff;
    if (!s_have_f) {
        s_fp = dp;
        s_fr = dr;
        s_have_f = true;
    } else {
        s_fp += GY_SMOOTH * (dp - s_fp);
        s_fr += GY_SMOOTH * (dr - s_fr);
    }
    dp = s_fp;
    dr = s_fr;
    float incline = sqrtf(dp * dp + dr * dr);

    /* Reset flash */
    static uint32_t fe = 0;
    uint32_t now = lv_tick_get();
    if (s_flash) {
        s_flash = false; fe = now + 500;
        lv_obj_set_style_bg_color(s_reset_btn, t->ok, 0);
        lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_50, 0);
    }
    if (fe && now > fe) {
        fe = 0;
        lv_obj_set_style_bg_color(s_reset_btn, t->primary, 0);
        lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_20, 0);
    }

    /* Arcs (lv_arc_set_value is a no-op when unchanged). Pitch grows upward
     * on the left arc, whose angles run bottom → top. */
    int32_t pv = (int32_t)lroundf(dp);
    if (pv < -GY_RANGE) pv = -GY_RANGE;
    if (pv > GY_RANGE)  pv = GY_RANGE;
    lv_arc_set_value(s_arc_pitch, (int16_t)pv);

    int32_t rv = (int32_t)lroundf(dr);
    if (rv < -GY_RANGE) rv = -GY_RANGE;
    if (rv > GY_RANGE)  rv = GY_RANGE;
    lv_arc_set_value(s_arc_roll, (int16_t)rv);

    /* Horizon: whole-degree steps keep redraws to real movement. */
    int16_t hp = (int16_t)lroundf(dp);
    int16_t hr = (int16_t)lroundf(dr);
    if (hp != s_prev_hz_p || hr != s_prev_hz_r) {
        s_prev_hz_p = hp;
        s_prev_hz_r = hr;
        set_horizon(hp, hr);
    }

    /* Values + colours, only on change */
    char b[16];
    snprintf(b, sizeof(b), "%+d\xC2\xB0", (int)pv);
    set_text_cached(s_val_pitch, 0, b);
    snprintf(b, sizeof(b), "%+d\xC2\xB0", (int)rv);
    set_text_cached(s_val_roll, 1, b);
    snprintf(b, sizeof(b), "INCLINE %d\xC2\xB0", (int)lroundf(incline));
    set_text_cached(s_val_incl, 2, b);

    int8_t lv[3] = { angle_level(dp), angle_level(dr), angle_level(incline) };
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
    }
}
