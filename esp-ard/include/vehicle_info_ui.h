#pragma once

#include <lvgl.h>
#include "obd_extras.h"

using UiNavHomeFn = void (*)();

class VehicleInfoUi {
public:
    void create();
    void show(ObdExtras &extras);
    void hide();
    ObdExtras *extrasPtr() { return extras_; }
    bool isVisible() const { return visible_; }

    void setNavHomeCallback(UiNavHomeFn fn) { navHome_ = fn; }
    void tick(ObdExtras &extras);
    bool consumeCloseRequest();

private:
    void dismissToHome();
    void refresh(ObdExtras &extras);
    static void btnEventCb(lv_event_t *e);

    lv_obj_t *screen_ = nullptr;
    lv_obj_t *lblVin_ = nullptr;
    lv_obj_t *lblMil_ = nullptr;
    lv_obj_t *lblDtc_ = nullptr;
    lv_obj_t *lblStatus_ = nullptr;
    lv_obj_t *btnBack_ = nullptr;
    lv_obj_t *btnVin_ = nullptr;
    lv_obj_t *btnDtc_ = nullptr;
    lv_obj_t *btnClear_ = nullptr;

    ObdExtras *extras_ = nullptr;
    UiNavHomeFn navHome_ = nullptr;
    bool visible_ = false;
    bool closeReq_ = false;
};
