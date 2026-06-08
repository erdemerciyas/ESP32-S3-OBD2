#include "wifi_menu_ui.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "config.h"
#include <lvgl.h>
#include <cstdio>
#include <cstring>

using namespace UiTheme;

/* OBD WiFi: rakam + W + ! + WW!' (tek tirnak dahil) */
static const char *const kWifiKbMap[] = {
    "1", "2", "3", LV_SYMBOL_BACKSPACE, "\n", "4", "5", "6", "W", "\n",
    "7", "8", "9", "!", "\n", "0", "WW!'", "'", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, ""};

static const lv_btnmatrix_ctrl_t kWifiKbCtrl[] = {
    1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static void applyWifiKeyboardMap(lv_obj_t *kb) {
    if (!kb) {
        return;
    }
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1,
                        const_cast<const char **>(kWifiKbMap), kWifiKbCtrl);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
}

void WifiMenuUi::raisePassChrome() {
    if (kb_) {
        lv_obj_move_foreground(kb_);
    }
    if (taPass_) {
        lv_obj_move_foreground(taPass_);
    }
    if (rowQuickPass_) {
        lv_obj_move_foreground(rowQuickPass_);
    }
    if (btnRowPass_) {
        lv_obj_move_foreground(btnRowPass_);
    }
    if (passPanel_) {
        lv_obj_move_foreground(passPanel_);
    }
}

void WifiMenuUi::create() {
    screen_ = lv_obj_create(NULL);
    applyScreen(screen_);

    lv_obj_t *content = lv_obj_create(screen_);
    lv_obj_set_size(content, WIFI_UI_W, WIFI_UI_H);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, WIFI_SAFE_Y0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, LV_SYMBOL_WIFI " WiFi");
    lv_obj_set_style_text_font(title, UI_FONT_MD, 0);
    lv_obj_set_style_text_color(title, color(kInk100), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    btnBack_ = makeGhostBtn(content, LV_SYMBOL_LEFT " Kapat", 100, WIFI_PASS_BACK_H);
    lv_obj_align(btnBack_, LV_ALIGN_TOP_LEFT, 0, 22);
    registerNavBtnEvents(btnBack_, btnEventCb, this);
    lv_obj_set_user_data(btnBack_, reinterpret_cast<void *>(6));
    lv_obj_t *backLbl = lv_obj_get_child(btnBack_, 0);
    if (backLbl) {
        lv_obj_set_style_text_font(backLbl, UI_FONT_SM, 0);
    }

    lblInfo_ = lv_label_create(content);
    lv_label_set_text(lblInfo_, "Ağ seçin");
    lv_obj_set_style_text_color(lblInfo_, color(kInk300), 0);
    lv_obj_set_style_text_font(lblInfo_, UI_FONT_XS, 0);
    lv_obj_set_width(lblInfo_, WIFI_UI_W);
    lv_label_set_long_mode(lblInfo_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lblInfo_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lblInfo_, LV_ALIGN_TOP_MID, 0, 22);

    list_ = lv_list_create(content);
    lv_obj_set_size(list_, WIFI_UI_W, WIFI_LIST_H);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, WIFI_LIST_Y);
    styleList(list_);

    const int btnW = (WIFI_UI_W - 16) / 2;

    lv_obj_t *btnRow = lv_obj_create(content);
    lv_obj_set_size(btnRow, WIFI_UI_W, WIFI_FOOTER_H);
    lv_obj_align(btnRow, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btnScan = makePrimaryBtn(btnRow, LV_SYMBOL_REFRESH " Tara", btnW, 36);
    lv_obj_add_event_cb(btnScan, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnScan, reinterpret_cast<void *>(1));
    lv_obj_set_style_text_font(lv_obj_get_child(btnScan, 0), UI_FONT_SM, 0);

    lv_obj_t *btnForget = makeGhostBtn(btnRow, LV_SYMBOL_TRASH " Bu ağ", btnW, 36);
    lv_obj_add_event_cb(btnForget, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnForget, reinterpret_cast<void *>(2));
    lv_obj_set_style_text_font(lv_obj_get_child(btnForget, 0), UI_FONT_XS, 0);

    btnRowMain_ = btnRow;

    btnCancelConnect_ = makeGhostBtn(content, LV_SYMBOL_CLOSE " İptal", WIFI_UI_W, 36);
    lv_obj_align(btnCancelConnect_, LV_ALIGN_BOTTOM_MID, 0, 0);
    registerNavBtnEvents(btnCancelConnect_, btnEventCb, this);
    lv_obj_set_user_data(btnCancelConnect_, reinterpret_cast<void *>(12));
    lv_obj_set_style_text_font(lv_obj_get_child(btnCancelConnect_, 0), UI_FONT_SM, 0);
    lv_obj_add_flag(btnCancelConnect_, LV_OBJ_FLAG_HIDDEN);

    scrim_ = lv_obj_create(screen_);
    lv_obj_set_size(scrim_, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(scrim_, color(kLcdBg), 0);
    lv_obj_set_style_bg_opa(scrim_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(scrim_, 0, 0);
    lv_obj_align(scrim_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(scrim_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(scrim_, LV_OBJ_FLAG_SCROLLABLE);

    passPanel_ = lv_obj_create(scrim_);
    lv_obj_set_size(passPanel_, WIFI_UI_W, WIFI_KB_Y - WIFI_PASS_Y0);
    lv_obj_align(passPanel_, LV_ALIGN_TOP_MID, 0, WIFI_PASS_Y0);
    lv_obj_set_style_bg_opa(passPanel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passPanel_, 0, 0);
    lv_obj_clear_flag(passPanel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btnPassBack = makeGhostBtn(passPanel_, LV_SYMBOL_LEFT " Liste", 92, WIFI_PASS_BACK_H);
    lv_obj_align(btnPassBack, LV_ALIGN_TOP_LEFT, 0, 0);
    registerNavBtnEvents(btnPassBack, btnEventCb, this);
    lv_obj_set_user_data(btnPassBack, reinterpret_cast<void *>(5));
    lv_obj_t *passBackLbl = lv_obj_get_child(btnPassBack, 0);
    if (passBackLbl) {
        lv_obj_set_style_text_font(passBackLbl, UI_FONT_XS, 0);
    }

    lblSelected_ = lv_label_create(passPanel_);
    lv_label_set_text(lblSelected_, "");
    lv_obj_set_style_text_font(lblSelected_, UI_FONT_SM, 0);
    lv_obj_set_style_text_color(lblSelected_, color(kAmber300), 0);
    lv_obj_set_width(lblSelected_, WIFI_UI_W - 98);
    lv_label_set_long_mode(lblSelected_, LV_LABEL_LONG_DOT);
    lv_obj_align(lblSelected_, LV_ALIGN_TOP_LEFT, 98, 8);

    lblPassHint_ = lv_label_create(passPanel_);
    lv_label_set_text(lblPassHint_, "Şifre");
    lv_obj_set_style_text_color(lblPassHint_, color(kInk400), 0);
    lv_obj_set_style_text_font(lblPassHint_, UI_FONT_XS, 0);
    lv_obj_align(lblPassHint_, LV_ALIGN_TOP_LEFT, 0, 36);

    lblOpenHint_ = lv_label_create(passPanel_);
    lv_label_set_text(lblOpenHint_, "Şifresiz ağ\nBağlan veya İptal");
    lv_obj_set_style_text_color(lblOpenHint_, color(kInk300), 0);
    lv_obj_set_style_text_font(lblOpenHint_, UI_FONT_SM, 0);
    lv_obj_set_style_text_align(lblOpenHint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lblOpenHint_, WIFI_UI_W);
    lv_obj_align(lblOpenHint_, LV_ALIGN_CENTER, 0, -24);
    lv_obj_add_flag(lblOpenHint_, LV_OBJ_FLAG_HIDDEN);

    taPass_ = lv_textarea_create(passPanel_);
    lv_obj_set_size(taPass_, WIFI_UI_W, WIFI_PASS_TA_H);
    lv_obj_align(taPass_, LV_ALIGN_TOP_MID, 0, WIFI_PASS_TA_Y - WIFI_PASS_Y0);
    lv_textarea_set_placeholder_text(taPass_, "WiFi şifresi");
    lv_textarea_set_one_line(taPass_, true);
    lv_textarea_set_password_mode(taPass_, false);
    lv_textarea_set_max_length(taPass_, 63);
    styleTextarea(taPass_);
    lv_obj_set_style_text_font(taPass_, UI_FONT_SM, 0);
    lv_obj_add_event_cb(taPass_, textareaEventCb, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(taPass_, textareaEventCb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(taPass_, textareaEventCb, LV_EVENT_PRESSED, this);

    const int passBtnW = (WIFI_UI_W - 16) / 3;
    btnRowPass_ = lv_obj_create(passPanel_);
    lv_obj_set_size(btnRowPass_, WIFI_UI_W, WIFI_PASS_BTNS_H);
    lv_obj_align(btnRowPass_, LV_ALIGN_TOP_MID, 0, WIFI_PASS_BTNS_Y - WIFI_PASS_Y0);
    lv_obj_set_style_bg_opa(btnRowPass_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRowPass_, 0, 0);
    lv_obj_set_flex_flow(btnRowPass_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRowPass_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btnRowPass_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btnRowPass_, LV_OBJ_FLAG_HIDDEN);

    btnConnect_ = makePrimaryBtn(btnRowPass_, LV_SYMBOL_OK " Bağlan", passBtnW, WIFI_PASS_BTNS_H);
    lv_obj_add_event_cb(btnConnect_, connectBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_style_text_font(lv_obj_get_child(btnConnect_, 0), UI_FONT_XS, 0);

    btnForgetSel_ = makeGhostBtn(btnRowPass_, LV_SYMBOL_TRASH " Unut", passBtnW, WIFI_PASS_BTNS_H);
    lv_obj_add_event_cb(btnForgetSel_, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnForgetSel_, reinterpret_cast<void *>(10));
    lv_obj_set_style_text_font(lv_obj_get_child(btnForgetSel_, 0), UI_FONT_XS, 0);
    lv_obj_add_flag(btnForgetSel_, LV_OBJ_FLAG_HIDDEN);

    btnCancelSel_ = makeGhostBtn(btnRowPass_, LV_SYMBOL_CLOSE " İptal", passBtnW, WIFI_PASS_BTNS_H);
    registerNavBtnEvents(btnCancelSel_, btnEventCb, this);
    lv_obj_set_user_data(btnCancelSel_, reinterpret_cast<void *>(11));
    lv_obj_set_style_text_font(lv_obj_get_child(btnCancelSel_, 0), UI_FONT_XS, 0);

    rowQuickPass_ = lv_obj_create(passPanel_);
    lv_obj_set_size(rowQuickPass_, WIFI_UI_W, WIFI_PASS_PRESET_H);
    lv_obj_align(rowQuickPass_, LV_ALIGN_TOP_MID, 0, WIFI_PASS_PRESET_Y - WIFI_PASS_Y0);
    lv_obj_set_style_bg_opa(rowQuickPass_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rowQuickPass_, 0, 0);
    lv_obj_set_flex_flow(rowQuickPass_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rowQuickPass_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rowQuickPass_, LV_OBJ_FLAG_SCROLLABLE);

    const int qW = (WIFI_UI_W - 12) / 2;
    lv_obj_t *btnP1 = makeGhostBtn(rowQuickPass_, "12345678", qW, WIFI_PASS_PRESET_H);
    lv_obj_add_event_cb(btnP1, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnP1, reinterpret_cast<void *>(7));
    lv_obj_t *btnP2 = makeGhostBtn(rowQuickPass_, "WW!'", qW, WIFI_PASS_PRESET_H);
    lv_obj_add_event_cb(btnP2, btnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnP2, reinterpret_cast<void *>(9));
    lv_obj_set_style_text_font(lv_obj_get_child(btnP1, 0), UI_FONT_XS, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(btnP2, 0), UI_FONT_XS, 0);

    kb_ = lv_keyboard_create(scrim_);
    lv_keyboard_set_textarea(kb_, taPass_);
    applyWifiKeyboardMap(kb_);
    lv_obj_set_size(kb_, WIFI_UI_W, WIFI_KB_H);
    lv_obj_align(kb_, LV_ALIGN_TOP_MID, 0, WIFI_KB_Y);
    styleWifiKeyboard(kb_);
    lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);
}

void WifiMenuUi::setPasswordUiVisible(bool showKeyboard) {
    const auto hide = [&](lv_obj_t *o) {
        if (o) {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    };
    const auto show = [&](lv_obj_t *o) {
        if (o) {
            lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    };

    show(btnRowPass_);

    if (showKeyboard) {
        show(lblPassHint_);
        show(taPass_);
        show(kb_);
        show(rowQuickPass_);
        hide(lblOpenHint_);
        lv_keyboard_set_textarea(kb_, taPass_);
        applyWifiKeyboardMap(kb_);
    } else {
        hide(lblPassHint_);
        hide(taPass_);
        hide(kb_);
        hide(rowQuickPass_);
        show(lblOpenHint_);
    }
}

void WifiMenuUi::focusPassTimerCb(lv_timer_t *t) {
    auto *self = static_cast<WifiMenuUi *>(t->user_data);
    if (!self || !self->taPass_ || !self->kb_) {
        return;
    }
    lv_group_t *grp = lv_group_get_default();
    if (!grp) {
        grp = lv_group_create();
        lv_group_set_default(grp);
    }
    lv_group_add_obj(grp, self->taPass_);
    lv_group_focus_obj(self->taPass_);
    lv_keyboard_set_textarea(self->kb_, self->taPass_);
    lv_obj_clear_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
    self->raisePassChrome();
}

void WifiMenuUi::textareaEventCb(lv_event_t *e) {
    auto *self = static_cast<WifiMenuUi *>(lv_event_get_user_data(e));
    if (!self || !self->kb_ || !self->taPass_) {
        return;
    }
    lv_keyboard_set_textarea(self->kb_, self->taPass_);
    lv_obj_clear_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
    self->raisePassChrome();
}

void WifiMenuUi::setInfoText(const char *text) {
    if (!lblInfo_ || !text) {
        return;
    }
    if (strncmp(lastInfoText_, text, sizeof(lastInfoText_)) == 0) {
        return;
    }
    strncpy(lastInfoText_, text, sizeof(lastInfoText_) - 1);
    lastInfoText_[sizeof(lastInfoText_) - 1] = '\0';
    lv_label_set_text(lblInfo_, lastInfoText_);
}

void WifiMenuUi::requestListRebuild() {
    listDirty_ = true;
    listBuilt_ = false;
}

void WifiMenuUi::show(WifiManager &wifi) {
    wifi_ = &wifi;
    if (!screen_) {
        return;
    }
    wifi.setMenuActive(true);
    lv_scr_load(screen_);
    visible_ = true;
    lastTickMs_ = 0;
    lastListRebuildMs_ = 0;
    lastInfoText_[0] = '\0';
    lastWifiState_ = wifi.state();
    requestListRebuild();
    wifi.markDisplayDirty();
    rebuildList(wifi);
    lastListRebuildMs_ = millis();
    listDirty_ = false;
}

void WifiMenuUi::hide() {
    visible_ = false;
    if (wifi_) {
        wifi_->setMenuActive(false);
    }
}

void WifiMenuUi::dismissToHome() {
    hidePasswordPanel();
    hide();
    if (navHome_) {
        navHome_();
    }
}

bool WifiMenuUi::consumeCloseRequest() {
    if (closeRequest_) {
        closeRequest_ = false;
        return true;
    }
    return false;
}

void WifiMenuUi::updateActionButtons() {
    if (!btnForgetSel_) {
        return;
    }
    if (selectedIsSaved_) {
        lv_obj_clear_flag(btnForgetSel_, LV_OBJ_FLAG_HIDDEN);
        if (lblPassHint_) {
            lv_label_set_text(lblPassHint_,
                               selectedOpen_
                                   ? "Kayıtlı şifresiz ağ — Unut veya Bağlan"
                                   : "Kayıtlı ağ — şifre düzenleyebilirsiniz");
        }
    } else {
        lv_obj_add_flag(btnForgetSel_, LV_OBJ_FLAG_HIDDEN);
        if (lblPassHint_ && !selectedOpen_) {
            lv_label_set_text(lblPassHint_, "Şifre");
        }
    }
}

void WifiMenuUi::hidePasswordPanel() {
    lv_obj_add_flag(scrim_, LV_OBJ_FLAG_HIDDEN);
    if (kb_) {
        lv_obj_add_flag(kb_, LV_OBJ_FLAG_HIDDEN);
    }
    if (btnRowPass_) {
        lv_obj_add_flag(btnRowPass_, LV_OBJ_FLAG_HIDDEN);
    }
    selectedSsid_[0] = '\0';
    selectedOpen_ = false;
    selectedIsSaved_ = false;
}

void WifiMenuUi::showPasswordPanel(const char *ssid, bool openNet) {
    strncpy(selectedSsid_, ssid, sizeof(selectedSsid_) - 1);
    selectedSsid_[sizeof(selectedSsid_) - 1] = '\0';
    selectedOpen_ = openNet;
    selectedIsSaved_ = wifi_ && wifi_->isSavedSsid(ssid);

    char buf[96];
    char hint[40] = {};
    if (selectedIsSaved_ && wifi_ && wifi_->savedNetworkHint(ssid, hint, sizeof(hint))) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s\n%s", ssid, hint);
    } else if (selectedIsSaved_) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s (kayitli)", ssid);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s", ssid);
    }
    lv_label_set_text(lblSelected_, buf);

    setPasswordUiVisible(!openNet);
    updateActionButtons();

    if (!openNet) {
        char savedPass[64] = {};
        if (selectedIsSaved_ &&
            wifi_->getSavedPasswordFor(ssid, savedPass, sizeof(savedPass))) {
            lv_textarea_set_text(taPass_, savedPass);
        } else {
            lv_textarea_set_text(taPass_, "12345678");
        }
        lv_keyboard_set_textarea(kb_, taPass_);
    }

    lv_obj_clear_flag(scrim_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(scrim_);
    raisePassChrome();

    if (!openNet) {
        lv_timer_t *tm = lv_timer_create(focusPassTimerCb, 80, this);
        lv_timer_set_repeat_count(tm, 1);
    }
}

void WifiMenuUi::doConnect() {
    if (!wifi_ || selectedSsid_[0] == '\0') {
        return;
    }

    char ssid[33] = {};
    char pass[64] = {};
    strncpy(ssid, selectedSsid_, sizeof(ssid) - 1);

    if (selectedOpen_) {
        pass[0] = '\0';
    } else {
        const char *raw = taPass_ ? lv_textarea_get_text(taPass_) : "";
        if (!raw || raw[0] == '\0') {
            raw = "12345678";
        }
        strncpy(pass, raw, sizeof(pass) - 1);
    }

    lv_label_set_text(lblInfo_, "Bağlanıyor...");
    hidePasswordPanel();
    wifi_->startConnect(ssid, pass);
}

void WifiMenuUi::connectBtnEventCb(lv_event_t *e) {
    auto *self = static_cast<WifiMenuUi *>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    self->doConnect();
}

void WifiMenuUi::onNetworkSelected(int displayIndex) {
    if (!wifi_ || displayIndex < 0 || displayIndex >= wifi_->displayCount()) {
        return;
    }

    const auto &entry = wifi_->displayEntry(displayIndex);
    if (entry.ssid[0] == '\0' || entry.ssid[0] == '-' ||
        strstr(entry.ssid, "---") != nullptr) {
        return;
    }

    if (wifi_->isConnecting()) {
        wifi_->cancelConnect();
    }

    showPasswordPanel(entry.ssid, entry.open);
}

void WifiMenuUi::rebuildList(WifiManager &wifi) {
    if (scrim_ && !lv_obj_has_flag(scrim_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    wifi.ensureDisplayList();

    lv_obj_clean(list_);
    listBuilt_ = false;

    if (wifi.isScanning()) {
        lv_obj_t *t = lv_list_add_text(list_, "Taranıyor...");
        lv_obj_set_style_text_color(t, color(kInk300), 0);
        lv_obj_set_style_text_font(t, UI_FONT_MD, 0);
        return;
    }

    wifi.rebuildDisplayList();
    const int n = wifi.displayCount();
    if (n == 0) {
        lv_obj_t *t = lv_list_add_text(list_, "Ağ yok — Tara");
        lv_obj_set_style_text_color(t, color(kInk300), 0);
        lv_obj_set_style_text_font(t, UI_FONT_MD, 0);
        return;
    }

    for (int i = 0; i < n; ++i) {
        const auto &e = wifi.displayEntry(i);
        if (e.ssid[0] == '-' || strstr(e.ssid, "---") != nullptr) {
            lv_obj_t *t = lv_list_add_text(list_, e.ssid);
            lv_obj_set_style_text_color(t, color(kInk400), 0);
            lv_obj_set_style_text_font(t, UI_FONT_XS, 0);
            continue;
        }

        char line[72];
        const char *sym = LV_SYMBOL_WIFI;
        if (e.active) {
            sym = LV_SYMBOL_OK;
            if (e.addrHint[0]) {
                snprintf(line, sizeof(line), "%s  %s", e.ssid, e.addrHint);
            } else {
                snprintf(line, sizeof(line), "%s  (bagli)", e.ssid);
            }
        } else if (e.saved) {
            sym = LV_SYMBOL_SAVE;
            if (e.addrHint[0]) {
                snprintf(line, sizeof(line), "%s  %s", e.ssid, e.addrHint);
            } else {
                snprintf(line, sizeof(line), "%s  (kayitli)", e.ssid);
            }
            if (e.inRange) {
                char tail[16];
                snprintf(tail, sizeof(tail), " %d", e.rssi);
                strncat(line, tail, sizeof(line) - strlen(line) - 1);
            }
        } else {
            snprintf(line, sizeof(line), "%s  %s", e.ssid,
                     e.addrHint[0] ? e.addrHint : "");
        }

        lv_obj_t *btn = lv_list_add_btn(list_, sym, line);
        styleListBtn(btn);
        lv_obj_set_style_text_font(btn, UI_FONT_SM, 0);
        lv_obj_set_height(btn, e.active ? 44 : 40);
        if (e.active) {
            lv_obj_set_style_bg_color(btn, color(kInk700), 0);
        }
        lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(i + 1)));
        lv_obj_add_event_cb(btn, listBtnEventCb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, listBtnEventCb, LV_EVENT_SHORT_CLICKED, this);
    }
    listBuilt_ = true;
}

void WifiMenuUi::tick(WifiManager &wifi) {
    wifi_ = &wifi;

    if (!visible_) {
        return;
    }

    const uint32_t now = millis();
    if (now - lastTickMs_ < WIFI_MENU_TICK_MS) {
        return;
    }
    lastTickMs_ = now;

    const WifiConnState st = wifi.state();
    if (lastWifiState_ != st) {
        if (st == WifiConnState::Failed) {
            char err[96];
            snprintf(err, sizeof(err), "Hata: %s — sifreyi kontrol edin",
                     wifi.statusText());
            setInfoText(err);
        }
        if (st != WifiConnState::Scanning) {
            requestListRebuild();
        }
        lastWifiState_ = st;
    }

    if (wifi.isConnecting()) {
        if (btnRowMain_) {
            lv_obj_add_flag(btnRowMain_, LV_OBJ_FLAG_HIDDEN);
        }
        if (btnCancelConnect_) {
            lv_obj_clear_flag(btnCancelConnect_, LV_OBJ_FLAG_HIDDEN);
        }
        char info[96];
        snprintf(info, sizeof(info), "%s — iptal edebilirsiniz", wifi.statusText());
        setInfoText(info);
    } else {
        if (btnRowMain_) {
            lv_obj_clear_flag(btnRowMain_, LV_OBJ_FLAG_HIDDEN);
        }
        if (btnCancelConnect_) {
            lv_obj_add_flag(btnCancelConnect_, LV_OBJ_FLAG_HIDDEN);
        }
        char info[80];
        snprintf(info, sizeof(info), "%s", wifi.statusText());
        setInfoText(info);
    }

    const bool force = wifi.isScanning() || !listBuilt_;
    const bool throttledOk = (now - lastListRebuildMs_ >= WIFI_LIST_REBUILD_MS);
    if ((listDirty_ || force) && (force || throttledOk)) {
        rebuildList(wifi);
        lastListRebuildMs_ = now;
        listDirty_ = false;
    }
}

void WifiMenuUi::listBtnEventCb(lv_event_t *e) {
    auto *self = static_cast<WifiMenuUi *>(lv_event_get_user_data(e));
    if (!self || !self->wifi_) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED) {
        return;
    }

    lv_obj_t *btn = lv_event_get_target(e);
    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
    if (tag <= 0) {
        return;
    }
    self->onNetworkSelected(static_cast<int>(tag - 1));
}

void WifiMenuUi::btnEventCb(lv_event_t *e) {
    auto *self = static_cast<WifiMenuUi *>(lv_event_get_user_data(e));
    if (!self || !self->wifi_) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED &&
        code != LV_EVENT_PRESSED) {
        return;
    }

    lv_obj_t *obj = lv_event_get_target(e);
    while (obj && lv_obj_get_user_data(obj) == nullptr) {
        obj = lv_obj_get_parent(obj);
    }
    if (!obj) {
        return;
    }

    const intptr_t id = reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj));

    switch (id) {
    case 1:
        self->requestListRebuild();
        self->wifi_->startScan();
        break;
    case 2:
        if (self->wifi_->isConnected() && self->wifi_->connectedSsid()[0]) {
            self->wifi_->forgetSavedFor(self->wifi_->connectedSsid());
            self->setInfoText("Bagli ag unutuldu");
        } else {
            self->setInfoText("Unutmak icin once aga baglanin");
        }
        self->hidePasswordPanel();
        self->requestListRebuild();
        break;
    case 3:
        self->doConnect();
        break;
    case 5:
        self->hidePasswordPanel();
        break;
    case 6:
        self->dismissToHome();
        break;
    case 7:
        if (self->taPass_) {
            lv_textarea_set_text(self->taPass_, "12345678");
            lv_keyboard_set_textarea(self->kb_, self->taPass_);
        }
        break;
    case 8:
        if (self->taPass_) {
            lv_textarea_set_text(self->taPass_, "123456");
            lv_keyboard_set_textarea(self->kb_, self->taPass_);
        }
        break;
    case 9:
        if (self->taPass_) {
            lv_textarea_add_text(self->taPass_, "WW!'");
            lv_keyboard_set_textarea(self->kb_, self->taPass_);
        }
        break;
    case 10:
        if (self->selectedSsid_[0]) {
            self->wifi_->forgetSavedFor(self->selectedSsid_);
            self->hidePasswordPanel();
            self->requestListRebuild();
            self->setInfoText("Bu ag unutuldu");
        }
        break;
    case 11:
        self->hidePasswordPanel();
        lv_label_set_text(self->lblInfo_, "Ağ seçin");
        break;
    case 12:
        self->wifi_->cancelConnect();
        self->hidePasswordPanel();
        self->requestListRebuild();
        self->setInfoText("Baglanti iptal edildi");
        break;
    default:
        break;
    }
}
