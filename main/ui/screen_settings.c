#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "ble_obd.h"
#include <stdio.h>

static lv_obj_t *s_unit_switch;
static lv_obj_t *s_auto_switch;
static lv_obj_t *s_gauge_switch;
static lv_obj_t *s_info_lbl;

static const char *state_label(obd_state_t state)
{
    switch (state) {
    case OBD_STATE_SCANNING:      return "Scanning";
    case OBD_STATE_CONNECTING:    return "Connecting";
    case OBD_STATE_ELM_INIT:      return "ELM init";
    case OBD_STATE_PID_DISCOVERY: return "PID discovery";
    case OBD_STATE_READY:         return "Ready";
    case OBD_STATE_ERROR:         return "Error";
    default:                      return "Disconnected";
    }
}

static void unit_switch_cb(lv_event_t *e)
{
    (void)e;
    vehicle_data_t *vd = vehicle_data_get();
    vehicle_data_lock();
    vd->metric_units = lv_obj_has_state(s_unit_switch, LV_STATE_CHECKED);
    vehicle_data_unlock();
}

static void auto_switch_cb(lv_event_t *e)
{
    (void)e;
    vehicle_data_t *vd = vehicle_data_get();
    vehicle_data_lock();
    vd->auto_connect = lv_obj_has_state(s_auto_switch, LV_STATE_CHECKED);
    vehicle_data_unlock();
}

static void gauge_switch_cb(lv_event_t *e)
{
    (void)e;
    vehicle_data_t *vd = vehicle_data_get();
    vehicle_data_lock();
    vd->center_gauge_rpm = lv_obj_has_state(s_gauge_switch, LV_STATE_CHECKED);
    vehicle_data_unlock();
}

static lv_obj_t *create_setting_row(lv_obj_t *parent, const char *label, lv_obj_t **sw_out)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, UI_SETTING_ROW_H);
    theme_apply_card(row);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_font(lbl, t->font_sm, 0);
    lv_obj_set_style_text_color(lbl, t->text, 0);

    *sw_out = lv_switch_create(row);
    lv_obj_set_size(*sw_out, 44, 24);

    return row;
}

void screen_settings_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *body = theme_create_flex_col(parent, true);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, UI_SETTING_GAP, 0);

    lv_obj_t *title = lv_label_create(body);
    lv_label_set_text(title, "Settings");
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, t->font_md, 0);
    lv_obj_set_style_text_color(title, t->text, 0);

    lv_obj_t *list = theme_create_flex_col(body, false);
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(list, UI_SETTING_GAP, 0);

    create_setting_row(list, "Metric units", &s_unit_switch);
    lv_obj_add_state(s_unit_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_unit_switch, unit_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_setting_row(list, "Auto connect", &s_auto_switch);
    lv_obj_add_state(s_auto_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_auto_switch, auto_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_setting_row(list, "Center RPM", &s_gauge_switch);
    lv_obj_add_state(s_gauge_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_gauge_switch, gauge_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *info = lv_obj_create(body);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    theme_apply_card(info);
    lv_obj_set_style_pad_ver(info, 6, 0);
    lv_obj_set_style_pad_hor(info, 8, 0);

    lv_obj_t *info_lbl = lv_label_create(info);
    s_info_lbl = info_lbl;
    lv_label_set_text(info_lbl, "ESP32-S3 OBD2\nBLE ELM327");
    lv_obj_set_style_text_font(info_lbl, t->font_sm, 0);
    lv_obj_set_style_text_color(info_lbl, t->text_dim, 0);
    lv_obj_set_style_text_align(info_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(info_lbl, LV_PCT(100));
}

void screen_settings_update(void)
{
    const vehicle_profile_t *profile = vehicle_profile_get();
    vehicle_data_t *vd = vehicle_data_get();
    char buf[256];
    const char *adapter_line = vd->adapter_name[0] ? vd->adapter_name : "No adapter";

    if (vd->voltage > 0.1f) {
        snprintf(buf, sizeof(buf),
                 "Profile: %s\nProtocol: %s\nAdapter: %s\n%s\nStatus: %s\nVoltage: %.1fV\nDTC: %d+%d",
                 profile->display_name,
                 profile->protocol,
                 adapter_line,
                 vd->adapter_addr[0] ? vd->adapter_addr : "",
                 state_label(vd->state),
                 vd->voltage,
                 vd->dtc_count,
                 vd->dtc_pending_count);
    } else {
        snprintf(buf, sizeof(buf),
                 "Profile: %s\nProtocol: %s\nAdapter: %s\n%s\nStatus: %s\nVoltage: --\nDTC: %d+%d",
                 profile->display_name,
                 profile->protocol,
                 adapter_line,
                 vd->adapter_addr[0] ? vd->adapter_addr : "",
                 state_label(vd->state),
                 vd->dtc_count,
                 vd->dtc_pending_count);
    }
    lv_label_set_text(s_info_lbl, buf);
}
