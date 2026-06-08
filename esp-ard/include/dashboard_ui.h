#pragma once

#include <lvgl.h>
#include "config.h"
#include "gauge_ui.h"
#include "ui_theme.h"
#include "obd_service.h"
#include "wifi_manager.h"
#include "elm327_client.h"

class DashboardUi {
public:
    void create();
    void show();
    void hide();
    void update(const ObdSnapshot &obd, const WifiManager &wifi,
                const Elm327Client &elm, const char *pollLabel);

    bool consumeOpenWifiMenuRequest();
    bool consumeOpenVehicleInfoRequest();

private:
    enum class Channel : uint8_t {
        Rpm = 0,
        Speed,
        Coolant,
        Voltage,
        Throttle,
        Intake,
        EngineLoad,
        Map,
        Fuel,
        Count
    };

    static void tapLayerEventCb(lv_event_t *e);
    static bool textHas(const char *haystack, const char *needle);

    void applyChannel(Channel ch);
    void stepChannel(int delta);
    void refreshChannelValue(const ObdSnapshot &obd);

    lv_obj_t *screen_ = nullptr;
    lv_obj_t *tapLayer_ = nullptr;
    lv_obj_t *pillWifi_ = nullptr;
    lv_obj_t *pillObd_ = nullptr;
    lv_obj_t *lblHint_ = nullptr;
    lv_obj_t *indRow_ = nullptr;
    lv_obj_t *indDots_[LCD_CHANNEL_COUNT] = {};
    PhosphorGaugeUi gauge_;

    uint32_t lastTapMs_ = 0;
    lv_point_t lastTapPt_ = {};
    lv_point_t pressPt_ = {};
    bool pressActive_ = false;
    bool openWifiMenuReq_ = false;
    bool openVehicleInfoReq_ = false;
    Channel channel_ = Channel::Rpm;
    bool lastWifiOk_ = false;
    bool lastObdReady_ = false;
    bool lastObdPending_ = false;
    char lastHint_[72] = {};
    uint32_t lastHintMs_ = 0;
};
