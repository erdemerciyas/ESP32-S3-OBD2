#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include "ble_obd.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static lv_obj_t *s_status_label;
static lv_obj_t *s_device_label;
static lv_obj_t *s_scan_arc;
static lv_obj_t *s_scan_btn;

/* Previous state cache to avoid redundant LVGL updates */
static obd_state_t s_prev_state = OBD_STATE_DISCONNECTED;
static int s_prev_anim_val = -1;
static int s_prev_arc_width = -1;
static lv_color_t s_prev_arc_color;
static char s_prev_status[64] = "";
static char s_prev_device[32] = "";
static lv_color_t s_prev_status_color;

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    ble_obd_scan();
}

void screen_connect_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    theme_create_header(parent, "Connection");

    lv_obj_t *body = theme_create_flex_col(parent, true);
    const lv_coord_t body_b = UI_VIEWPORT_SZ - UI_STAT_BOTTOM_OFF - UI_GAP_LG;
    lv_obj_set_width(body, theme_safe_width(UI_PAD_TOP + UI_HEADER_H + UI_GAP_LG, body_b));
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, UI_GAP_LG, 0);

    s_scan_arc = lv_arc_create(body);
    lv_obj_set_size(s_scan_arc, UI_SCAN_ARC_SZ, UI_SCAN_ARC_SZ);
    lv_arc_set_rotation(s_scan_arc, 135);
    lv_arc_set_bg_angles(s_scan_arc, 0, 270);
    lv_arc_set_value(s_scan_arc, 0);
    lv_obj_remove_style(s_scan_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_scan_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_scan_arc, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_style_arc_color(s_scan_arc, t->primary, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_scan_arc, t->arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_scan_arc, UI_SCAN_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_scan_arc, UI_SCAN_ARC_W, LV_PART_INDICATOR);

    lv_obj_t *info = lv_obj_create(body);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    theme_apply_card(info);
    theme_apply_card_topline(info, t->primary);
    lv_obj_set_style_pad_all(info, UI_GAP_MD, 0);
    lv_obj_set_style_pad_row(info, UI_GAP_SM, 0);
    lv_obj_set_style_radius(info, UI_GRID, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_status_label = lv_label_create(info);
    lv_label_set_text(s_status_label, "Ready");
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_status_label, t->font_md, 0);
    lv_obj_set_style_text_color(s_status_label, t->text, 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_status_label, LV_PCT(100));

    s_device_label = lv_label_create(info);
    lv_label_set_text(s_device_label, "No device");
    lv_label_set_long_mode(s_device_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_device_label, t->font_sm, 0);
    lv_obj_set_style_text_color(s_device_label, t->secondary, 0);
    lv_obj_set_style_text_align(s_device_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_device_label, LV_PCT(100));

    s_scan_btn = lv_btn_create(body);
    lv_obj_set_width(s_scan_btn, LV_PCT(100));
    lv_obj_set_height(s_scan_btn, UI_BTN_H);
    lv_obj_set_style_bg_color(s_scan_btn, t->primary, 0);
    lv_obj_set_style_bg_opa(s_scan_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scan_btn, 0, 0);
    lv_obj_set_style_radius(s_scan_btn, UI_GRID, 0);
    lv_obj_set_style_outline_width(s_scan_btn, 0, LV_STATE_ANY);
    lv_obj_set_style_shadow_width(s_scan_btn, 0, LV_STATE_ANY);
    lv_obj_set_style_pad_all(s_scan_btn, 0, LV_STATE_ANY);
    lv_obj_add_event_cb(s_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(s_scan_btn);
    lv_label_set_text(btn_label, "Scan for adapter");
    lv_obj_set_style_text_font(btn_label, t->font_md, 0);
    lv_obj_set_style_text_color(btn_label, t->bg, 0);
    lv_obj_set_style_bg_opa(btn_label, LV_OPA_TRANSP, 0);
    lv_obj_center(btn_label);
}

void screen_connect_update(const vehicle_data_snapshot_t *snap)
{
    const ui_theme_t *t = theme_get();

    if (strcmp(snap->status_msg, s_prev_status) != 0) {
        strncpy(s_prev_status, snap->status_msg, sizeof(s_prev_status) - 1);
        s_prev_status[sizeof(s_prev_status) - 1] = '\0';
        lv_label_set_text(s_status_label, snap->status_msg);
    }

    const char *device_text = snap->adapter_name[0] ? snap->adapter_name : "No device";
    if (strcmp(device_text, s_prev_device) != 0) {
        strncpy(s_prev_device, device_text, sizeof(s_prev_device) - 1);
        s_prev_device[sizeof(s_prev_device) - 1] = '\0';
        lv_label_set_text(s_device_label, device_text);
    }

    lv_color_t status_color = t->text_dim;
    switch (snap->state) {
    case OBD_STATE_READY:         status_color = t->ok;       break;
    case OBD_STATE_ERROR:         status_color = t->crit;     break;
    case OBD_STATE_SCANNING:
    case OBD_STATE_CONNECTING:
    case OBD_STATE_ELM_INIT:
    case OBD_STATE_PID_DISCOVERY: status_color = t->secondary; break;
    default:                      status_color = t->text_dim; break;
    }
    if (status_color.full != s_prev_status_color.full) {
        s_prev_status_color = status_color;
        lv_obj_set_style_text_color(s_status_label, status_color, 0);
    }

    int anim_val = 0;
    int arc_width = 10;
    lv_color_t arc_color = t->primary;

    if (snap->state == OBD_STATE_SCANNING ||
        snap->state == OBD_STATE_CONNECTING ||
        snap->state == OBD_STATE_ELM_INIT ||
        snap->state == OBD_STATE_PID_DISCOVERY) {
        uint32_t now = lv_tick_get();
        float phase = (now % 1200) / 1200.0f;
        float angle = (sinf(phase * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) * 0.5f;
        anim_val = (int)(angle * 270.0f);
        arc_width = 8 + (int)(angle * 6.0f);
        if (snap->state == OBD_STATE_ELM_INIT || snap->state == OBD_STATE_PID_DISCOVERY) {
            arc_color = t->primary;
        } else {
            arc_color = t->secondary;
        }
    } else if (snap->state == OBD_STATE_READY) {
        anim_val = 270;
        arc_width = 10;
        arc_color = t->ok;
    } else {
        anim_val = 0;
        arc_width = 10;
        arc_color = t->primary;
    }

    if (anim_val != s_prev_anim_val) {
        s_prev_anim_val = anim_val;
        lv_arc_set_value(s_scan_arc, anim_val);
    }
    if (arc_width != s_prev_arc_width) {
        s_prev_arc_width = arc_width;
        lv_obj_set_style_arc_width(s_scan_arc, (lv_coord_t)arc_width, LV_PART_INDICATOR);
    }
    if (s_prev_state != snap->state || arc_color.full != s_prev_arc_color.full) {
        s_prev_arc_color = arc_color;
        lv_obj_set_style_arc_color(s_scan_arc, arc_color, LV_PART_INDICATOR);
    }

    s_prev_state = snap->state;
}
