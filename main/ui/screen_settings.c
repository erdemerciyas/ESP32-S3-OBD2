#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "ble_obd.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_unit_switch;
static lv_obj_t *s_auto_switch;
static lv_obj_t *s_gauge_switch;
static lv_obj_t *s_profile_dropdown;
static lv_obj_t *s_info_lbl;
static char s_prev_active_profile[VEHICLE_PROFILE_ID_LEN] = "";

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

static void profile_dropdown_cb(lv_event_t *e)
{
    (void)e;
    if (!s_profile_dropdown) {
        return;
    }
    uint16_t sel = lv_dropdown_get_selected(s_profile_dropdown);
    const vehicle_profile_t *profile = vehicle_profile_get_at((int)sel);
    if (profile) {
        vehicle_profile_set_active(profile->profile_id);
    }
}

static lv_obj_t *create_setting_row(lv_obj_t *parent, const char *label,
                                     lv_color_t accent, lv_obj_t **sw_out)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, UI_SETTING_ROW_H);
    theme_apply_setting_row(row, accent);
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
    lv_obj_set_style_bg_color(*sw_out, t->surface_hi, 0);
    lv_obj_set_style_bg_color(*sw_out, accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(*sw_out, t->text, LV_PART_KNOB);
    lv_obj_set_style_bg_color(*sw_out, t->text, LV_PART_KNOB | LV_STATE_CHECKED);

    return row;
}

void screen_settings_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *body = theme_create_flex_col(parent, true);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, UI_SETTING_GAP, 0);

    theme_create_header(body, "Settings");

    lv_obj_t *list = theme_create_flex_col(body, false);
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(list, UI_SETTING_GAP, 0);

    create_setting_row(list, "Metric units", t->primary, &s_unit_switch);
    lv_obj_add_state(s_unit_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_unit_switch, unit_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_setting_row(list, "Auto connect", t->ok, &s_auto_switch);
    lv_obj_add_state(s_auto_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_auto_switch, auto_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_setting_row(list, "Center RPM", t->secondary, &s_gauge_switch);
    lv_obj_add_state(s_gauge_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_gauge_switch, gauge_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Profile selector row */
    lv_obj_t *profile_row = lv_obj_create(list);
    lv_obj_set_width(profile_row, LV_PCT(100));
    lv_obj_set_height(profile_row, UI_SETTING_ROW_H);
    theme_apply_setting_row(profile_row, t->warn);
    lv_obj_set_flex_flow(profile_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(profile_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *profile_lbl = lv_label_create(profile_row);
    lv_label_set_text(profile_lbl, "Profile");
    lv_label_set_long_mode(profile_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(profile_lbl, 1);
    lv_obj_set_style_text_font(profile_lbl, t->font_sm, 0);
    lv_obj_set_style_text_color(profile_lbl, t->text, 0);

    s_profile_dropdown = lv_dropdown_create(profile_row);
    lv_obj_set_size(s_profile_dropdown, 160, 32);
    lv_obj_set_style_text_font(s_profile_dropdown, t->font_sm, 0);
    lv_obj_set_style_text_color(s_profile_dropdown, t->text, 0);
    lv_obj_set_style_bg_color(s_profile_dropdown, t->surface_hi, 0);
    lv_obj_set_style_border_color(s_profile_dropdown, t->border, 0);
    lv_obj_set_style_border_width(s_profile_dropdown, 1, 0);

    /* Build options string from loaded profiles. */
    char options[256] = {0};
    int count = vehicle_profile_get_count();
    for (int i = 0; i < count; i++) {
        const vehicle_profile_t *profile = vehicle_profile_get_at(i);
        if (!profile) {
            continue;
        }
        if (i > 0) {
            strncat(options, "\n", sizeof(options) - strlen(options) - 1);
        }
        strncat(options, profile->display_name, sizeof(options) - strlen(options) - 1);
    }
    lv_dropdown_set_options(s_profile_dropdown, options);
    lv_dropdown_set_selected(s_profile_dropdown, 0);
    lv_obj_add_event_cb(s_profile_dropdown, profile_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Dark dropdown list styling */
    lv_obj_t *dd_list = lv_dropdown_get_list(s_profile_dropdown);
    if (dd_list) {
        lv_obj_set_style_bg_color(dd_list, t->surface, 0);
        lv_obj_set_style_text_color(dd_list, t->text, 0);
        lv_obj_set_style_border_color(dd_list, t->border, 0);
        lv_obj_set_style_border_width(dd_list, 1, 0);
    }

    lv_obj_t *info = lv_obj_create(body);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    theme_apply_card(info);
    theme_apply_card_topline(info, t->primary);
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

void screen_settings_update(const vehicle_data_snapshot_t *snap)
{
    const vehicle_profile_t *profile = vehicle_profile_get();
    char buf[256];
    const char *adapter_line = snap->adapter_name[0] ? snap->adapter_name : "No adapter";

    /* Sync dropdown selection with active profile only when it changes. */
    if (s_profile_dropdown && strcmp(s_prev_active_profile, profile->profile_id) != 0) {
        strncpy(s_prev_active_profile, profile->profile_id, sizeof(s_prev_active_profile) - 1);
        s_prev_active_profile[sizeof(s_prev_active_profile) - 1] = '\0';
        int count = vehicle_profile_get_count();
        for (int i = 0; i < count; i++) {
            const vehicle_profile_t *p = vehicle_profile_get_at(i);
            if (p && strcmp(p->profile_id, profile->profile_id) == 0) {
                lv_dropdown_set_selected(s_profile_dropdown, (uint16_t)i);
                break;
            }
        }
    }

    if (snap->voltage > 0.1f) {
        snprintf(buf, sizeof(buf),
                 "Profile: %s\nProtocol: %s\nAdapter: %s\n%s\nStatus: %s\nVoltage: %.1fV",
                 profile->display_name,
                 profile->protocol,
                 adapter_line,
                 snap->adapter_addr[0] ? snap->adapter_addr : "",
                 state_label(snap->state),
                 snap->voltage);
    } else {
        snprintf(buf, sizeof(buf),
                 "Profile: %s\nProtocol: %s\nAdapter: %s\n%s\nStatus: %s\nVoltage: --",
                 profile->display_name,
                 profile->protocol,
                 adapter_line,
                 snap->adapter_addr[0] ? snap->adapter_addr : "",
                 state_label(snap->state));
    }
    lv_label_set_text(s_info_lbl, buf);
}
