#include "dashboard_ui.h"
#include "ui_fonts.h"
#include "config.h"
#include <lvgl.h>
#include <Arduino.h>
#include <cstring>

using namespace UiTheme;

void DashboardUi::show() {
    if (screen_) {
        lv_scr_load(screen_);
    }
}

void DashboardUi::hide() {}

bool DashboardUi::consumeOpenWifiMenuRequest() {
    if (openWifiMenuReq_) {
        openWifiMenuReq_ = false;
        return true;
    }
    return false;
}

bool DashboardUi::consumeOpenVehicleInfoRequest() {
    if (openVehicleInfoReq_) {
        openVehicleInfoReq_ = false;
        return true;
    }
    return false;
}

bool DashboardUi::textHas(const char *haystack, const char *needle) {
    return haystack && needle && strstr(haystack, needle) != nullptr;
}

void DashboardUi::applyChannel(Channel ch) {
    channel_ = ch;
    uint32_t accent = kAmber300;
    switch (ch) {
    case Channel::Rpm:
        gauge_.setChannel("RPM", "dev / dk", 6500, kAmber300);
        accent = kAmber300;
        break;
    case Channel::Speed:
        gauge_.setChannel("HIZ", "km / s", 180, kAmber300);
        accent = kAmber300;
        break;
    case Channel::Coolant:
        gauge_.setChannel("SOĞUTMA", "°C", 120, kAmber500);
        accent = kAmber500;
        break;
    case Channel::Voltage:
        gauge_.setChannel("VOLTAJ", "V", 16, kCyan500);
        accent = kCyan500;
        break;
    case Channel::Throttle:
        gauge_.setChannel("GAZ", "%", 100, kAmber300);
        accent = kAmber300;
        break;
    case Channel::Intake:
        gauge_.setChannel("EMME", "°C", 80, kOlive500);
        accent = kOlive500;
        break;
    case Channel::EngineLoad:
        gauge_.setChannel("YÜK", "%", 100, kAmber500);
        accent = kAmber500;
        break;
    case Channel::Map:
        gauge_.setChannel("MAP", "psi", 40, kCyan500);
        accent = kCyan500;
        break;
    case Channel::Fuel:
        gauge_.setChannel("YAKIT", "%", 100, kOlive500);
        accent = kOlive500;
        break;
    default:
        break;
    }
    for (int i = 0; i < static_cast<int>(Channel::Count); ++i) {
        if (!indDots_[i]) {
            continue;
        }
        if (i == static_cast<int>(channel_)) {
            lv_obj_set_width(indDots_[i], 22);
            lv_obj_set_style_bg_color(indDots_[i], color(accent), 0);
        } else {
            lv_obj_set_width(indDots_[i], 14);
            lv_obj_set_style_bg_color(indDots_[i], color(kInk500), 0);
        }
    }
}

void DashboardUi::stepChannel(int delta) {
    int ch = static_cast<int>(channel_) + delta;
    const int n = static_cast<int>(Channel::Count);
    while (ch < 0) {
        ch += n;
    }
    while (ch >= n) {
        ch -= n;
    }
    applyChannel(static_cast<Channel>(ch));
}

void DashboardUi::refreshChannelValue(const ObdSnapshot &obd) {
    switch (channel_) {
    case Channel::Rpm:
        if (obd.rpmValid) {
            gauge_.setValue(obd.rpm);
        }
        break;
    case Channel::Speed:
        if (obd.speedValid) {
            gauge_.setValue(obd.speedKmh);
        }
        break;
    case Channel::Coolant:
        if (obd.coolantValid) {
            gauge_.setValue(obd.coolantC);
        }
        break;
    case Channel::Voltage:
        if (obd.voltageValid) {
            gauge_.setValue(obd.voltage);
        }
        break;
    case Channel::Throttle:
        if (obd.throttleValid) {
            gauge_.setValue(obd.throttlePct);
        }
        break;
    case Channel::Intake:
        if (obd.intakeValid) {
            gauge_.setValue(obd.intakeC);
        }
        break;
    case Channel::EngineLoad:
        if (obd.engineLoadValid) {
            gauge_.setValue(obd.engineLoadPct);
        }
        break;
    case Channel::Map:
        if (obd.mapValid) {
            gauge_.setValue(obd.mapPsi);
        }
        break;
    case Channel::Fuel:
        if (obd.fuelValid) {
            gauge_.setValue(obd.fuelPct);
        }
        break;
    default:
        break;
    }
}

void DashboardUi::tapLayerEventCb(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    auto *self = static_cast<DashboardUi *>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);

    if (code == LV_EVENT_LONG_PRESSED) {
        self->openVehicleInfoReq_ = true;
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        self->pressActive_ = true;
        self->pressPt_ = pt;
        return;
    }

    if (code == LV_EVENT_RELEASED && self->pressActive_) {
        self->pressActive_ = false;
        const int dx = pt.x - self->pressPt_.x;
        const int dy = pt.y - self->pressPt_.y;
        if (abs(dx) > 45 && abs(dx) > abs(dy)) {
            self->stepChannel(dx > 0 ? -1 : 1);
            return;
        }
    }

    if (code != LV_EVENT_CLICKED) {
        return;
    }

    const uint32_t now = millis();
    const int dx = abs(pt.x - self->lastTapPt_.x);
    const int dy = abs(pt.y - self->lastTapPt_.y);

    if (now - self->lastTapMs_ < 450 && dx < 40 && dy < 40) {
        self->openWifiMenuReq_ = true;
        self->lastTapMs_ = 0;
        return;
    }

    self->lastTapMs_ = now;
    self->lastTapPt_ = pt;
}

void DashboardUi::create() {
    screen_ = lv_obj_create(NULL);
    applyScreen(screen_);

    gauge_.create(screen_);
    applyChannel(Channel::Rpm);

    pillWifi_ = makeStatusPill(screen_, "WiFi", false);
    lv_obj_align(pillWifi_, LV_ALIGN_BOTTOM_MID, -62, -82);

    pillObd_ = makeStatusPill(screen_, "OBD", false);
    lv_obj_align(pillObd_, LV_ALIGN_BOTTOM_MID, 62, -82);
    lv_obj_move_foreground(pillWifi_);
    lv_obj_move_foreground(pillObd_);

    indRow_ = lv_obj_create(screen_);
    lv_obj_set_size(indRow_, 220, 14);
    lv_obj_align(indRow_, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_set_style_bg_opa(indRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(indRow_, 0, 0);
    lv_obj_set_flex_flow(indRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(indRow_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(indRow_, 8, 0);
    lv_obj_clear_flag(indRow_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(indRow_);

    for (int i = 0; i < static_cast<int>(Channel::Count); ++i) {
        indDots_[i] = makeIndicatorDot(indRow_, i);
    }
    setIndicatorActive(indDots_, static_cast<int>(Channel::Count), 0);

    lblHint_ = lv_label_create(screen_);
    lv_label_set_text(lblHint_,
                      LV_SYMBOL_LEFT " Kaydir · cift WiFi · uzun Arac");
    lv_obj_set_style_text_color(lblHint_, color(kInk400), 0);
    lv_obj_set_style_text_font(lblHint_, UI_FONT_SM, 0);
    lv_obj_align(lblHint_, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_move_foreground(lblHint_);

    tapLayer_ = lv_obj_create(screen_);
    lv_obj_set_size(tapLayer_, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_opa(tapLayer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tapLayer_, 0, 0);
    lv_obj_align(tapLayer_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(tapLayer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tapLayer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tapLayer_, tapLayerEventCb, LV_EVENT_ALL, this);
    lv_obj_move_foreground(tapLayer_);
}

void DashboardUi::update(const ObdSnapshot &obd, const WifiManager &wifi,
                          const Elm327Client &elm, const char *pollLabel) {
    (void)pollLabel;
    const char *wifiTxt = wifi.statusText();
    const char *elmTxt = elm.statusText();

    const bool wifiOk =
        textHas(wifiTxt, "Bağlı") || textHas(wifiTxt, "Bagli") ||
        textHas(wifiTxt, "Connected");
    const bool obdOk = elm.isReady();
    const bool obdPending = wifiOk && !obdOk;

    if (wifiOk != lastWifiOk_) {
        lastWifiOk_ = wifiOk;
        setStatusPill(pillWifi_, wifiOk, false);
    }
    if (obdOk != lastObdReady_ || obdPending != lastObdPending_) {
        lastObdReady_ = obdOk;
        lastObdPending_ = obdPending;
        setStatusPill(pillObd_, obdOk, obdPending);
    }

    if (!obdPending) {
        refreshChannelValue(obd);
    }

    if (lblHint_) {
        const char *hint =
            (wifiOk && !obdOk)
                ? elmTxt
                : (obdOk ? LV_SYMBOL_LEFT " Kaydir · cift WiFi · uzun Arac"
                         : nullptr);
        if (hint && strncmp(hint, lastHint_, sizeof(lastHint_)) != 0) {
            const uint32_t now = millis();
            const bool throttleHint = obdPending && !obdOk;
            if (!throttleHint || now - lastHintMs_ >= UI_HINT_PENDING_MS) {
                strncpy(lastHint_, hint, sizeof(lastHint_) - 1);
                lastHint_[sizeof(lastHint_) - 1] = '\0';
                lv_label_set_text(lblHint_, hint);
                lastHintMs_ = now;
            }
        }
    }
}
