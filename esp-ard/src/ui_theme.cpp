#include "ui_theme.h"
#include "ui_fonts.h"
#include "config.h"

namespace UiTheme {

lv_color_t color(uint32_t hex) { return lv_color_hex(hex); }

uint32_t mixHex(uint32_t base, uint32_t blend, uint8_t blend_ratio) {
    const uint8_t br = (base >> 16) & 0xff;
    const uint8_t bg = (base >> 8) & 0xff;
    const uint8_t bb = base & 0xff;
    const uint8_t er = (blend >> 16) & 0xff;
    const uint8_t eg = (blend >> 8) & 0xff;
    const uint8_t eb = blend & 0xff;
    const uint8_t r = static_cast<uint8_t>(br + ((er - br) * blend_ratio) / 255);
    const uint8_t g = static_cast<uint8_t>(bg + ((eg - bg) * blend_ratio) / 255);
    const uint8_t b = static_cast<uint8_t>(bb + ((eb - bb) * blend_ratio) / 255);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

uint32_t brightenHex(uint32_t hex, uint8_t amount) {
    return mixHex(hex, 0xffffff, amount);
}

GaugeDialTheme gaugeDialTheme(uint32_t accent_hex, bool show_zones) {
    GaugeDialTheme t{};
    t.tickMinor = kGaugeTickMinor;
    t.tickMajor = kGaugeTickMajor;
    t.tickLabel = kGaugeTickLabel;
    t.arcTrack = kGaugeArcTrack;
    t.accent = accent_hex;
    t.valueText = brightenHex(accent_hex, 90);
    t.titleText = kInk400;
    t.unitText = mixHex(accent_hex, kInk300, 140);
    t.zoneLo = kGaugeZoneLo;
    t.zoneMid = kGaugeZoneMid;
    t.zoneHi = kGaugeZoneHi;
    t.zoneOpa = show_zones ? 72 : 0;
    return t;
}

void applyScreen(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, color(kLcdBg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void styleCard(lv_obj_t *obj, int radius) {
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, color(kInk700), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, color(kHairline), 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void styleGlassPanel(lv_obj_t *obj) {
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_bg_color(obj, color(kInk700), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_90, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, color(kInk400), 0);
}

lv_obj_t *makeRoundContent(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LCD_SAFE_WIDTH, LCD_SAFE_BOTTOM - LCD_SAFE_TOP);
    lv_obj_align(root, LV_ALIGN_TOP_MID, 0, LCD_SAFE_TOP);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

lv_obj_t *makeStatusPill(lv_obj_t *parent, const char *text, bool on) {
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_height(pill, 22);
    lv_obj_set_style_min_width(pill, 52, 0);
    lv_obj_set_style_radius(pill, kRadiusPill, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_pad_hor(pill, 4, 0);
    lv_obj_set_style_pad_column(pill, 6, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(pill);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color(on ? kAmber300 : kInk500), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(pill);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(lbl, color(on ? kInk100 : kInk300), 0);

    return pill;
}

void setStatusPill(lv_obj_t *pill, bool on, bool alert) {
    if (!pill) {
        return;
    }
    lv_obj_t *dot = lv_obj_get_child(pill, 0);
    lv_obj_t *lbl = lv_obj_get_child(pill, 1);
    if (dot) {
        lv_obj_set_style_bg_color(dot, color(alert ? kRust500 : (on ? kAmber300 : kInk500)),
                                0);
    }
    if (lbl) {
        lv_obj_set_style_text_color(lbl, color(alert ? kRust500 : (on ? kInk100 : kInk300)),
                                    0);
    }
}

lv_obj_t *makePrimaryBtn(lv_obj_t *parent, const char *text, int w, int h) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, kRadiusBtn, 0);
    lv_obj_set_style_bg_color(btn, color(kAmber500), 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(lbl, color(kLcdBg), 0);
    lv_obj_center(lbl);
    return btn;
}

lv_obj_t *makeGhostBtn(lv_obj_t *parent, const char *text, int w, int h) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, kRadiusBtn, 0);
    lv_obj_set_style_bg_color(btn, color(kInk700), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, color(kHairline), 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(lbl, color(kInk100), 0);
    lv_obj_center(lbl);
    return btn;
}

void styleNavBtn(lv_obj_t *btn) {
    if (!btn) {
        return;
    }
    lv_obj_set_ext_click_area(btn, 18);
}

void registerNavBtnEvents(lv_obj_t *btn, lv_event_cb_t cb, void *user_data) {
    if (!btn || !cb) {
        return;
    }
    styleNavBtn(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_SHORT_CLICKED, user_data);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
}

void styleList(lv_obj_t *list) {
    lv_obj_set_style_bg_color(list, color(kInk700), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_border_color(list, color(kHairline), 0);
    lv_obj_set_style_radius(list, kRadiusCard, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
}

void styleListBtn(lv_obj_t *btn) {
    lv_obj_set_style_bg_color(btn, color(kInk700), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_text_color(btn, color(kInk100), 0);
    lv_obj_set_style_text_font(btn, UI_FONT_SM, 0);
}

void styleTextarea(lv_obj_t *ta) {
    lv_obj_set_style_radius(ta, kRadiusBtn, 0);
    lv_obj_set_style_bg_color(ta, color(kLcdBg), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, color(kInk400), 0);
    lv_obj_set_style_text_color(ta, color(kInk100), 0);
}

void styleKeyboard(lv_obj_t *kb) {
    lv_obj_set_style_bg_color(kb, color(kInk700), 0);
    lv_obj_set_style_bg_color(kb, color(kInk500), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, UI_FONT_SM, 0);
    lv_obj_set_style_text_font(kb, UI_FONT_SM, LV_PART_ITEMS);
}

void styleWifiKeyboard(lv_obj_t *kb) {
    styleKeyboard(kb);
    lv_obj_set_style_radius(kb, kRadiusBtn, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_row(kb, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_column(kb, 2, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, UI_FONT_XS, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, color(kInk100), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, color(kInk700), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
}

lv_obj_t *makeIndicatorDot(lv_obj_t *parent, int index) {
    (void)index;
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 14, 3);
    lv_obj_set_style_radius(dot, 2, 0);
    lv_obj_set_style_bg_color(dot, color(kInk500), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

void setIndicatorActive(lv_obj_t *const *dots, int count, int active) {
    for (int i = 0; i < count; ++i) {
        if (!dots[i]) {
            continue;
        }
        if (i == active) {
            lv_obj_set_width(dots[i], 22);
            lv_obj_set_style_bg_color(dots[i], color(kAmber300), 0);
        } else {
            lv_obj_set_width(dots[i], 14);
            lv_obj_set_style_bg_color(dots[i], color(kInk500), 0);
        }
    }
}

} // namespace UiTheme
