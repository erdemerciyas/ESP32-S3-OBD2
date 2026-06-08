#pragma once

#include <lvgl.h>
#include "ui_theme.h"
#include <cstdint>

/** Phosphor arc gauge — 270° sweep, channel-aware (ui-demo.html) */
class PhosphorGaugeUi {
public:
    void create(lv_obj_t *parent);
    void setChannel(const char *label, const char *unit, int32_t value_max,
                    uint32_t accent_hex);
    void setValue(float value);
    void setValueInt(int32_t value);

    lv_obj_t *valueLabel() const { return lblValue_; }

private:
    struct ScaleProfile {
        int32_t meterMax = 8;
        uint16_t tickCnt = 9;
        uint16_t majorNth = 2;
        bool zones = true;
    };

    static ScaleProfile scaleProfile(int32_t value_max);
    static bool sameProfile(const ScaleProfile &a, const ScaleProfile &b);

    void rebuildMeter();
    void applyDialTheme();
    void applyCenterTheme();
    int32_t valueToArc(float value) const;
    int32_t valueToArc(int32_t value) const;

    lv_obj_t *parent_ = nullptr;
    lv_obj_t *halo_ = nullptr;
    lv_obj_t *panel_ = nullptr;
    lv_obj_t *meter_ = nullptr;
    lv_obj_t *lblTitle_ = nullptr;
    lv_obj_t *lblValue_ = nullptr;
    lv_obj_t *lblUnit_ = nullptr;
    lv_meter_scale_t *scale_ = nullptr;
    lv_meter_indicator_t *arcFill_ = nullptr;
    lv_meter_indicator_t *arcTrack_ = nullptr;
    lv_meter_indicator_t *zoneArcs_[3] = {};

    ScaleProfile profile_{};
    UiTheme::GaugeDialTheme theme_{};
    int32_t valueMax_ = 6500;
    uint32_t accentHex_ = 0xffb44a;
    char labelBuf_[24] = "RPM";
    char unitBuf_[20] = "dev / dk";
    int32_t lastArcEnd_ = -1;
    char lastValueText_[16] = {};
};
