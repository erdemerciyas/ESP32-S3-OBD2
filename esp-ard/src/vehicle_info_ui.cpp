#include "vehicle_info_ui.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "config.h"
#include <cstdio>
#include <cstring>

using namespace UiTheme;

void VehicleInfoUi::create() {
    screen_ = lv_obj_create(NULL);
    applyScreen(screen_);

    lv_obj_t *title = lv_label_create(screen_);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Arac");
    lv_obj_set_style_text_font(title, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(title, color(kInk100), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    btnBack_ = makeGhostBtn(screen_, LV_SYMBOL_LEFT " Geri", 100, 34);
    lv_obj_align(btnBack_, LV_ALIGN_TOP_LEFT, 36, 78);
    registerNavBtnEvents(btnBack_, btnEventCb, this);
    lv_obj_set_user_data(btnBack_, reinterpret_cast<void *>(intptr_t(0)));

    lblVin_ = lv_label_create(screen_);
    lv_obj_set_width(lblVin_, WIFI_UI_W);
    lv_label_set_long_mode(lblVin_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lblVin_, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(lblVin_, color(kInk100), 0);
    lv_obj_align(lblVin_, LV_ALIGN_TOP_MID, 0, 120);

    lblMil_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(lblMil_, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(lblMil_, color(kAmber300), 0);
    lv_obj_align(lblMil_, LV_ALIGN_TOP_MID, 0, 168);

    lblDtc_ = lv_label_create(screen_);
    lv_obj_set_width(lblDtc_, WIFI_UI_W);
    lv_label_set_long_mode(lblDtc_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lblDtc_, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(lblDtc_, color(kInk300), 0);
    lv_obj_align(lblDtc_, LV_ALIGN_TOP_MID, 0, 200);

    lblStatus_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(lblStatus_, UI_FONT_XS, 0);
    lv_obj_set_style_text_color(lblStatus_, color(kInk400), 0);
    lv_obj_align(lblStatus_, LV_ALIGN_TOP_MID, 0, 300);

    btnVin_ = makePrimaryBtn(screen_, "VIN oku", 130, 36);
    lv_obj_align(btnVin_, LV_ALIGN_BOTTOM_MID, -72, -120);
    lv_obj_add_event_cb(btnVin_, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnVin_, reinterpret_cast<void *>(intptr_t(1)));

    btnDtc_ = makePrimaryBtn(screen_, "DTC oku", 130, 36);
    lv_obj_align(btnDtc_, LV_ALIGN_BOTTOM_MID, 72, -120);
    lv_obj_add_event_cb(btnDtc_, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnDtc_, reinterpret_cast<void *>(intptr_t(2)));

    btnClear_ = makeGhostBtn(screen_, "MIL temizle (04)", 200, 36);
    lv_obj_align(btnClear_, LV_ALIGN_BOTTOM_MID, 0, -68);
    lv_obj_add_event_cb(btnClear_, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnClear_, reinterpret_cast<void *>(intptr_t(3)));
}

void VehicleInfoUi::show(ObdExtras &extras) {
    extras_ = &extras;
    visible_ = true;
    lv_scr_load(screen_);
    if (!extras.data().vinValid) {
        extras.requestVinRead();
    }
    refresh(extras);
}

void VehicleInfoUi::hide() {
    visible_ = false;
    extras_ = nullptr;
}

void VehicleInfoUi::dismissToHome() {
    hide();
    if (navHome_) {
        navHome_();
    }
}

bool VehicleInfoUi::consumeCloseRequest() {
    if (closeReq_) {
        closeReq_ = false;
        return true;
    }
    return false;
}

void VehicleInfoUi::refresh(ObdExtras &extras) {
    const VehicleExtras &d = extras.data();
    char line[96];

    if (d.vinValid && d.vin[0]) {
        snprintf(line, sizeof(line), "VIN: %s", d.vin);
    } else {
        snprintf(line, sizeof(line), "VIN: —");
    }
    lv_label_set_text(lblVin_, line);

    if (d.milOn) {
        lv_label_set_text(lblMil_, LV_SYMBOL_WARNING " MIL acik");
        lv_obj_set_style_text_color(lblMil_, color(kRust500), 0);
    } else if (d.dtcCount == 0 && strstr(d.statusLine, "MIL kapali")) {
        lv_label_set_text(lblMil_, LV_SYMBOL_OK " MIL kapali");
        lv_obj_set_style_text_color(lblMil_, color(kCyan500), 0);
    } else {
        lv_label_set_text(lblMil_, "MIL: —");
        lv_obj_set_style_text_color(lblMil_, color(kInk300), 0);
    }

    line[0] = '\0';
    if (d.dtcCount > 0) {
        size_t pos = 0;
        for (uint8_t i = 0; i < d.dtcCount && pos < sizeof(line) - 8; ++i) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%s%s", d.dtcCodes[i],
                            (i + 1 < d.dtcCount) ? "  " : "");
        }
    } else {
        strncpy(line, "DTC: —", sizeof(line) - 1);
    }
    lv_label_set_text(lblDtc_, line);
    lv_label_set_text(lblStatus_, d.statusLine);
}

void VehicleInfoUi::tick(ObdExtras &extras) {
    if (!visible_) {
        return;
    }
    refresh(extras);
}

void VehicleInfoUi::btnEventCb(lv_event_t *e) {
    auto *self = static_cast<VehicleInfoUi *>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED) {
        return;
    }
    const intptr_t id =
        reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target(e)));
    if (id == 0) {
        self->dismissToHome();
        return;
    }
    ObdExtras *extras = self->extras_;
    if (!extras) {
        return;
    }
    switch (id) {
    case 1:
        extras->requestVinRead();
        break;
    case 2:
        extras->requestDtcRead();
        break;
    case 3:
        extras->requestDtcClear();
        break;
    default:
        break;
    }
}
