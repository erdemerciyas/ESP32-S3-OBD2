#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Live Data — round-panel layout.
 * Outer ring: one arc segment per PID the ECU reports as supported (Mode 01
 * bitmap), sweeping 300° with the 60° gap at 6 o'clock left free for the dot
 * navigation. A name/value chip sits just inside each segment. Centre: the
 * selected metric as a hero gauge with session min/max.
 * Tap the centre → next metric, tap a chip → jump to it, long-press the
 * centre → reset min/max. Unsupported PIDs are hidden and the ring re-divides. */

#define LD_RING_W       6
#define LD_RING_SEL_W   10
#define LD_SEG_GAP      3
#define LD_CHIP_W       66
#define LD_CHIP_H       34
#define LD_CHIP_GAP     6       /* ring inner edge → chip box */
#define LD_HERO_D       244
#define LD_HERO_W       12
#define LD_LENS_D       200
#define LD_MINMAX_X     40
#define LD_MINMAX_Y     112
#define LD_RANGE        1000
#define LD_VIS_MS       500     /* re-check the supported bitmap this often */

typedef enum { U_PCT, U_KPA, U_TEMP, U_DEG, U_GS, U_VOLT } unit_kind_t;

typedef struct {
    uint8_t     pid;
    const char *name;
    unit_kind_t unit;
    float       lo, hi;         /* arc range (metric units); lo == -hi → centre-zero */
    uint8_t     decimals;
} ld_metric_t;

static const ld_metric_t s_metrics[] = {
    { 0x11, "Throttle", U_PCT,    0.0f, 100.0f, 0 },
    { 0x04, "Load",     U_PCT,    0.0f, 100.0f, 0 },
    { 0x0B, "MAP",      U_KPA,    0.0f, 110.0f, 0 },
    { 0x0F, "Intake",   U_TEMP, -20.0f,  80.0f, 0 },
    { 0x5C, "Oil",      U_TEMP,  40.0f, 150.0f, 0 },
    { 0x0E, "Timing",   U_DEG,  -10.0f,  50.0f, 1 },
    { 0x10, "MAF",      U_GS,     0.0f, 100.0f, 1 },
    { 0x06, "STFT",     U_PCT,  -25.0f,  25.0f, 1 },
    { 0x07, "LTFT",     U_PCT,  -25.0f,  25.0f, 1 },
    { 0x14, "O2 S1",    U_VOLT,   0.0f,   1.0f, 2 },
    { 0x15, "O2 S2",    U_VOLT,   0.0f,   1.0f, 2 },
    { 0x2F, "Fuel",     U_PCT,    0.0f, 100.0f, 0 },
};
#define METRIC_COUNT ((int)(sizeof(s_metrics) / sizeof(s_metrics[0])))

static lv_obj_t *s_seg[METRIC_COUNT];
static lv_obj_t *s_chip[METRIC_COUNT];
static lv_obj_t *s_chip_name[METRIC_COUNT];
static lv_obj_t *s_chip_val[METRIC_COUNT];
static char      s_prev_chip[METRIC_COUNT][16];
static int8_t    s_prev_lvl[METRIC_COUNT];
static int8_t    s_vis[METRIC_COUNT];
static float     s_min[METRIC_COUNT];
static float     s_max[METRIC_COUNT];
static bool      s_has_mm[METRIC_COUNT];

static lv_obj_t   *s_hero_arc;
static lv_obj_t   *s_hero_name;
static lv_obj_t   *s_hero_val;
static lv_obj_t   *s_hero_unit;
static lv_obj_t   *s_min_lbl;
static lv_obj_t   *s_max_lbl;
static char        s_prev_hero[12];
static char        s_prev_min[20];
static char        s_prev_max[20];
static const char *s_prev_hero_unit;
static int8_t      s_prev_hero_lvl;

static int      s_sel = -1;
static bool     s_was_ready;
static uint32_t s_vis_checked;

static float metric_value(uint8_t pid, const vehicle_data_snapshot_t *s)
{
    switch (pid) {
    case 0x11: return s->throttle;
    case 0x04: return s->load;
    case 0x0B: return s->map;
    case 0x0F: return s->iat;
    case 0x5C: return s->oil_temp;
    case 0x0E: return s->timing;
    case 0x10: return s->maf;
    case 0x06: return s->fuel_trim_st;
    case 0x07: return s->fuel_trim_lt;
    case 0x14: return s->o2_voltage;
    case 0x15: return s->o2_b1s2;
    case 0x2F: return s->fuel_level;
    default:   return 0.0f;
    }
}

static const char *unit_text(unit_kind_t u, bool metric)
{
    switch (u) {
    case U_PCT:  return "%";
    case U_KPA:  return "kPa";
    case U_TEMP: return metric ? "\xC2\xB0" "C" : "\xC2\xB0" "F";
    case U_DEG:  return "\xC2\xB0";
    case U_GS:   return "g/s";
    case U_VOLT: return "V";
    default:     return "";
    }
}

/* Metric units in; only PIDs with a meaningful danger zone are graded. */
static threshold_level_t metric_level(uint8_t pid, float v)
{
    switch (pid) {
    case 0x5C:                          /* oil °C */
        if (v > 135.0f) return THRESHOLD_CRIT;
        if (v > 120.0f) return THRESHOLD_WARN;
        break;
    case 0x0F:                          /* intake °C (heat soak) */
        if (v > 70.0f) return THRESHOLD_CRIT;
        if (v > 55.0f) return THRESHOLD_WARN;
        break;
    case 0x06:
    case 0x07:                          /* fuel trims % */
        if (fabsf(v) > 20.0f) return THRESHOLD_CRIT;
        if (fabsf(v) > 10.0f) return THRESHOLD_WARN;
        break;
    case 0x2F:                          /* fuel level % */
        if (v < 8.0f)  return THRESHOLD_CRIT;
        if (v < 15.0f) return THRESHOLD_WARN;
        break;
    default:
        break;
    }
    return THRESHOLD_OK;
}

static lv_color_t tone_color(threshold_level_t lvl)
{
    const ui_theme_t *t = theme_get();
    return lvl == THRESHOLD_OK ? t->primary : theme_threshold_color(lvl);
}

static bool is_centre_zero(const ld_metric_t *m)
{
    return m->lo == -m->hi;
}

static int16_t arc_empty(const ld_metric_t *m)
{
    return is_centre_zero(m) ? LD_RANGE / 2 : 0;
}

static int16_t arc_value(const ld_metric_t *m, float v)
{
    float k = (v - m->lo) / (m->hi - m->lo);
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    return (int16_t)lroundf(k * LD_RANGE);
}

/* v in metric units; formatted in the user's units, no "-0.0". */
static void fmt_number(char *buf, size_t n, const ld_metric_t *m, float v, bool metric)
{
    static const float half_step[] = { 0.5f, 0.05f, 0.005f };
    float shown = (m->unit == U_TEMP) ? vehicle_data_convert_temp(v, metric) : v;
    if (fabsf(shown) < half_step[m->decimals]) {
        shown = 0.0f;
    }
    if (snprintf(buf, n, "%.*f", m->decimals, shown) >= (int)n) {
        snprintf(buf, n, "--");     /* out-of-range garbage, never in practice */
    }
}

static lv_obj_t *make_arc(lv_obj_t *parent, lv_coord_t d, lv_coord_t w)
{
    lv_obj_t *arc = theme_create_arc(parent, d, w);
    lv_arc_set_range(arc, 0, LD_RANGE);
    lv_arc_set_value(arc, 0);
    return arc;
}

static void style_segment(int i, bool sel)
{
    const ui_theme_t *t = theme_get();
    lv_coord_t w = sel ? LD_RING_SEL_W : LD_RING_W;

    lv_obj_set_style_arc_width(s_seg[i], w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_seg[i], w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_seg[i], sel ? t->border : t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_seg[i], sel ? LV_OPA_COVER : LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_chip[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_chip_name[i], sel ? t->primary : t->text_dim, 0);
}

static void select_metric(int i)
{
    if (s_sel >= 0) {
        style_segment(s_sel, false);
    }
    s_sel = i;

    /* Force the hero to repaint on the next update. */
    s_prev_hero[0] = '\0';
    s_prev_min[0] = '\0';
    s_prev_max[0] = '\0';
    s_prev_hero_unit = NULL;
    s_prev_hero_lvl = -1;

    if (i < 0) {
        lv_label_set_text_static(s_hero_name, "NO DATA");
        lv_label_set_text_static(s_hero_val, "--");
        lv_label_set_text_static(s_hero_unit, "");
        lv_label_set_text_static(s_min_lbl, "");
        lv_label_set_text_static(s_max_lbl, "");
        lv_arc_set_mode(s_hero_arc, LV_ARC_MODE_NORMAL);
        lv_arc_set_value(s_hero_arc, 0);
        return;
    }

    style_segment(i, true);

    char up[16];
    const char *src = s_metrics[i].name;
    size_t k = 0;
    for (; src[k] && k < sizeof(up) - 1; k++) {
        up[k] = (char)toupper((unsigned char)src[k]);
    }
    up[k] = '\0';
    lv_label_set_text(s_hero_name, up);

    lv_arc_set_mode(s_hero_arc, is_centre_zero(&s_metrics[i]) ? LV_ARC_MODE_SYMMETRICAL
                                                              : LV_ARC_MODE_NORMAL);
}

static void select_next(void)
{
    for (int step = 1; step <= METRIC_COUNT; step++) {
        int i = (s_sel + step + METRIC_COUNT) % METRIC_COUNT;
        if (s_vis[i] > 0) {
            if (i != s_sel) {
                select_metric(i);
            }
            return;
        }
    }
}

static void lens_click_cb(lv_event_t *e)
{
    (void)e;
    select_next();
}

static void lens_long_press_cb(lv_event_t *e)
{
    (void)e;
    if (s_sel >= 0) {
        s_has_mm[s_sel] = false;
    }
}

static void chip_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i != s_sel) {
        select_metric(i);
    }
}

/* Divide the 300° sweep among the visible PIDs and park each chip just inside
 * its segment's midpoint, pushed in by the box's radial half-extent so it
 * never touches the ring regardless of angle. */
static void layout_ring(void)
{
    int n = 0;
    for (int i = 0; i < METRIC_COUNT; i++) {
        n += s_vis[i] > 0;
    }
    if (n == 0) {
        return;
    }

    float seg = (UI_RING_SWEEP - (n - 1) * LD_SEG_GAP) / (float)n;
    int j = 0;
    for (int i = 0; i < METRIC_COUNT; i++) {
        if (s_vis[i] <= 0) {
            continue;
        }
        float a0 = j * (seg + LD_SEG_GAP);
        lv_arc_set_bg_angles(s_seg[i], (uint16_t)lroundf(a0), (uint16_t)lroundf(a0 + seg));

        theme_place_in_ring(s_chip[i], LD_CHIP_W, LD_CHIP_H, UI_RING_ROT + a0 + seg / 2.0f,
                            UI_RING_D / 2 - LD_RING_SEL_W - LD_CHIP_GAP);
        j++;
    }
}

/* Returns true when the visible set changed. */
static bool update_visibility(void)
{
    const vehicle_data_t *vd = vehicle_data_get();
    bool known = vd->supported_pids[0] || vd->supported_pids[1] ||
                 vd->supported_pids[2] || vd->supported_pids[3];
    bool changed = false;

    for (int i = 0; i < METRIC_COUNT; i++) {
        int8_t vis = (int8_t)(!known || vehicle_data_is_pid_supported(s_metrics[i].pid));
        if (vis == s_vis[i]) {
            continue;
        }
        s_vis[i] = vis;
        changed = true;
        if (vis) {
            lv_obj_clear_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (changed) {
        layout_ring();
        if (s_sel < 0 || s_vis[s_sel] <= 0) {
            int first = -1;
            for (int i = 0; i < METRIC_COUNT && first < 0; i++) {
                if (s_vis[i] > 0) {
                    first = i;
                }
            }
            select_metric(first);
        }
    }
    return changed;
}

static void create_chip(lv_obj_t *parent, int i, const ui_theme_t *t)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, LD_CHIP_W, LD_CHIP_H);
    lv_obj_set_style_radius(chip, 8, 0);
    lv_obj_set_style_bg_color(chip, t->surface_hi, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_ext_click_area(chip, 6);
    lv_obj_add_event_cb(chip, chip_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    s_chip_name[i] = lv_label_create(chip);
    lv_label_set_text_static(s_chip_name[i], s_metrics[i].name);
    lv_label_set_long_mode(s_chip_name[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_chip_name[i], LV_PCT(100));
    lv_obj_set_style_text_align(s_chip_name[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_chip_name[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_chip_name[i], t->text_dim, 0);
    lv_obj_align(s_chip_name[i], LV_ALIGN_TOP_MID, 0, 0);

    s_chip_val[i] = lv_label_create(chip);
    lv_label_set_text_static(s_chip_val[i], "--");
    lv_label_set_long_mode(s_chip_val[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_chip_val[i], LV_PCT(100));
    lv_obj_set_style_text_align(s_chip_val[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_chip_val[i], t->font_sm, 0);
    lv_obj_set_style_text_color(s_chip_val[i], t->text, 0);
    lv_obj_align(s_chip_val[i], LV_ALIGN_BOTTOM_MID, 0, 0);

    s_chip[i] = chip;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text_static(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

void screen_grid_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *root = theme_create_root(parent);

    /* Outer ring: square segment ends read as discrete gauges. */
    for (int i = 0; i < METRIC_COUNT; i++) {
        s_seg[i] = make_arc(root, UI_RING_D, LD_RING_W);
        lv_arc_set_rotation(s_seg[i], UI_RING_ROT);
        lv_obj_set_style_arc_rounded(s_seg[i], false, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(s_seg[i], false, LV_PART_INDICATOR);
        if (is_centre_zero(&s_metrics[i])) {
            lv_arc_set_mode(s_seg[i], LV_ARC_MODE_SYMMETRICAL);
        }
        lv_arc_set_value(s_seg[i], arc_empty(&s_metrics[i]));
        lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);

        create_chip(root, i, t);
        lv_obj_add_flag(s_chip[i], LV_OBJ_FLAG_HIDDEN);

        s_vis[i] = 0;
        s_prev_lvl[i] = -1;
    }

    /* Hero gauge: same 270° geometry as the dashboard, tip dot as the marker. */
    s_hero_arc = make_arc(root, LD_HERO_D, LD_HERO_W);
    lv_arc_set_rotation(s_hero_arc, 135);
    lv_arc_set_bg_angles(s_hero_arc, 0, 270);
    lv_obj_set_style_bg_color(s_hero_arc, t->text, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_hero_arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(s_hero_arc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_hero_arc, 3, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_hero_arc, 0, LV_PART_KNOB);

    lv_obj_t *lens = lv_obj_create(root);
    theme_apply_lens(lens, LD_LENS_D);
    lv_obj_add_event_cb(lens, lens_click_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(lens, lens_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    s_hero_name = make_label(lens, t->font_md, t->text_dim);
    lv_obj_set_style_text_letter_space(s_hero_name, 2, 0);
    lv_obj_align(s_hero_name, LV_ALIGN_CENTER, 0, -56);

    s_hero_val = make_label(lens, t->font_xxl, t->text);
    lv_obj_align(s_hero_val, LV_ALIGN_CENTER, 0, -2);

    s_hero_unit = make_label(lens, t->font_md, t->text_dim);
    lv_obj_align(s_hero_unit, LV_ALIGN_CENTER, 0, 38);

    /* Session min / max in the hero arc's bottom opening. */
    s_min_lbl = make_label(root, t->font_sm, t->text_dim);
    lv_obj_align(s_min_lbl, LV_ALIGN_CENTER, -LD_MINMAX_X, LD_MINMAX_Y);
    s_max_lbl = make_label(root, t->font_sm, t->text_dim);
    lv_obj_align(s_max_lbl, LV_ALIGN_CENTER, LD_MINMAX_X, LD_MINMAX_Y);

    s_sel = -1;
    select_metric(-1);
    update_visibility();
}

static void set_label_cached(lv_obj_t *l, char *prev, size_t n, const char *txt)
{
    if (strcmp(txt, prev) != 0) {
        snprintf(prev, n, "%s", txt);
        lv_label_set_text(l, txt);
    }
}

static void update_hero(bool ready, bool metric, float v)
{
    const ld_metric_t *m = &s_metrics[s_sel];
    char num[12];
    char buf[20];

    if (ready) {
        fmt_number(num, sizeof(num), m, v, metric);
    } else {
        snprintf(num, sizeof(num), "--");
    }
    set_label_cached(s_hero_val, s_prev_hero, sizeof(s_prev_hero), num);

    const char *u = unit_text(m->unit, metric);
    if (u != s_prev_hero_unit) {
        s_prev_hero_unit = u;
        lv_label_set_text_static(s_hero_unit, u);
    }

    lv_arc_set_value(s_hero_arc, ready ? arc_value(m, v) : arc_empty(m));

    int8_t lvl = (int8_t)(ready ? metric_level(m->pid, v) : THRESHOLD_OK);
    if (lvl != s_prev_hero_lvl) {
        s_prev_hero_lvl = lvl;
        lv_obj_set_style_arc_color(s_hero_arc, tone_color((threshold_level_t)lvl),
                                   LV_PART_INDICATOR);
    }

    if (s_has_mm[s_sel]) {
        fmt_number(num, sizeof(num), m, s_min[s_sel], metric);
        snprintf(buf, sizeof(buf), LV_SYMBOL_DOWN " %s", num);
        set_label_cached(s_min_lbl, s_prev_min, sizeof(s_prev_min), buf);
        fmt_number(num, sizeof(num), m, s_max[s_sel], metric);
        snprintf(buf, sizeof(buf), LV_SYMBOL_UP " %s", num);
        set_label_cached(s_max_lbl, s_prev_max, sizeof(s_prev_max), buf);
    } else {
        set_label_cached(s_min_lbl, s_prev_min, sizeof(s_prev_min), "");
        set_label_cached(s_max_lbl, s_prev_max, sizeof(s_prev_max), "");
    }
}

void screen_grid_update(const vehicle_data_snapshot_t *snap)
{
    const ui_theme_t *t = theme_get();
    uint32_t now = lv_tick_get();
    if (now - s_vis_checked >= LD_VIS_MS) {
        s_vis_checked = now;
        update_visibility();
    }

    bool ready = snap->state == OBD_STATE_READY;
    if (ready && !s_was_ready) {
        memset(s_has_mm, 0, sizeof(s_has_mm));
    }
    s_was_ready = ready;

    bool metric = snap->metric_units;
    float sel_v = 0.0f;
    char num[12];
    char buf[16];

    for (int i = 0; i < METRIC_COUNT; i++) {
        if (s_vis[i] <= 0) {
            continue;
        }
        const ld_metric_t *m = &s_metrics[i];
        float v = metric_value(m->pid, snap);
        if (i == s_sel) {
            sel_v = v;
        }

        if (ready) {
            if (!s_has_mm[i]) {
                s_min[i] = s_max[i] = v;
                s_has_mm[i] = true;
            } else {
                if (v < s_min[i]) s_min[i] = v;
                if (v > s_max[i]) s_max[i] = v;
            }
            fmt_number(num, sizeof(num), m, v, metric);
            snprintf(buf, sizeof(buf), "%s%s", num, unit_text(m->unit, metric));
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        set_label_cached(s_chip_val[i], s_prev_chip[i], sizeof(s_prev_chip[i]), buf);

        int8_t lvl = (int8_t)(ready ? metric_level(m->pid, v) : THRESHOLD_OK);
        if (lvl != s_prev_lvl[i]) {
            s_prev_lvl[i] = lvl;
            lv_obj_set_style_arc_color(s_seg[i], tone_color((threshold_level_t)lvl),
                                       LV_PART_INDICATOR);
            lv_obj_set_style_text_color(s_chip_val[i], lvl == THRESHOLD_OK ? t->text
                                        : theme_threshold_color((threshold_level_t)lvl), 0);
        }

        lv_arc_set_value(s_seg[i], ready ? arc_value(m, v) : arc_empty(m));
    }

    if (s_sel >= 0) {
        update_hero(ready, metric, sel_v);
    }
}
