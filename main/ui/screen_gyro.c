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

/* Twin arcs — centered pair, values inside */
#define ARC_SZ          210
#define ARC_W           10
#define ARC_GAP         8
#define ARC_ROT         135
#define ARC_SWEEP       270
#define ARC_TOP         70

/* Bottom row */
#define ROW_H           72
#define ROW_BOT         (-78)

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
static lv_obj_t *s_arc_pitch;
static lv_obj_t *s_arc_roll;
static lv_obj_t *s_val_pitch;
static lv_obj_t *s_val_roll;
static lv_obj_t *s_lbl_pitch;
static lv_obj_t *s_lbl_roll;
static lv_obj_t *s_reset_btn;
static lv_obj_t *s_pills[3];
static lv_obj_t *s_pill_vals[3];
static char     s_prev_val[3][16];
static float    s_poff = 0.0f;
static float    s_roff = 0.0f;
static float    s_raw_p = 0.0f;
static float    s_raw_r = 0.0f;
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
static lv_color_t angle_color(float deg)
{
    float a = fabsf(deg);
    if (a < 15.0f) return theme_get()->ok;
    if (a < 30.0f) return theme_get()->warn;
    return theme_get()->crit;
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    s_poff = s_raw_p;
    s_roff = s_raw_r;
    s_flash = true;
    calib_save();
}

/* ---------------------------------------------------------------------------
 * Create
 * ------------------------------------------------------------------------- */
void screen_gyro_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();
    calib_load();

    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_coord_t cx = UI_VIEWPORT_SZ / 2;
    lv_coord_t total_w = ARC_SZ * 2 + ARC_GAP;
    lv_coord_t arc_l_x  = cx - total_w / 2;
    lv_coord_t arc_r_x  = cx + total_w / 2 - ARC_SZ;
    lv_coord_t arc_cx_l = arc_l_x + ARC_SZ / 2;
    lv_coord_t arc_cx_r = arc_r_x + ARC_SZ / 2;
    lv_coord_t arc_cy   = ARC_TOP + ARC_SZ / 2;

    /* Header */
    lv_obj_t *hdr = lv_label_create(parent);
    lv_label_set_text(hdr, "GYRO");
    lv_obj_set_style_text_font(hdr, t->font_md, 0);
    lv_obj_set_style_text_color(hdr, t->text, 0);
    lv_obj_set_style_text_align(hdr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 8);

    /* SIFIRLA button */
    s_reset_btn = lv_btn_create(parent);
    lv_obj_set_size(s_reset_btn, 72, 26);
    lv_obj_set_style_radius(s_reset_btn, 8, 0);
    lv_obj_set_style_bg_color(s_reset_btn, t->primary, 0);
    lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_30, 0);
    lv_obj_set_style_border_color(s_reset_btn, t->primary, 0);
    lv_obj_set_style_border_width(s_reset_btn, 1, 0);
    lv_obj_set_style_pad_all(s_reset_btn, 0, 0);
    lv_obj_add_flag(s_reset_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_reset_btn, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_add_event_cb(s_reset_btn, reset_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(s_reset_btn);
    lv_label_set_text(bl, "SIFIRLA");
    lv_obj_set_style_text_font(bl, t->font_sm, 0);
    lv_obj_set_style_text_color(bl, t->primary, 0);
    lv_obj_center(bl);

    /* --- Pitch arc (left) --- */
    s_arc_pitch = lv_arc_create(parent);
    lv_obj_set_size(s_arc_pitch, ARC_SZ, ARC_SZ);
    lv_arc_set_rotation(s_arc_pitch, ARC_ROT);
    lv_arc_set_bg_angles(s_arc_pitch, 0, ARC_SWEEP);
    lv_arc_set_range(s_arc_pitch, -45, 45);
    lv_arc_set_value(s_arc_pitch, 0);
    lv_obj_remove_style(s_arc_pitch, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_pitch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_arc_pitch, t->primary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_pitch, t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_pitch, ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_pitch, ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc_pitch, false, 0);
    lv_obj_add_flag(s_arc_pitch, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(s_arc_pitch, arc_l_x, ARC_TOP);

    /* Pitch value INSIDE arc — positioned in upper-center of circle */
    s_val_pitch = lv_label_create(parent);
    lv_label_set_text(s_val_pitch, "--");
    lv_obj_set_style_text_font(s_val_pitch, t->font_xl, 0);
    lv_obj_set_style_text_color(s_val_pitch, t->text, 0);
    lv_obj_set_style_text_align(s_val_pitch, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_val_pitch, ARC_SZ);
    lv_obj_add_flag(s_val_pitch, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(s_val_pitch, arc_l_x, arc_cy - 30);

    /* Pitch label inside arc — below the value with gap */
    s_lbl_pitch = lv_label_create(parent);
    lv_label_set_text(s_lbl_pitch, "PITCH");
    lv_obj_set_style_text_font(s_lbl_pitch, t->font_md, 0);
    lv_obj_set_style_text_color(s_lbl_pitch, t->text_dim, 0);
    lv_obj_set_style_text_align(s_lbl_pitch, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_pitch, ARC_SZ);
    lv_obj_add_flag(s_lbl_pitch, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(s_lbl_pitch, arc_l_x, arc_cy + 16);

    /* --- Roll arc (right) --- */
    s_arc_roll = lv_arc_create(parent);
    lv_obj_set_size(s_arc_roll, ARC_SZ, ARC_SZ);
    lv_arc_set_rotation(s_arc_roll, ARC_ROT);
    lv_arc_set_bg_angles(s_arc_roll, 0, ARC_SWEEP);
    lv_arc_set_range(s_arc_roll, -45, 45);
    lv_arc_set_value(s_arc_roll, 0);
    lv_obj_remove_style(s_arc_roll, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_roll, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_arc_roll, t->secondary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_roll, t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_roll, ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_roll, ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc_roll, false, 0);
    lv_obj_add_flag(s_arc_roll, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(s_arc_roll, arc_r_x, ARC_TOP);

    /* Roll value INSIDE arc */
    s_val_roll = lv_label_create(parent);
    lv_label_set_text(s_val_roll, "--");
    lv_obj_set_style_text_font(s_val_roll, t->font_xl, 0);
    lv_obj_set_style_text_color(s_val_roll, t->text, 0);
    lv_obj_set_style_text_align(s_val_roll, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_val_roll, ARC_SZ);
    lv_obj_add_flag(s_val_roll, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(s_val_roll, arc_r_x, arc_cy - 30);

    /* Roll label inside arc */
    s_lbl_roll = lv_label_create(parent);
    lv_label_set_text(s_lbl_roll, "ROLL");
    lv_obj_set_style_text_font(s_lbl_roll, t->font_md, 0);
    lv_obj_set_style_text_color(s_lbl_roll, t->text_dim, 0);
    lv_obj_set_style_text_align(s_lbl_roll, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_roll, ARC_SZ);
    lv_obj_add_flag(s_lbl_roll, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(s_lbl_roll, arc_r_x, arc_cy + 16);

    /* --- Bottom data row (equal-width pills, chord-safe) --- */
    lv_coord_t row_top = UI_VIEWPORT_SZ + ROW_BOT - ROW_H;
    lv_coord_t row_bot = UI_VIEWPORT_SZ + ROW_BOT;
    lv_coord_t row_w   = theme_safe_width(row_top, row_bot);
    lv_coord_t pill_w  = (row_w - UI_GAP * 2) / 3;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, row_w, ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, UI_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, ROW_BOT);

    const char *names[] = { "Pitch", "Roll", "Incline" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *pill = lv_obj_create(row);
        theme_apply_glass_card(pill);
        lv_obj_set_style_clip_corner(pill, false, 0);
        lv_obj_set_width(pill, pill_w);
        lv_obj_set_height(pill, LV_PCT(100));
        lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(pill, 6, 0);
        lv_obj_set_style_pad_hor(pill, 0, 0);
        lv_obj_set_style_pad_row(pill, 4, 0);

        lv_obj_t *nm = lv_label_create(pill);
        lv_label_set_text(nm, names[i]);
        lv_obj_set_style_text_font(nm, t->font_md, 0);
        lv_obj_set_style_text_color(nm, t->text_dim, 0);

        lv_obj_t *val = lv_label_create(pill);
        lv_label_set_text(val, "--");
        lv_obj_set_style_text_font(val, t->font_xl, 0);
        lv_obj_set_style_text_color(val, t->text, 0);

        s_pills[i] = pill;
        s_pill_vals[i] = val;
        s_prev_val[i][0] = '\0';
    }
    lv_obj_move_foreground(row);
}

/* ---------------------------------------------------------------------------
 * Update
 * ------------------------------------------------------------------------- */
void screen_gyro_update(const imu_snapshot_t *snap)
{
    if (!snap || !snap->fresh) {
        lv_label_set_text(s_val_pitch, "--");
        lv_label_set_text(s_val_roll, "--");
        for (int i = 0; i < 3; i++) {
            if (strcmp(s_prev_val[i], "--") != 0) {
                strncpy(s_prev_val[i], "--", sizeof(s_prev_val[i]));
                lv_label_set_text(s_pill_vals[i], "--");
            }
        }
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

    float incline = sqrtf(pitch * pitch + roll * roll);
    s_raw_p = pitch;
    s_raw_r = roll;

    float dp = pitch - s_poff;
    float dr = roll  - s_roff;

    const ui_theme_t *t = theme_get();

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
        lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_30, 0);
    }

    /* Arcs */
    int32_t pv = (int32_t)dp;
    if (pv < -45) pv = -45;
    if (pv > 45)  pv = 45;
    lv_arc_set_value(s_arc_pitch, pv);
    lv_obj_set_style_arc_color(s_arc_pitch, angle_color(dp), LV_PART_INDICATOR);

    int32_t rv = (int32_t)dr;
    if (rv < -45) rv = -45;
    if (rv > 45)  rv = 45;
    lv_arc_set_value(s_arc_roll, rv);
    lv_obj_set_style_arc_color(s_arc_roll, angle_color(dr), LV_PART_INDICATOR);

    /* Values inside arcs */
    char b[16];
    snprintf(b, sizeof(b), "%+.0f°", dp);
    lv_label_set_text(s_val_pitch, b);
    lv_obj_set_style_text_color(s_val_pitch, angle_color(dp), 0);

    snprintf(b, sizeof(b), "%+.0f°", dr);
    lv_label_set_text(s_val_roll, b);
    lv_obj_set_style_text_color(s_val_roll, angle_color(dr), 0);

    /* Bottom pills */
    snprintf(b, sizeof(b), "%+.0f", dp);
    if (strcmp(b, s_prev_val[0])) {
        strncpy(s_prev_val[0], b, 16); lv_label_set_text(s_pill_vals[0], b); }
    lv_obj_set_style_text_color(s_pill_vals[0], angle_color(dp), 0);

    snprintf(b, sizeof(b), "%+.0f", dr);
    if (strcmp(b, s_prev_val[1])) {
        strncpy(s_prev_val[1], b, 16); lv_label_set_text(s_pill_vals[1], b); }
    lv_obj_set_style_text_color(s_pill_vals[1], angle_color(dr), 0);

    snprintf(b, sizeof(b), "%.0f", incline);
    if (strcmp(b, s_prev_val[2])) {
        strncpy(s_prev_val[2], b, 16); lv_label_set_text(s_pill_vals[2], b); }
    lv_obj_set_style_text_color(s_pill_vals[2], angle_color(incline), 0);
}
