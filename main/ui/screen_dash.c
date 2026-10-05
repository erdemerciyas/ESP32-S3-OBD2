#include "ui.h"
#include "theme.h"
#include "bsp.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "obd_dtc.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Gauge sweep geometry: 270 degrees, rotation 135 (from 7:30 to 4:30 clockwise).
 * The arc is always the tachometer; the centre shows RPM and Speed as digits. */
#define GAUGE_ROTATION_DEG    135.0f
#define GAUGE_SWEEP_DEG       270.0f

#define TICK_STEP_RPM         1000
#define TICK_MAJOR_MAX        11          /* up to a 10 000 rpm dial */
#define DIGITS_MAX            4
#define TILE_COUNT            3

/* ------------------------------------------------------------------------
 * Motion channel — keeps RPM and Speed in lockstep.
 *
 * K-line delivers each PID only every ~150-350 ms. Instead of jumping (or a
 * fixed 80 ms ease that then stalls), each new sample starts a linear segment
 * from the currently displayed value to the new value whose duration is the
 * measured sample spacing. Both channels are evaluated on the same frame
 * clock, so RPM and Speed move continuously with the same, constant latency.
 * ------------------------------------------------------------------------ */
typedef struct {
    float    disp;
    float    from;
    float    to;
    float    last_raw;
    float    interval;      /* EMA of sample spacing (ms) */
    uint32_t last_ts;
    uint32_t last_arrival;
    uint32_t t0;
    bool     init;
} motion_t;

static void motion_reset(motion_t *m)
{
    memset(m, 0, sizeof(*m));
}

static void motion_update(motion_t *m, float raw, uint32_t ts, uint32_t now)
{
    if (!m->init) {
        m->disp = m->from = m->to = m->last_raw = raw;
        m->interval = UI_MOTION_DEFAULT_MS;
        m->last_ts = ts;
        m->last_arrival = now;
        m->t0 = now;
        m->init = true;
        return;
    }

    /* New sample: timestamp moved (ESP32) or value changed (simulator). */
    if (ts != m->last_ts || raw != m->last_raw) {
        uint32_t gap = now - m->last_arrival;
        if (gap >= UI_MOTION_MIN_MS && gap <= UI_MOTION_MAX_MS * 2) {
            m->interval += 0.3f * ((float)gap - m->interval);
        }
        if (m->interval < UI_MOTION_MIN_MS) m->interval = UI_MOTION_MIN_MS;
        if (m->interval > UI_MOTION_MAX_MS) m->interval = UI_MOTION_MAX_MS;
        m->last_ts = ts;
        m->last_raw = raw;
        m->last_arrival = now;
        m->from = m->disp;
        m->to = raw;
        m->t0 = now;
    }

    float k = (float)(now - m->t0) / m->interval;
    m->disp = (k >= 1.0f) ? m->to : m->from + (m->to - m->from) * k;
}

static bool motion_stale(const motion_t *m, uint32_t now)
{
    return !m->init || (now - m->last_arrival) > UI_DATA_STALE_MS;
}

/* ------------------------------------------------------------------------
 * Fixed-pitch digit display.
 *
 * One label per digit at a constant pitch (the widest digit of the font), so
 * numbers never jitter sideways and only the digits that actually change are
 * invalidated and redrawn.
 * ------------------------------------------------------------------------ */
typedef struct {
    lv_obj_t  *box;
    lv_obj_t  *cell[DIGITS_MAX];
    char       txt[DIGITS_MAX][2];
    uint8_t    len;
    lv_coord_t pitch;
    lv_color_t color;
} digits_t;

static void digits_create(digits_t *d, lv_obj_t *parent, const lv_font_t *font,
                          lv_color_t color)
{
    memset(d, 0, sizeof(*d));
    for (uint32_t c = '0'; c <= '9'; c++) {
        lv_coord_t w = (lv_coord_t)lv_font_get_glyph_width(font, c, 0);
        if (w > d->pitch) {
            d->pitch = w;
        }
    }
    d->color = color;

    d->box = lv_obj_create(parent);
    lv_obj_remove_style_all(d->box);
    lv_obj_set_size(d->box, d->pitch, lv_font_get_line_height(font));
    lv_obj_clear_flag(d->box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < DIGITS_MAX; i++) {
        lv_obj_t *l = lv_label_create(d->box);
        lv_obj_set_width(l, d->pitch);
        lv_obj_set_pos(l, (lv_coord_t)(i * d->pitch), 0);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, color, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text_static(l, d->txt[i]);
        d->cell[i] = l;
    }
}

static void digits_set(digits_t *d, const char *s)
{
    uint8_t len = (uint8_t)strlen(s);
    if (len > DIGITS_MAX) {
        len = DIGITS_MAX;
    }
    if (len != d->len) {
        d->len = len;
        lv_obj_set_width(d->box, (lv_coord_t)(d->pitch * (len ? len : 1)));
    }
    for (int i = 0; i < DIGITS_MAX; i++) {
        char c = (i < len) ? s[i] : '\0';
        if (c != d->txt[i][0]) {
            d->txt[i][0] = c;
            lv_label_set_text_static(d->cell[i], d->txt[i]);
        }
    }
}

static void digits_set_color(digits_t *d, lv_color_t color)
{
    if (color.full == d->color.full) {
        return;
    }
    d->color = color;
    for (int i = 0; i < DIGITS_MAX; i++) {
        lv_obj_set_style_text_color(d->cell[i], color, 0);
    }
}

/* ------------------------------------------------------------------------ */

static lv_obj_t *s_bt_icon;
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_val_arc;
static lv_obj_t *s_redline_arc;
static lv_obj_t *s_shift_seg[UI_SHIFT_LIGHT_SEGS];
static lv_obj_t *s_primary_unit;
static lv_obj_t *s_secondary_unit;
static digits_t  s_primary;
static digits_t  s_secondary;

static lv_obj_t *s_tile[TILE_COUNT];
static lv_obj_t *s_tile_val[TILE_COUNT];
static lv_obj_t *s_tile_unit[TILE_COUNT];
static char      s_prev_tile_val[TILE_COUNT][12];
static const char *s_prev_tile_unit[TILE_COUNT];
static threshold_level_t s_prev_tile_lvl[TILE_COUNT];
static int8_t    s_prev_tile_vis[TILE_COUNT] = { -1, -1, -1 };

static motion_t  s_rpm_motion;
static motion_t  s_spd_motion;
static uint32_t  s_last_click_ms;
static int8_t    s_prev_rpm_mode = -1;
static int8_t    s_prev_metric = -1;
static int8_t    s_prev_connected = -1;
static int8_t    s_prev_zone = -1;
static int32_t   s_prev_redline = -1;
static uint8_t   s_prev_shift[UI_SHIFT_LIGHT_SEGS];
static char      s_prev_status[40];
static lv_obj_t  *s_dtc_lbl;          /* arıza göstergesi: dokun → DTC sekmesi */
static uint32_t   s_dtc_seq = UINT32_MAX;

/* Buzzer alert state */
#define ALERT_BEEP_MS     400
#define ALERT_REPEAT_MS   30000

static bool     s_alert_beeping;
static uint32_t s_alert_beep_start;
static uint32_t s_alert_last_beep;
static bool     s_alert_was_crit;

static void alert_check(float coolant)
{
    uint32_t now = lv_tick_get();
    bool is_crit = (vehicle_data_coolant_level(coolant) == THRESHOLD_CRIT);

    if (s_alert_beeping && (now - s_alert_beep_start) >= ALERT_BEEP_MS) {
        bsp_buzzer_off();
        s_alert_beeping = false;
        s_alert_last_beep = now;
    }

    if (is_crit) {
        if (!s_alert_was_crit) {
            bsp_buzzer_on();
            s_alert_beeping = true;
            s_alert_beep_start = now;
        } else if (!s_alert_beeping && (now - s_alert_last_beep) >= ALERT_REPEAT_MS) {
            bsp_buzzer_on();
            s_alert_beeping = true;
            s_alert_beep_start = now;
        }
    } else {
        if (s_alert_beeping) {
            bsp_buzzer_off();
            s_alert_beeping = false;
        }
    }
    s_alert_was_crit = is_crit;
}

#define DOUBLE_TAP_MS  350

static void gauge_double_tap_cb(lv_event_t *e)
{
    (void)e;
    uint32_t now = lv_tick_get();
    if (now - s_last_click_ms <= DOUBLE_TAP_MS) {
        vehicle_data_t *vd = vehicle_data_get();
        vd->center_gauge_rpm = !vd->center_gauge_rpm;
        s_last_click_ms = 0;
    } else {
        s_last_click_ms = now;
    }
}

static threshold_level_t oil_level(float c)
{
    if (c > 135.0f) return THRESHOLD_CRIT;
    if (c > 120.0f || c < 50.0f) return THRESHOLD_WARN;
    return THRESHOLD_OK;
}

static lv_obj_t *make_arc(lv_obj_t *parent, const ui_theme_t *t)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, UI_GAUGE_SZ, UI_GAUGE_SZ);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(arc, (int16_t)GAUGE_ROTATION_DEG);
    lv_arc_set_bg_angles(arc, 0, (int16_t)GAUGE_SWEEP_DEG);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_style_arc_width(arc, UI_GAUGE_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, UI_GAUGE_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, t->arc_bg, LV_PART_MAIN);
    return arc;
}

static void point_on_dial(float ratio, float r, lv_point_t *p)
{
    float a = (GAUGE_ROTATION_DEG + ratio * GAUGE_SWEEP_DEG) * (float)M_PI / 180.0f;
    p->x = (lv_coord_t)(UI_GAUGE_SZ / 2 + r * cosf(a));
    p->y = (lv_coord_t)(UI_GAUGE_SZ / 2 + r * sinf(a));
}

/* Static dial: major ticks + numerals every 1000 rpm, minor tick at 500.
 * Built once; never touched again, so it costs nothing per frame. */
static void build_dial(lv_obj_t *parent, const ui_theme_t *t, int32_t rpm_max)
{
    static lv_point_t pts[TICK_MAJOR_MAX * 2][2];
    int majors = (int)(rpm_max / TICK_STEP_RPM);
    if (majors > TICK_MAJOR_MAX - 1) {
        majors = TICK_MAJOR_MAX - 1;
    }
    const float r_out = UI_GAUGE_SZ / 2 - UI_GAUGE_ARC_W - 2;
    int n = 0;

    for (int i = 0; i <= majors * 2; i++) {
        bool major = (i % 2) == 0;
        float ratio = (float)(i * TICK_STEP_RPM / 2) / (float)rpm_max;
        point_on_dial(ratio, r_out - (major ? UI_TICK_MARK_LEN : UI_TICK_MINOR_LEN), &pts[n][0]);
        point_on_dial(ratio, r_out, &pts[n][1]);

        lv_obj_t *ln = lv_line_create(parent);
        lv_line_set_points(ln, pts[n], 2);
        lv_obj_set_style_line_width(ln, major ? 3 : 2, 0);
        lv_obj_set_style_line_color(ln, major ? t->text : t->text_dim, 0);
        lv_obj_set_style_line_opa(ln, major ? LV_OPA_COVER : LV_OPA_60, 0);
        n++;

        /* Numerals 1..max-1 — the end numerals would sit under the tiles. */
        if (major && i > 0 && i < majors * 2) {
            lv_point_t c;
            point_on_dial(ratio, r_out - UI_TICK_MARK_LEN - 14, &c);
            lv_obj_t *num = lv_label_create(parent);
            lv_label_set_text_fmt(num, "%d", i / 2);
            lv_obj_set_style_text_font(num, t->font_sm, 0);
            lv_obj_set_style_text_color(num, t->text_dim, 0);
            lv_obj_update_layout(num);
            lv_obj_set_pos(num, c.x - lv_obj_get_width(num) / 2,
                           c.y - lv_obj_get_height(num) / 2);
        }
    }
}

static void create_tile(lv_obj_t *row, int idx, const char *name, const ui_theme_t *t)
{
    lv_obj_t *tile = lv_obj_create(row);
    theme_apply_card(tile);
    lv_obj_set_height(tile, LV_PCT(100));
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 0, 0);

    lv_obj_t *nm = lv_label_create(tile);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_font(nm, t->font_sm, 0);
    lv_obj_set_style_text_color(nm, t->text_dim, 0);

    lv_obj_t *vrow = lv_obj_create(tile);
    lv_obj_remove_style_all(vrow);
    lv_obj_set_size(vrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(vrow, 3, 0);

    s_tile_val[idx] = lv_label_create(vrow);
    lv_label_set_text(s_tile_val[idx], "--");
    lv_obj_set_style_text_font(s_tile_val[idx], t->font_data, 0);
    lv_obj_set_style_text_color(s_tile_val[idx], t->text, 0);

    s_tile_unit[idx] = lv_label_create(vrow);
    lv_label_set_text(s_tile_unit[idx], "");
    lv_obj_set_style_text_font(s_tile_unit[idx], t->font_sm, 0);
    lv_obj_set_style_text_color(s_tile_unit[idx], t->text_dim, 0);
    lv_obj_set_style_pad_bottom(s_tile_unit[idx], 4, 0);

    s_tile[idx] = tile;
}

static void tile_update(int idx, bool visible, const char *val, const char *unit,
                        threshold_level_t lvl, const ui_theme_t *t)
{
    if ((int8_t)visible != s_prev_tile_vis[idx]) {
        s_prev_tile_vis[idx] = (int8_t)visible;
        if (visible) {
            lv_obj_clear_flag(s_tile[idx], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_tile[idx], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!visible) {
        return;
    }
    if (strcmp(val, s_prev_tile_val[idx]) != 0) {
        snprintf(s_prev_tile_val[idx], sizeof(s_prev_tile_val[idx]), "%s", val);
        lv_label_set_text(s_tile_val[idx], val);
    }
    if (unit != s_prev_tile_unit[idx]) {
        s_prev_tile_unit[idx] = unit;
        lv_label_set_text_static(s_tile_unit[idx], unit);
    }
    if (lvl != s_prev_tile_lvl[idx]) {
        s_prev_tile_lvl[idx] = lvl;
        lv_obj_set_style_text_color(s_tile_val[idx],
                                    lvl == THRESHOLD_OK ? t->text : theme_threshold_color(lvl), 0);
    }
}

static void dtc_lbl_click_cb(lv_event_t *e)
{
    (void)e;
    ui_show_tab(UI_TAB_DTC);
}

/* Yalnız DTC raporu değiştiğinde (seq) güncellenir: her karede mutex yok. */
static void update_dtc_badge(const ui_theme_t *t)
{
    uint32_t seq = obd_dtc_seq();
    if (seq == s_dtc_seq) {
        return;
    }
    s_dtc_seq = seq;
    bool mil = false;
    uint8_t n = obd_dtc_active_count(&mil);
    if (n == 0) {
        lv_obj_add_flag(s_dtc_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text_fmt(s_dtc_lbl, LV_SYMBOL_WARNING " %u ARIZA", n);
    lv_obj_set_style_text_color(s_dtc_lbl, mil ? t->crit : t->secondary, 0);
    lv_obj_clear_flag(s_dtc_lbl, LV_OBJ_FLAG_HIDDEN);
}

void screen_dash_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();
    const vehicle_profile_t *profile = vehicle_profile_get();

    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);

    /* --- Gauge layer (full viewport) --- */
    lv_obj_t *gauge = lv_obj_create(parent);
    lv_obj_remove_style_all(gauge);
    lv_obj_set_size(gauge, UI_GAUGE_SZ, UI_GAUGE_SZ);
    lv_obj_clear_flag(gauge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gauge, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_FLOATING |
                           LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(gauge, LV_ALIGN_TOP_MID, 0, UI_GAUGE_TOP);
    lv_obj_add_event_cb(gauge, gauge_double_tap_cb, LV_EVENT_CLICKED, NULL);

    /* Track → redline band → value arc. Only the value arc changes at runtime
     * and LVGL invalidates just the swept angle span + knob. */
    lv_obj_t *track = make_arc(gauge, t);
    lv_arc_set_value(track, 0);
    lv_obj_set_style_arc_opa(track, LV_OPA_TRANSP, LV_PART_INDICATOR);

    s_redline_arc = make_arc(gauge, t);
    lv_arc_set_range(s_redline_arc, 0, 100);
    lv_arc_set_value(s_redline_arc, 100);
    lv_obj_set_style_arc_opa(s_redline_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_redline_arc, t->crit, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_redline_arc, LV_OPA_50, LV_PART_INDICATOR);

    s_val_arc = make_arc(gauge, t);
    lv_arc_set_range(s_val_arc, 0, profile->rpm_max);
    lv_arc_set_value(s_val_arc, 0);
    lv_obj_set_style_arc_opa(s_val_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_val_arc, t->primary, LV_PART_INDICATOR);
    /* Tip marker replaces the old needle: a small dot riding the arc end. */
    lv_obj_set_style_bg_color(s_val_arc, t->text, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_val_arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(s_val_arc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_val_arc, 3, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_val_arc, 0, LV_PART_KNOB);

    build_dial(gauge, t, profile->rpm_max);

    /* --- Status line: link info (centre) + BT icon --- */
    s_status_lbl = lv_label_create(gauge);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_font(s_status_lbl, t->font_sm, 0);
    lv_obj_set_style_text_color(s_status_lbl, t->text_dim, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, -10, UI_DASH_STATUS_TOP);

    s_bt_icon = lv_label_create(gauge);
    lv_label_set_text(s_bt_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(s_bt_icon, t->font_sm, 0);
    lv_obj_set_style_text_color(s_bt_icon, t->primary, 0);
    lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);

    s_dtc_lbl = lv_label_create(gauge);
    lv_label_set_text(s_dtc_lbl, "");
    lv_obj_set_style_text_font(s_dtc_lbl, t->font_sm, 0);
    lv_obj_set_style_pad_all(s_dtc_lbl, 6, 0);   /* dokunma alanı */
    lv_obj_align(s_dtc_lbl, LV_ALIGN_TOP_MID, 0, UI_DASH_SHIFT_Y + 12);  /* shift ışıkları altı */
    lv_obj_add_flag(s_dtc_lbl, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_dtc_lbl, dtc_lbl_click_cb, LV_EVENT_CLICKED, NULL);

    /* --- Shift light strip --- */
    {
        lv_coord_t total_w = (UI_GAUGE_SZ * 45) / 100;
        lv_coord_t seg_w = (total_w - (UI_SHIFT_LIGHT_SEGS - 1) * UI_SHIFT_LIGHT_GAP) /
                           UI_SHIFT_LIGHT_SEGS;
        for (int i = 0; i < UI_SHIFT_LIGHT_SEGS; i++) {
            lv_obj_t *seg = lv_obj_create(gauge);
            lv_obj_remove_style_all(seg);
            lv_obj_set_size(seg, seg_w, UI_SHIFT_LIGHT_H);
            lv_obj_set_style_radius(seg, 2, 0);
            lv_obj_set_style_bg_color(seg, t->arc_bg, 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
            lv_obj_align(seg, LV_ALIGN_TOP_MID,
                         (lv_coord_t)((i - (UI_SHIFT_LIGHT_SEGS - 1) / 2.0f) *
                                      (seg_w + UI_SHIFT_LIGHT_GAP)),
                         UI_DASH_SHIFT_Y);
            s_shift_seg[i] = seg;
        }
    }

    /* --- Centre readouts: primary (large) + unit, secondary + unit --- */
    digits_create(&s_primary, gauge, t->font_value, t->text);
    lv_obj_align(s_primary.box, LV_ALIGN_CENTER, 0, UI_DASH_PRIMARY_Y);

    s_primary_unit = lv_label_create(gauge);
    lv_label_set_text(s_primary_unit, "");
    lv_obj_set_style_text_font(s_primary_unit, t->font_sm, 0);
    lv_obj_set_style_text_color(s_primary_unit, t->text_dim, 0);
    lv_obj_align(s_primary_unit, LV_ALIGN_CENTER, 0, UI_DASH_UNIT_Y);

    lv_obj_t *sec_row = lv_obj_create(gauge);
    lv_obj_remove_style_all(sec_row);
    lv_obj_set_size(sec_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sec_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(sec_row, 6, 0);
    lv_obj_clear_flag(sec_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(sec_row, LV_ALIGN_CENTER, 0, UI_DASH_SECONDARY_Y);

    digits_create(&s_secondary, sec_row, t->font_xl, t->primary);
    s_secondary_unit = lv_label_create(sec_row);
    lv_label_set_text(s_secondary_unit, "");
    lv_obj_set_style_text_font(s_secondary_unit, t->font_sm, 0);
    lv_obj_set_style_text_color(s_secondary_unit, t->text_dim, 0);
    lv_obj_set_style_pad_bottom(s_secondary_unit, 12, 0);

    /* --- Bottom tiles: Coolant | Oil (if supported) | Voltage --- */
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    {
        const lv_coord_t row_b = UI_VIEWPORT_SZ - UI_STAT_BOTTOM_OFF;
        const lv_coord_t row_t = row_b - UI_DASH_DATA_H;
        lv_obj_set_size(row, theme_safe_width(row_t, row_b), UI_DASH_DATA_H);
    }
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UI_GAP_SM, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, UI_FLOAT_BOTTOM_Y);

    create_tile(row, 0, "Coolant", t);
    create_tile(row, 1, "Oil", t);
    create_tile(row, 2, "Battery", t);

    for (int i = 0; i < UI_SHIFT_LIGHT_SEGS; i++) {
        s_prev_shift[i] = 0xFF;
    }
}

static void update_status(bool connected, const vehicle_data_snapshot_t *snap)
{
    char buf[40];
    if (connected && snap->link.req_rate > 0.0f) {
        snprintf(buf, sizeof(buf), "%s  %.1f/s", snap->link.proto[0] ? snap->link.proto : "OBD",
                 snap->link.req_rate);
    } else {
        snprintf(buf, sizeof(buf), "%s", connected ? "OBD" : "No adapter");
    }
    if (strcmp(buf, s_prev_status) != 0) {
        snprintf(s_prev_status, sizeof(s_prev_status), "%s", buf);
        lv_label_set_text(s_status_lbl, buf);
        lv_obj_update_layout(s_status_lbl);
        lv_obj_align_to(s_bt_icon, s_status_lbl, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    }
    if ((int8_t)connected != s_prev_connected) {
        s_prev_connected = (int8_t)connected;
        if (connected) {
            lv_obj_clear_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_HIDDEN);
            motion_reset(&s_rpm_motion);
            motion_reset(&s_spd_motion);
        }
    }
}

static void update_shift_lights(float rpm, int32_t redline, uint32_t now, const ui_theme_t *t)
{
    float ratio = (redline > 0) ? rpm / (float)redline : 0.0f;
    bool blink = ratio >= 1.0f && ((now / 120) & 1);
    for (int i = 0; i < UI_SHIFT_LIGHT_SEGS; i++) {
        float seg_ratio = 0.70f + (0.30f * i) / (UI_SHIFT_LIGHT_SEGS - 1);
        uint8_t st = ratio >= seg_ratio ? 1 : 0;
        if (st && blink && i >= UI_SHIFT_LIGHT_SEGS - 3) {
            st = 0;
        }
        if (st != s_prev_shift[i]) {
            s_prev_shift[i] = st;
            lv_obj_set_style_bg_color(s_shift_seg[i],
                                      st ? theme_shift_light_color(seg_ratio) : t->arc_bg, 0);
        }
    }
}

void screen_dash_update(bool connected, const vehicle_data_snapshot_t *snap)
{
    const ui_theme_t *t = theme_get();
    const vehicle_profile_t *profile = vehicle_profile_get();
    uint32_t now = lv_tick_get();
    bool rpm_mode = snap->center_gauge_rpm;
    bool metric = snap->metric_units;

    update_status(connected, snap);
    update_dtc_badge(t);

    /* Redline band (profile may change at runtime). */
    if (profile->rpm_redline != s_prev_redline) {
        s_prev_redline = profile->rpm_redline;
        lv_arc_set_range(s_val_arc, 0, profile->rpm_max);
        int start = (int)(GAUGE_SWEEP_DEG * profile->rpm_redline / profile->rpm_max);
        if (start > (int)GAUGE_SWEEP_DEG) {
            start = (int)GAUGE_SWEEP_DEG;
        }
        lv_arc_set_bg_angles(s_redline_arc, (uint16_t)start, (uint16_t)GAUGE_SWEEP_DEG);
    }

    if ((int8_t)rpm_mode != s_prev_rpm_mode || (int8_t)metric != s_prev_metric) {
        s_prev_rpm_mode = (int8_t)rpm_mode;
        s_prev_metric = (int8_t)metric;
        lv_label_set_text_static(s_primary_unit, rpm_mode ? "rpm" : vehicle_data_speed_unit(metric));
        lv_label_set_text_static(s_secondary_unit, rpm_mode ? vehicle_data_speed_unit(metric) : "rpm");
    }

    /* Same frame, same clock: RPM and Speed advance together. */
    motion_update(&s_rpm_motion, snap->rpm, snap->rpm_ts, now);
    motion_update(&s_spd_motion, snap->speed, snap->speed_ts, now);
    /* rpm_ts == 0: simulator / no timestamp yet. Otherwise the last real
     * sample must be recent — after a reconnect old values are not "live". */
    bool live = connected && !motion_stale(&s_rpm_motion, now) &&
                (snap->rpm_ts == 0 || now - snap->rpm_ts <= UI_DATA_STALE_MS);

    float rpm = live ? s_rpm_motion.disp : 0.0f;
    float spd = live ? vehicle_data_convert_speed(s_spd_motion.disp, metric) : 0.0f;

    lv_arc_set_value(s_val_arc, (int16_t)rpm);

    int zone = rpm >= profile->rpm_redline ? 2 : (rpm >= profile->rpm_redline - 1000 ? 1 : 0);
    if (zone != s_prev_zone) {
        s_prev_zone = (int8_t)zone;
        lv_color_t c = zone == 2 ? t->crit : (zone == 1 ? t->secondary : t->primary);
        lv_obj_set_style_arc_color(s_val_arc, c, LV_PART_INDICATOR);
    }

    char rpm_txt[12], spd_txt[12];
    if (live) {
        snprintf(rpm_txt, sizeof(rpm_txt), "%d", ((int)(rpm + 5.0f) / 10) * 10);
        snprintf(spd_txt, sizeof(spd_txt), "%d", (int)(spd + 0.5f));
    } else {
        snprintf(rpm_txt, sizeof(rpm_txt), "--");
        snprintf(spd_txt, sizeof(spd_txt), "--");
    }
    digits_set(&s_primary, rpm_mode ? rpm_txt : spd_txt);
    digits_set(&s_secondary, rpm_mode ? spd_txt : rpm_txt);
    digits_set_color(&s_primary, live ? t->text : t->text_dim);
    digits_set_color(&s_secondary, live ? t->primary : t->text_dim);

    update_shift_lights(rpm, profile->rpm_redline, now, t);

    /* Tiles */
    char buf[12];
    const char *tunit = metric ? "\xC2\xB0" "C" : "\xC2\xB0" "F";
    bool have_cool = connected && snap->coolant > -40.0f && snap->coolant != 0.0f;
    if (have_cool) {
        snprintf(buf, sizeof(buf), "%.0f", vehicle_data_convert_temp(snap->coolant, metric));
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    tile_update(0, true, buf, tunit,
                have_cool ? vehicle_data_coolant_level(snap->coolant) : THRESHOLD_OK, t);

    bool oil = vehicle_data_is_pid_supported(0x5C);
    snprintf(buf, sizeof(buf), "%.0f", vehicle_data_convert_temp(snap->oil_temp, metric));
    tile_update(1, oil, buf, tunit, oil_level(snap->oil_temp), t);

    bool have_volt = snap->voltage > 0.1f;
    if (have_volt) {
        snprintf(buf, sizeof(buf), "%.1f", snap->voltage);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    tile_update(2, true, buf, "V",
                have_volt ? vehicle_data_voltage_level(snap->voltage) : THRESHOLD_OK, t);

    if (connected) {
        alert_check(snap->coolant);
    }
}
