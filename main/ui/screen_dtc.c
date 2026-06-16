#include "ui.h"
#include "theme.h"
#include "vehicle_data.h"
#include "obd_dtc.h"

static lv_obj_t *s_list;
static lv_obj_t *s_count_label;
static lv_obj_t *s_status_label;
static uint32_t s_last_auto_scan_ms;

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    obd_dtc_read_all();
}

static void clear_btn_cb(lv_event_t *e)
{
    (void)e;
    obd_dtc_clear();
}

static void request_scan_if_needed(bool visible, bool connected)
{
    if (!visible || !connected) {
        return;
    }
    uint32_t now = lv_tick_get();
    if (obd_dtc_is_busy()) {
        return;
    }
    if (now - s_last_auto_scan_ms > 8000) {
        s_last_auto_scan_ms = now;
        obd_dtc_read_all();
    }
}

void screen_dtc_create(lv_obj_t *parent)
{
    const ui_theme_t *t = theme_get();

    lv_obj_t *hdr_row = theme_create_flex_row(parent);
    lv_obj_set_height(hdr_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(hdr_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(hdr_row);
    lv_label_set_text(title, "Fault Codes");
    lv_obj_set_style_text_font(title, t->font_md, 0);
    lv_obj_set_style_text_color(title, t->text, 0);

    s_count_label = lv_label_create(hdr_row);
    lv_label_set_text(s_count_label, "0");
    lv_obj_set_style_text_font(s_count_label, t->font_sm, 0);
    lv_obj_set_style_text_color(s_count_label, t->text_dim, 0);

    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_width(s_status_label, LV_PCT(100));
    lv_obj_set_style_text_font(s_status_label, t->font_sm, 0);
    lv_obj_set_style_text_color(s_status_label, t->text_dim, 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);

    s_list = lv_obj_create(parent);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_height(s_list, UI_VIEWPORT_SZ - UI_PAD_TOP - UI_DOT_H - UI_BTN_H - 56);
    theme_apply_card(s_list);
    lv_obj_set_style_pad_all(s_list, 6, 0);
    lv_obj_set_style_pad_row(s_list, 2, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *btn_row = theme_create_flex_row(parent);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, UI_BTN_H - 4);
    lv_obj_add_flag(btn_row, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -(UI_STAT_LIFT + UI_DOT_H));

    lv_obj_t *refresh_btn = lv_btn_create(btn_row);
    lv_obj_set_size(refresh_btn, LV_PCT(48), UI_BTN_H - 8);
    lv_obj_set_style_bg_color(refresh_btn, t->surface_hi, 0);
    lv_obj_set_style_border_color(refresh_btn, t->primary, 0);
    lv_obj_set_style_border_width(refresh_btn, 1, 0);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_add_event_cb(refresh_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(refresh_btn);
    lv_label_set_text(rl, "Scan");
    lv_obj_set_style_text_font(rl, t->font_sm, 0);
    lv_obj_set_style_text_color(rl, t->primary, 0);
    lv_obj_center(rl);

    lv_obj_t *clear_btn = lv_btn_create(btn_row);
    lv_obj_set_size(clear_btn, LV_PCT(48), UI_BTN_H - 8);
    lv_obj_set_style_bg_color(clear_btn, t->surface_hi, 0);
    lv_obj_set_style_border_color(clear_btn, t->crit, 0);
    lv_obj_set_style_border_width(clear_btn, 1, 0);
    lv_obj_set_style_radius(clear_btn, 8, 0);
    lv_obj_add_event_cb(clear_btn, clear_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(clear_btn);
    lv_label_set_text(cl, "Clear");
    lv_obj_set_style_text_font(cl, t->font_sm, 0);
    lv_obj_set_style_text_color(cl, t->crit, 0);
    lv_obj_center(cl);
}

void screen_dtc_update(bool visible)
{
    vehicle_data_t *vd = vehicle_data_get();
    const ui_theme_t *t = theme_get();
    bool connected = vd->state == OBD_STATE_READY;

    request_scan_if_needed(visible, connected);

    lv_label_set_text_fmt(s_count_label, "%d+%d",
                          vd->dtc_count, vd->dtc_pending_count);

    if (vd->dtc_scan_state == DTC_SCAN_BUSY || obd_dtc_is_busy()) {
        lv_label_set_text(s_status_label, vd->dtc_scan_msg[0] ? vd->dtc_scan_msg : "Reading...");
        lv_obj_set_style_text_color(s_status_label, t->primary, 0);
    } else if (vd->dtc_scan_state == DTC_SCAN_ERROR) {
        lv_label_set_text(s_status_label, vd->dtc_scan_msg[0] ? vd->dtc_scan_msg : "Scan failed");
        lv_obj_set_style_text_color(s_status_label, t->crit, 0);
    } else if (!connected) {
        lv_label_set_text(s_status_label, "Connect OBD adapter first");
        lv_obj_set_style_text_color(s_status_label, t->warn, 0);
    } else {
        lv_label_set_text(s_status_label, "");
    }

    lv_obj_clean(s_list);

    if (!connected) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "Not connected");
        lv_obj_set_style_text_font(empty, t->font_md, 0);
        lv_obj_set_style_text_color(empty, t->text_dim, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        return;
    }

    if (vd->dtc_scan_state == DTC_SCAN_BUSY || obd_dtc_is_busy()) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "Scanning ECU...");
        lv_obj_set_style_text_font(empty, t->font_md, 0);
        lv_obj_set_style_text_color(empty, t->primary, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        return;
    }

    if (vd->dtc_count == 0 && vd->dtc_pending_count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        if (vd->dtc_scan_state == DTC_SCAN_DONE) {
            lv_label_set_text(empty, "No fault codes");
            lv_obj_set_style_text_color(empty, t->ok, 0);
        } else {
            lv_label_set_text(empty, "Tap Scan to read codes");
            lv_obj_set_style_text_color(empty, t->text_dim, 0);
        }
        lv_obj_set_style_text_font(empty, t->font_md, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        return;
    }

    for (int i = 0; i < vd->dtc_count; i++) {
        lv_obj_t *item = lv_label_create(s_list);
        lv_label_set_text_fmt(item, "%s", vd->dtcs[i]);
        lv_obj_set_style_text_font(item, t->font_sm, 0);
        lv_obj_set_style_text_color(item, t->warn, 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(item, LV_PCT(100));
    }

    if (vd->dtc_pending_count > 0) {
        lv_obj_t *pending = lv_label_create(s_list);
        lv_label_set_text_fmt(pending, "Pending: %d", vd->dtc_pending_count);
        lv_obj_set_style_text_font(pending, t->font_sm, 0);
        lv_obj_set_style_text_color(pending, t->text_dim, 0);
        lv_obj_set_style_text_align(pending, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(pending, LV_PCT(100));
    }
}
