#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

enum class WifiConnState : uint8_t {
    Idle,
    Scanning,
    Connecting,
    Connected,
    Failed,
};

struct WifiScanEntry {
    char ssid[33];
    int8_t rssi;
    bool open;
};

struct WifiDisplayEntry {
    char ssid[33];
    int8_t rssi;
    bool open;
    bool saved;
    bool inRange;
    bool active;
    char addrHint[28];
};

class WifiManager {
public:
    void begin();
    void loop();

    void setMenuActive(bool active) { menuActive_ = active; }

    void startScan();
    bool isScanning() const { return state_ == WifiConnState::Scanning; }
    bool hasScanResults() const { return scanCount_ > 0; }
    int scanCount() const { return scanCount_; }
    const WifiScanEntry &scanEntry(int index) const;

    int displayCount() const { return displayCount_; }
    const WifiDisplayEntry &displayEntry(int index) const;
    void rebuildDisplayList();
    void ensureDisplayList();
    void markDisplayDirty() { displayDirty_ = true; }

    int savedCount() const;
    bool startConnectSaved(int savedIndex);

    bool startConnect(const char *ssid, const char *password);
    void cancelConnect();
    bool isSavedSsid(const char *ssid) const;
    bool getSavedPasswordFor(const char *ssid, char *out, size_t outLen) const;
    bool savedNetworkHint(const char *ssid, char *out, size_t outLen) const;
    void forgetSavedFor(const char *ssid);
    void forgetSaved();

    bool isConnected() const {
        return state_ == WifiConnState::Connected &&
               WiFi.status() == WL_CONNECTED;
    }
    bool isConnecting() const { return state_ == WifiConnState::Connecting; }
    WifiConnState state() const { return state_; }

    const char *statusText() const;
    const char *connectedSsid() const { return savedSsid_; }
    const char *elmHost() const { return elmHost_; }
    bool consumeLinkUpEvent() {
        if (!linkUpEvent_) {
            return false;
        }
        linkUpEvent_ = false;
        return true;
    }

    void logStoredNetworks() const;

private:
    void tryLoadAndConnect();
    void tryAutoProvision();
    void tryDirectSavedConnect(const char *ssid);
    void onConnectSuccess();
    void onConnectFailed();
    static bool lookupObdPassword(const char *ssid, char *out, size_t outLen);

    WifiConnState state_ = WifiConnState::Idle;
    WifiScanEntry scanList_[WIFI_SCAN_MAX];
    int scanCount_ = 0;
    WifiDisplayEntry displayList_[WIFI_DISPLAY_MAX];
    int displayCount_ = 0;

    char savedSsid_[33] = {};
    char savedPass_[64] = {};
    char elmHost_[40] = {};
    char pendingSsid_[33] = {};
    char pendingPass_[64] = {};

    uint32_t connectStartMs_ = 0;
    uint32_t lastFailMs_ = 0;
    uint32_t lastAutoScanMs_ = 0;
    uint32_t lastDirectConnectMs_ = 0;
    bool autoProvision_ = false;
    bool menuActive_ = false;
    bool displayDirty_ = true;
    bool linkUpEvent_ = false;
};
