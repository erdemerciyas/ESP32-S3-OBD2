#include "screen_splash.h"
#include "theme.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include <string.h>

/* Boot sequence — instrument-cluster "ignition on" on the round panel.
 *
 *   0    – 700 ms  outer ring traces itself from 12 o'clock
 *   200  – 1500    gauge sweep 0 → max → 0 (same 270° geometry as the dash),
 *                  colour runs cyan → orange → red with the needle
 *   500  – 1100    OBD2 wordmark fades in while its letter spacing tightens
 *   900  – 1400    vehicle profile + live boot status fade in
 *   2350 – 2650    fade to background, then the UI fades in from it
 *
 * Everything is driven from one timer off elapsed time, so the sequence is
 * deterministic and each frame only touches what actually moved. */

#define SP_TOTAL_MS     2650
#define SP_FADE_IN_MS   300
#define SP_FRAME_MS     16
#define SP_RING_D       444
#define SP_GAUGE_D      372
#define SP_GAUGE_W      10
#define SP_RANGE        1000
#define SP_REDLINE      850     /* last 15 % of the sweep */
#define SP_TITLE_Y      (-18)
#define SP_SUB_Y        50
#define SP_PROFILE_Y    92
#define SP_STATUS_Y     122

static lv_obj_t     *s_root;
static lv_obj_t     *s_trace;
static lv_obj_t     *s_gauge;
static lv_obj_t     *s_redline;
static lv_obj_t     *s_title;
static lv_obj_t     *s_sub;
static lv_obj_t     *s_profile;
static lv_obj_t     *s_status;
static lv_obj_t     *s_veil;
static lv_timer_t   *s_timer;
static lv_timer_cb_t s_finish_cb;
static uint32_t      s_t0;
static char          s_prev_status[64];

/* Last applied style values — LVGL restyles (and invalidates) on every set,
 * so each property is only pushed when it actually changes. */
static uint16_t s_p_color;
static int16_t  s_p_red = -1, s_p_title = -1, s_p_ls = -1, s_p_sub = -1;
static int16_t  s_p_info = -1, s_p_veil = -1;

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

static void veil_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static bool changed(int16_t *prev, int16_t v)
{
    if (*prev == v) {
        return false;
    }
    *prev = v;
    return true;
}

static void finish(void)
{
    lv_timer_del(s_timer);
    s_timer = NULL;

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
        s_finish_cb(NULL);      /* deletes the splash root, shows the tabview */
    }
}

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t t = lv_tick_get() - s_t0;
    if (t >= SP_TOTAL_MS) {
        finish();
        return;
    }

    /* Outer ring trace */
    lv_arc_set_value(s_trace, (int16_t)(ease_out(span(t, 0, 700)) * 360.0f));

    /* Needle sweep: up 200–900, down 900–1500 */
    float up = ease_in_out(span(t, 200, 900));
    float down = ease_in_out(span(t, 900, 1500));
    float k = up - down;
    lv_arc_set_value(s_gauge, (int16_t)(k * SP_RANGE));
    lv_color_t c = theme_rpm_gradient_color(k * 7000.0f);
    if (c.full != s_p_color) {
        s_p_color = c.full;
        lv_obj_set_style_arc_color(s_gauge, c, LV_PART_INDICATOR);
    }
    int16_t v = opa_of(span(t, 200, 600)) / 2;
    if (changed(&s_p_red, v)) {
        lv_obj_set_style_arc_opa(s_redline, (lv_opa_t)v, LV_PART_INDICATOR);
    }

    /* Wordmark */
    float tf = ease_out(span(t, 500, 1100));
    if (changed(&s_p_title, opa_of(tf))) {
        lv_obj_set_style_text_opa(s_title, (lv_opa_t)s_p_title, 0);
    }
    if (changed(&s_p_ls, (int16_t)(4 + (1.0f - tf) * 20.0f))) {
        lv_obj_set_style_text_letter_space(s_title, s_p_ls, 0);
    }
    if (changed(&s_p_sub, opa_of(span(t, 700, 1200)))) {
        lv_obj_set_style_text_opa(s_sub, (lv_opa_t)s_p_sub, 0);
    }

    /* Profile + live boot status (BLE may already be scanning) */
    if (changed(&s_p_info, opa_of(span(t, 900, 1400)))) {
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

    /* Fade out to the background */
    if (changed(&s_p_veil, opa_of(span(t, 2350, SP_TOTAL_MS)))) {
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

    /* Outer ring trace (full circle from 12 o'clock) */
    s_trace = theme_create_arc(s_root, SP_RING_D, 2);
    lv_arc_set_rotation(s_trace, 270);
    lv_arc_set_bg_angles(s_trace, 0, 360);
    lv_arc_set_range(s_trace, 0, 360);
    lv_arc_set_value(s_trace, 0);
    lv_obj_set_style_arc_opa(s_trace, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_trace, LV_OPA_60, LV_PART_INDICATOR);

    /* Gauge: track, redline band, then the sweep (track-less) with tip dot */
    lv_obj_t *track = theme_create_arc(s_root, SP_GAUGE_D, SP_GAUGE_W);
    lv_arc_set_rotation(track, 135);
    lv_arc_set_bg_angles(track, 0, 270);
    lv_obj_set_style_arc_rounded(track, false, LV_PART_MAIN);

    s_redline = theme_create_arc(s_root, SP_GAUGE_D, SP_GAUGE_W);
    lv_arc_set_rotation(s_redline, 135);
    lv_arc_set_bg_angles(s_redline, 0, 270);
    lv_arc_set_range(s_redline, 0, SP_RANGE);
    lv_arc_set_angles(s_redline, SP_REDLINE * 270 / SP_RANGE, 270);
    lv_obj_set_style_arc_opa(s_redline, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_redline, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_redline, t->crit, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_redline, LV_OPA_TRANSP, LV_PART_INDICATOR);

    s_gauge = theme_create_arc(s_root, SP_GAUGE_D, SP_GAUGE_W);
    lv_arc_set_rotation(s_gauge, 135);
    lv_arc_set_bg_angles(s_gauge, 0, 270);
    lv_arc_set_range(s_gauge, 0, SP_RANGE);
    lv_arc_set_value(s_gauge, 0);
    lv_obj_set_style_arc_opa(s_gauge, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_gauge, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_gauge, t->text, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_gauge, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(s_gauge, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_gauge, 3, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_gauge, 0, LV_PART_KNOB);

    s_title = mk_label(s_root, t->font_value, t->text, SP_TITLE_Y);
    lv_label_set_text_static(s_title, "OBD2");

    s_sub = mk_label(s_root, &lv_font_montserrat_14, t->primary, SP_SUB_Y);
    lv_obj_set_style_text_letter_space(s_sub, 8, 0);
    lv_label_set_text_static(s_sub, "DASHBOARD");

    s_profile = mk_label(s_root, t->font_md, t->text_dim, SP_PROFILE_Y);
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
