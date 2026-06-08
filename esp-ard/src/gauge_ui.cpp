#include "gauge_ui.h"
#include "ui_fonts.h"
#include "config.h"
#include <cstdio>
#include <cstring>

using namespace UiTheme;

namespace {

static void setArcColor(lv_meter_indicator_t *ind, uint32_t hex) {
    if (ind) {
        ind->type_data.arc.color = lv_color_hex(hex);
    }
}

static void addZoneArc(lv_obj_t *meter, lv_meter_scale_t *scale, lv_meter_indicator_t *&slot,
                       int32_t from, int32_t to, uint32_t hex, int16_t r_mod, uint16_t width,
                       lv_opa_t opa) {
    slot = lv_meter_add_arc(meter, scale, width, lv_color_hex(hex), r_mod);
    slot->opa = opa;
    lv_meter_set_indicator_start_value(meter, slot, from);
    lv_meter_set_indicator_end_value(meter, slot, to);
}

} // namespace

PhosphorGaugeUi::ScaleProfile PhosphorGaugeUi::scaleProfile(int32_t value_max) {
    ScaleProfile p{};
    if (value_max <= 10) {
        p.meterMax = value_max;
        p.tickCnt = static_cast<uint16_t>(value_max + 1);
        p.majorNth = 1;
        p.zones = false;
        return p;
    }
    if (value_max <= 100) {
        p.meterMax = value_max;
        p.tickCnt = 11;
        p.majorNth = 2;
        p.zones = (value_max > 50);
        return p;
    }
    if (value_max <= 200) {
        p.meterMax = value_max;
        p.tickCnt = 13;
        p.majorNth = 3;
        p.zones = true;
        return p;
    }
    p.meterMax = 8;
    p.tickCnt = 9;
    p.majorNth = 2;
    p.zones = true;
    return p;
}

bool PhosphorGaugeUi::sameProfile(const ScaleProfile &a, const ScaleProfile &b) {
    return a.meterMax == b.meterMax && a.tickCnt == b.tickCnt &&
           a.majorNth == b.majorNth && a.zones == b.zones;
}

void PhosphorGaugeUi::applyDialTheme() {
    if (!meter_ || !scale_) {
        return;
    }

    scale_->tick_color = lv_color_hex(theme_.tickMinor);
    scale_->tick_major_color = lv_color_hex(theme_.tickMajor);

    lv_obj_set_style_text_color(meter_, lv_color_hex(theme_.tickLabel), LV_PART_TICKS);

    setArcColor(arcTrack_, theme_.arcTrack);
    setArcColor(arcFill_, theme_.accent);

    for (int i = 0; i < 3; ++i) {
        if (!zoneArcs_[i]) {
            continue;
        }
        const uint32_t zc =
            (i == 0) ? theme_.zoneLo : ((i == 1) ? theme_.zoneMid : theme_.zoneHi);
        setArcColor(zoneArcs_[i], zc);
        zoneArcs_[i]->opa = theme_.zoneOpa ? theme_.zoneOpa : LV_OPA_TRANSP;
    }

    lv_obj_invalidate(meter_);
}

void PhosphorGaugeUi::applyCenterTheme() {
    if (lblTitle_) {
        lv_obj_set_style_text_color(lblTitle_, color(theme_.titleText), 0);
    }
    if (lblValue_) {
        lv_obj_set_style_text_color(lblValue_, color(theme_.valueText), 0);
    }
    if (lblUnit_) {
        lv_obj_set_style_text_color(lblUnit_, color(theme_.unitText), 0);
    }
    if (halo_) {
        lv_obj_set_style_border_color(halo_, color(mixHex(theme_.accent, kInk500, 160)), 0);
    }
}

void PhosphorGaugeUi::rebuildMeter() {
    if (!parent_) {
        return;
    }

    if (meter_) {
        lv_obj_del(meter_);
        meter_ = nullptr;
        scale_ = nullptr;
        arcTrack_ = nullptr;
        arcFill_ = nullptr;
        zoneArcs_[0] = zoneArcs_[1] = zoneArcs_[2] = nullptr;
    }

    const int d = LCD_GAUGE_DIAMETER;
    const int arcW = LCD_GAUGE_ARC_W + 2;
    const int rMod = -(d / 16);
    const int zoneMod = rMod + 5;

    meter_ = lv_meter_create(parent_);
    lv_obj_set_size(meter_, d, d);
    lv_obj_align(meter_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(meter_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meter_, 0, 0);
    lv_obj_clear_flag(meter_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(meter_, &lv_font_tr_14, LV_PART_TICKS);

    scale_ = lv_meter_add_scale(meter_);
    lv_meter_set_scale_range(meter_, scale_, 0, profile_.meterMax, 270, 135);
    lv_meter_set_scale_ticks(meter_, scale_, profile_.tickCnt, 2, 11,
                             lv_color_hex(theme_.tickMinor));
    lv_meter_set_scale_major_ticks(meter_, scale_, profile_.majorNth, 3, 20,
                                   lv_color_hex(theme_.tickMajor), 28);

    if (profile_.zones && theme_.zoneOpa > 0) {
        const int32_t z1 = (profile_.meterMax * 45) / 100;
        const int32_t z2 = (profile_.meterMax * 70) / 100;
        addZoneArc(meter_, scale_, zoneArcs_[0], 0, z1, theme_.zoneLo, zoneMod, 5,
                   theme_.zoneOpa);
        addZoneArc(meter_, scale_, zoneArcs_[1], z1, z2, theme_.zoneMid, zoneMod, 5,
                   theme_.zoneOpa);
        addZoneArc(meter_, scale_, zoneArcs_[2], z2, profile_.meterMax, theme_.zoneHi, zoneMod,
                   5, theme_.zoneOpa);
    }

    arcTrack_ = lv_meter_add_arc(meter_, scale_, arcW, lv_color_hex(theme_.arcTrack), rMod);
    lv_meter_set_indicator_start_value(meter_, arcTrack_, 0);
    lv_meter_set_indicator_end_value(meter_, arcTrack_, profile_.meterMax);

    arcFill_ = lv_meter_add_arc(meter_, scale_, arcW, lv_color_hex(theme_.accent), rMod);
    lv_meter_set_indicator_start_value(meter_, arcFill_, 0);
    lv_meter_set_indicator_end_value(meter_, arcFill_, 0);

    applyDialTheme();

    if (halo_) {
        lv_obj_move_background(halo_);
    }
    if (panel_) {
        lv_obj_move_foreground(panel_);
    }
}

void PhosphorGaugeUi::create(lv_obj_t *parent) {
    parent_ = parent;
    profile_ = scaleProfile(valueMax_);
    theme_ = gaugeDialTheme(accentHex_, profile_.zones);

    const int d = LCD_GAUGE_DIAMETER;
    const int haloSz = d / 3 + 8;

    halo_ = lv_obj_create(parent);
    lv_obj_set_size(halo_, haloSz, haloSz);
    lv_obj_set_style_radius(halo_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(halo_, color(mixHex(kInk700, kLcdBg, 128)), 0);
    lv_obj_set_style_bg_opa(halo_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(halo_, 1, 0);
    lv_obj_set_style_border_color(halo_, color(kInk500), 0);
    lv_obj_clear_flag(halo_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(halo_, LV_ALIGN_CENTER, 0, 0);

    rebuildMeter();

    const int panelW = d / 2 + 20;
    const int panelH = d / 3 + 12;
    panel_ = lv_obj_create(parent);
    lv_obj_set_size(panel_, panelW, panelH);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_, 0, 0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(panel_, LV_ALIGN_CENTER, 0, 0);

    lblTitle_ = lv_label_create(panel_);
    lv_label_set_text(lblTitle_, "- RPM -");
    lv_obj_set_style_text_font(lblTitle_, UI_FONT_SM, 0);
    lv_obj_align(lblTitle_, LV_ALIGN_TOP_MID, 0, 4);

    lblValue_ = lv_label_create(panel_);
    lv_label_set_text(lblValue_, "0");
    lv_obj_set_style_text_font(lblValue_, UI_FONT_VALUE, 0);
    lv_obj_align(lblValue_, LV_ALIGN_CENTER, 0, 0);

    lblUnit_ = lv_label_create(panel_);
    lv_label_set_text(lblUnit_, unitBuf_);
    lv_obj_set_style_text_font(lblUnit_, UI_FONT_SM, 0);
    lv_obj_align(lblUnit_, LV_ALIGN_BOTTOM_MID, 0, -2);

    applyCenterTheme();
    lv_obj_move_foreground(panel_);
}

void PhosphorGaugeUi::setChannel(const char *label, const char *unit, int32_t max_value,
                                 uint32_t accent_hex) {
    if (label) {
        strncpy(labelBuf_, label, sizeof(labelBuf_) - 1);
        labelBuf_[sizeof(labelBuf_) - 1] = '\0';
    }
    if (unit) {
        strncpy(unitBuf_, unit, sizeof(unitBuf_) - 1);
        unitBuf_[sizeof(unitBuf_) - 1] = '\0';
    }
    valueMax_ = max_value > 0 ? max_value : 1;
    accentHex_ = accent_hex;

    const ScaleProfile next = scaleProfile(valueMax_);
    theme_ = gaugeDialTheme(accent_hex, next.zones);

    const bool rebuild = !meter_ || !sameProfile(profile_, next);
    profile_ = next;

    if (rebuild) {
        rebuildMeter();
    } else {
        applyDialTheme();
    }
    applyCenterTheme();

    char title[32];
    snprintf(title, sizeof(title), "- %s -", labelBuf_);
    if (lblTitle_) {
        lv_label_set_text(lblTitle_, title);
    }
    if (lblUnit_) {
        lv_label_set_text(lblUnit_, unitBuf_);
    }
    if (arcFill_) {
        lv_meter_set_indicator_end_value(meter_, arcFill_, 0);
    }
    lastArcEnd_ = -1;
    lastValueText_[0] = '\0';
}

int32_t PhosphorGaugeUi::valueToArc(float value) const {
    const int32_t v =
        static_cast<int32_t>((value / static_cast<float>(valueMax_)) * profile_.meterMax + 0.5f);
    return v > profile_.meterMax ? profile_.meterMax : v;
}

int32_t PhosphorGaugeUi::valueToArc(int32_t value) const {
    const int32_t v = static_cast<int32_t>(
        (static_cast<int64_t>(value) * profile_.meterMax + valueMax_ / 2) / valueMax_);
    return v > profile_.meterMax ? profile_.meterMax : v;
}

void PhosphorGaugeUi::setValue(float value) {
    if (!meter_ || !arcFill_ || !lblValue_) {
        return;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > static_cast<float>(valueMax_)) {
        value = static_cast<float>(valueMax_);
    }

    const int32_t arcEnd = valueToArc(value);
    if (arcEnd != lastArcEnd_) {
        lastArcEnd_ = arcEnd;
        lv_meter_set_indicator_end_value(meter_, arcFill_, arcEnd);
    }

    char buf[16];
    if (strchr(unitBuf_, 'V') != nullptr) {
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(value));
    } else {
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(value + 0.5f));
    }
    if (strcmp(buf, lastValueText_) != 0) {
        strncpy(lastValueText_, buf, sizeof(lastValueText_) - 1);
        lastValueText_[sizeof(lastValueText_) - 1] = '\0';
        lv_label_set_text(lblValue_, buf);
    }
}

void PhosphorGaugeUi::setValueInt(int32_t value) {
    if (!meter_ || !arcFill_ || !lblValue_) {
        return;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > valueMax_) {
        value = valueMax_;
    }

    const int32_t arcEnd = valueToArc(value);
    if (arcEnd != lastArcEnd_) {
        lastArcEnd_ = arcEnd;
        lv_meter_set_indicator_end_value(meter_, arcFill_, arcEnd);
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(value));
    if (strcmp(buf, lastValueText_) != 0) {
        strncpy(lastValueText_, buf, sizeof(lastValueText_) - 1);
        lastValueText_[sizeof(lastValueText_) - 1] = '\0';
        lv_label_set_text(lblValue_, buf);
    }
}
