#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include "obd_link.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Connection — round-panel layout.
 * Outer ring: the four link stages (scan → BLE link → ELM init → PID
 * discovery) as segments that fill as each completes; the active one pulses,
 * a failed one turns red. Centre: radar ring with a rotating sweep while
 * busy, and a BLE/WiFi core (transport picked in Settings) that starts a
 * scan when tapped. Status and adapter name sit below the core. */

#define CN_STAGES       4
#define CN_SEG_GAP      4
#define CN_RING_W       8
#define CN_LBL_W        52
#define CN_LBL_H        16
#define CN_LBL_GAP      8
#define CN_CORE_Y       (-24)
#define CN_CORE_D       128
#define CN_RADAR_D      220
#define CN_SWEEP_W      3
#define CN_SWEEP_SPAN   70      /* degrees */
#define CN_SWEEP_MS     1600
#define CN_PULSE_MS     1200
#define CN_STATUS_Y     112
#define CN_DEVICE_Y     138

static const char *const s_stage_name[CN_STAGES] = { "SCAN", "LINK", "ELM", "PIDS" };

static lv_obj_t *s_seg[CN_STAGES];
static lv_obj_t *s_seg_lbl[CN_STAGES];
static lv_obj_t *s_radar;
static lv_obj_t *s_sweep;
static lv_obj_t *s_core;
static lv_obj_t *s_core_icon;
static lv_obj_t *s_core_cap;
static lv_obj_t *s_status_label;
static lv_obj_t *s_device_label;

/* Previous state cache to avoid redundant LVGL updates */
static int  s_prev_state = -1;
static int  s_prev_link = -1;
static int  s_last_stage;       /* last busy stage, marks the failed segment */
static char s_prev_status[64] = "";
static char s_prev_device[32] = "";

/* -1 = idle/error/ready, else 0..3 */
static int stage_of(obd_state_t st)
{
    switch (st) {
    case OBD_STATE_SCANNING:      return 0;
    case OBD_STATE_CONNECTING:    return 1;
    case OBD_STATE_ELM_INIT:      return 2;
    case OBD_STATE_PID_DISCOVERY: return 3;
    default:                      return -1;
    }
}

static void core_click_cb(lv_event_t *e)
{
    (void)e;
    obd_link_rescan();
}

static const char *link_icon(obd_link_type_t type)
{
    return type == OBD_LINK_WIFI ? LV_SYMBOL_WIFI : LV_SYMBOL_BLUETOOTH;
}

void screen_connect_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();
    lv_obj_t *root = theme_create_root(parent);

    /* Stage ring + labels */
    float seg = (UI_RING_SWEEP - (CN_STAGES - 1) * CN_SEG_GAP) / (float)CN_STAGES;
    for (int i = 0; i < CN_STAGES; i++) {
        float a0 = i * (seg + CN_SEG_GAP);
        s_seg[i] = theme_create_arc(root, UI_RING_D, CN_RING_W);
        lv_arc_set_rotation(s_seg[i], UI_RING_ROT);
        lv_arc_set_bg_angles(s_seg[i], (uint16_t)lroundf(a0), (uint16_t)lroundf(a0 + seg));
        lv_arc_set_range(s_seg[i], 0, 100);
        lv_arc_set_value(s_seg[i], 0);
        lv_obj_set_style_arc_rounded(s_seg[i], false, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(s_seg[i], false, LV_PART_INDICATOR);

        s_seg_lbl[i] = lv_label_create(root);
        lv_label_set_text_static(s_seg_lbl[i], s_stage_name[i]);
        lv_obj_set_size(s_seg_lbl[i], CN_LBL_W, CN_LBL_H);
        lv_obj_set_style_text_align(s_seg_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s_seg_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_letter_space(s_seg_lbl[i], 1, 0);
        lv_obj_set_style_text_color(s_seg_lbl[i], t->text_dim, 0);
        theme_place_in_ring(s_seg_lbl[i], CN_LBL_W, CN_LBL_H, UI_RING_ROT + a0 + seg / 2.0f,
                            UI_RING_D / 2 - CN_RING_W - CN_LBL_GAP);
    }

    /* Radar ring + sweep */
    s_radar = lv_obj_create(root);
    lv_obj_remove_style_all(s_radar);
    lv_obj_set_size(s_radar, CN_RADAR_D, CN_RADAR_D);
    lv_obj_align(s_radar, LV_ALIGN_CENTER, 0, CN_CORE_Y);
    lv_obj_set_style_radius(s_radar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(s_radar, t->border, 0);
    lv_obj_set_style_border_width(s_radar, 1, 0);
    lv_obj_clear_flag(s_radar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_sweep = theme_create_arc(root, CN_RADAR_D, CN_SWEEP_W);
    lv_obj_align(s_sweep, LV_ALIGN_CENTER, 0, CN_CORE_Y);
    lv_arc_set_bg_angles(s_sweep, 0, 360);
    lv_arc_set_angles(s_sweep, 0, CN_SWEEP_SPAN);
    lv_obj_set_style_arc_opa(s_sweep, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_sweep, LV_OBJ_FLAG_HIDDEN);

    /* Core — tap to scan */
    s_core = lv_obj_create(root);
    theme_apply_lens(s_core, CN_CORE_D);
    lv_obj_align(s_core, LV_ALIGN_CENTER, 0, CN_CORE_Y);
    lv_obj_set_style_border_width(s_core, 2, 0);
    lv_obj_set_style_border_opa(s_core, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_core, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_core, core_click_cb, LV_EVENT_CLICKED, NULL);

    s_core_icon = lv_label_create(s_core);
    s_prev_link = (int)obd_link_get_type();
    lv_label_set_text_static(s_core_icon, link_icon(obd_link_get_type()));
    lv_obj_set_style_text_font(s_core_icon, t->font_lg, 0);
    lv_obj_align(s_core_icon, LV_ALIGN_CENTER, 0, -14);

    s_core_cap = lv_label_create(s_core);
    lv_label_set_text_static(s_core_cap, "");
    lv_obj_set_style_text_font(s_core_cap, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(s_core_cap, 1, 0);
    lv_obj_align(s_core_cap, LV_ALIGN_CENTER, 0, 22);

    /* Status + adapter */
    s_status_label = lv_label_create(root);
    lv_label_set_text_static(s_status_label, "");
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status_label, 300);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_status_label, t->font_md, 0);
    lv_obj_set_style_text_color(s_status_label, t->text, 0);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, CN_STATUS_Y);

    s_device_label = lv_label_create(root);
    lv_label_set_text_static(s_device_label, "");
    lv_label_set_long_mode(s_device_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_device_label, 260);
    lv_obj_set_style_text_align(s_device_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_device_label, t->font_sm, 0);
    lv_obj_set_style_text_color(s_device_label, t->secondary, 0);
    lv_obj_align(s_device_label, LV_ALIGN_CENTER, 0, CN_DEVICE_Y);
}

/* Restyle everything that depends only on the state (runs on change). */
static void apply_state(obd_state_t st)
{
    const ui_theme_t *t = theme_get();
    int stage = stage_of(st);
    lv_color_t core_c;
    const char *cap;

    if (stage >= 0) {
        s_last_stage = stage;
    }

    switch (st) {
    case OBD_STATE_READY: core_c = t->ok;      cap = "CONNECTED";   break;
    case OBD_STATE_ERROR: core_c = t->crit;    cap = "TAP TO RETRY"; break;
    case OBD_STATE_SCANNING:
    case OBD_STATE_CONNECTING:
    case OBD_STATE_ELM_INIT:
    case OBD_STATE_PID_DISCOVERY:
                          core_c = t->primary; cap = "LINKING";     break;
    default:              core_c = t->text_dim; cap = "TAP TO SCAN"; break;
    }
    lv_obj_set_style_border_color(s_core, core_c, 0);
    lv_obj_set_style_text_color(s_core_icon, core_c, 0);
    lv_obj_set_style_text_color(s_core_cap, core_c, 0);
    lv_label_set_text_static(s_core_cap, cap);
    lv_obj_set_style_text_color(s_status_label, st == OBD_STATE_READY ? t->ok
                                : st == OBD_STATE_ERROR ? t->crit : t->text, 0);

    if (stage >= 0) {
        lv_obj_clear_flag(s_sweep, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_sweep, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < CN_STAGES; i++) {
        bool done, active = false, failed = false;
        if (st == OBD_STATE_READY) {
            done = true;
        } else if (st == OBD_STATE_ERROR) {
            done = i < s_last_stage;
            failed = i == s_last_stage;
        } else if (stage >= 0) {
            done = i < stage;
            active = i == stage;
        } else {
            done = false;
        }

        lv_color_t c = failed ? t->crit : done ? t->ok : t->primary;
        lv_obj_set_style_arc_color(s_seg[i], c, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(s_seg_lbl[i], (done || active || failed) ? c : t->text_dim, 0);
        if (!active) {
            lv_arc_set_value(s_seg[i], (done || failed) ? 100 : 0);
        }
    }
}

void screen_connect_update(const vehicle_data_snapshot_t *snap)
{
    if (strcmp(snap->status_msg, s_prev_status) != 0) {
        strncpy(s_prev_status, snap->status_msg, sizeof(s_prev_status) - 1);
        s_prev_status[sizeof(s_prev_status) - 1] = '\0';
        lv_label_set_text(s_status_label, snap->status_msg);
    }

    const char *device_text = snap->adapter_name[0] ? snap->adapter_name
                            : obd_link_get_type() == OBD_LINK_WIFI ? "WiFi adapter"
                                                                   : "BLE adapter";
    if (strcmp(device_text, s_prev_device) != 0) {
        strncpy(s_prev_device, device_text, sizeof(s_prev_device) - 1);
        s_prev_device[sizeof(s_prev_device) - 1] = '\0';
        lv_label_set_text(s_device_label, device_text);
    }

    if ((int)obd_link_get_type() != s_prev_link) {
        s_prev_link = (int)obd_link_get_type();
        lv_label_set_text_static(s_core_icon, link_icon(obd_link_get_type()));
    }

    if ((int)snap->state != s_prev_state) {
        s_prev_state = (int)snap->state;
        apply_state(snap->state);
    }

    int stage = stage_of(snap->state);
    if (stage < 0) {
        return;
    }

    /* Busy: rotate the radar sweep and pulse the active stage. Both only
     * invalidate the swept angle span. */
    uint32_t now = lv_tick_get();
    uint16_t a = (uint16_t)((now % CN_SWEEP_MS) * 360U / CN_SWEEP_MS);
    lv_arc_set_angles(s_sweep, a, (uint16_t)((a + CN_SWEEP_SPAN) % 360));

    float ph = (now % CN_PULSE_MS) / (float)CN_PULSE_MS;
    float k = (sinf(ph * 2.0f * (float)M_PI - (float)M_PI / 2.0f) + 1.0f) * 0.5f;
    lv_arc_set_value(s_seg[stage], (int16_t)(15 + k * 85.0f));
}
