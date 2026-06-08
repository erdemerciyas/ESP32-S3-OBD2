#pragma once

#include <lvgl.h>
#include "wifi_manager.h"

using UiNavHomeFn = void (*)();

class WifiMenuUi {
public:
    void create();
    void show(WifiManager &wifi);
    void hide();
    bool isVisible() const { return visible_; }

    void setNavHomeCallback(UiNavHomeFn fn) { navHome_ = fn; }
    void tick(WifiManager &wifi);
    bool consumeCloseRequest();

private:
    void dismissToHome();
    void rebuildList(WifiManager &wifi);
    void showPasswordPanel(const char *ssid, bool openNet);
    void hidePasswordPanel();
    void onNetworkSelected(int index);
    void doConnect();
    void setPasswordUiVisible(bool showKeyboard);
    void updateActionButtons();
    void raisePassChrome();
    static void listBtnEventCb(lv_event_t *e);
    static void btnEventCb(lv_event_t *e);
    static void connectBtnEventCb(lv_event_t *e);
    static void textareaEventCb(lv_event_t *e);
    static void focusPassTimerCb(lv_timer_t *t);

    WifiManager *wifi_ = nullptr;
    UiNavHomeFn navHome_ = nullptr;

    lv_obj_t *screen_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *lblInfo_ = nullptr;
    lv_obj_t *scrim_ = nullptr;
    lv_obj_t *passPanel_ = nullptr;
    lv_obj_t *lblSelected_ = nullptr;
    lv_obj_t *lblPassHint_ = nullptr;
    lv_obj_t *lblOpenHint_ = nullptr;
    lv_obj_t *taPass_ = nullptr;
    lv_obj_t *kb_ = nullptr;
    lv_obj_t *rowQuickPass_ = nullptr;
    lv_obj_t *btnRowPass_ = nullptr;
    lv_obj_t *btnConnect_ = nullptr;
    lv_obj_t *btnForgetSel_ = nullptr;
    lv_obj_t *btnCancelSel_ = nullptr;
    lv_obj_t *btnRowMain_ = nullptr;
    lv_obj_t *btnCancelConnect_ = nullptr;
    lv_obj_t *btnBack_ = nullptr;

    char selectedSsid_[33] = {};
    bool selectedOpen_ = false;
    bool selectedIsSaved_ = false;
    bool visible_ = false;
    bool listBuilt_ = false;
    bool listDirty_ = true;
    bool closeRequest_ = false;
    uint32_t lastTickMs_ = 0;
    uint32_t lastListRebuildMs_ = 0;
    char lastInfoText_[96] = {};
    WifiConnState lastWifiState_ = WifiConnState::Idle;

    void setInfoText(const char *text);
    void requestListRebuild();
};
