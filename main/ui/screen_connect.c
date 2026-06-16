#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include "ble_obd.h"
#include <stdio.h>

static lv_obj_t *s_status_label;
static lv_obj_t *s_device_label;
static lv_obj_t *s_scan_arc;
static lv_obj_t *s_scan_btn;

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
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 6, 0);

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
    lv_obj_set_style_arc_width(s_scan_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_scan_arc, 10, LV_PART_INDICATOR);

    lv_obj_t *info = lv_obj_create(body);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    theme_apply_card(info);
    lv_obj_set_style_pad_all(info, 8, 0);
    lv_obj_set_style_pad_row(info, 4, 0);
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

    s_scan_btn = lv_btn_create(parent);
    lv_obj_set_width(s_scan_btn, LV_PCT(100));
    lv_obj_set_height(s_scan_btn, UI_BTN_H);
    lv_obj_add_flag(s_scan_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_scan_btn, LV_ALIGN_BOTTOM_MID, 0, -(UI_STAT_LIFT + UI_DOT_H));
    lv_obj_set_style_bg_color(s_scan_btn, t->surface_hi, 0);
    lv_obj_set_style_border_color(s_scan_btn, t->primary, 0);
    lv_obj_set_style_border_width(s_scan_btn, 1, 0);
    lv_obj_set_style_radius(s_scan_btn, 8, 0);
    lv_obj_add_event_cb(s_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(s_scan_btn);
    lv_label_set_text(btn_label, "Scan");
    lv_obj_set_style_text_font(btn_label, t->font_md, 0);
    lv_obj_set_style_text_color(btn_label, t->primary, 0);
    lv_obj_center(btn_label);
}

void screen_connect_update(void)
{
    vehicle_data_t *vd = vehicle_data_get();
    const ui_theme_t *t = theme_get();

    lv_label_set_text(s_status_label, vd->status_msg);

    if (vd->adapter_name[0]) {
        lv_label_set_text(s_device_label, vd->adapter_name);
    } else {
        lv_label_set_text(s_device_label, "No device");
    }

    static int anim_val = 0;
    if (vd->state == OBD_STATE_SCANNING ||
        vd->state == OBD_STATE_CONNECTING ||
        vd->state == OBD_STATE_ELM_INIT ||
        vd->state == OBD_STATE_PID_DISCOVERY) {
        anim_val = (anim_val + 10) % 270;
        lv_arc_set_value(s_scan_arc, anim_val);
        if (vd->state == OBD_STATE_ELM_INIT || vd->state == OBD_STATE_PID_DISCOVERY) {
            lv_obj_set_style_arc_color(s_scan_arc, t->primary, LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_arc_color(s_scan_arc, t->secondary, LV_PART_INDICATOR);
        }
    } else if (vd->state == OBD_STATE_READY) {
        lv_arc_set_value(s_scan_arc, 270);
        lv_obj_set_style_arc_color(s_scan_arc, t->ok, LV_PART_INDICATOR);
    } else {
        lv_arc_set_value(s_scan_arc, 0);
        lv_obj_set_style_arc_color(s_scan_arc, t->primary, LV_PART_INDICATOR);
    }
}
