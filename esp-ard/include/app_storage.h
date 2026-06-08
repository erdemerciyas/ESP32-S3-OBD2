#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

enum class NetMode : uint8_t {
    ObdDirect = 0,
    HomeRouter = 1,
};

struct SavedWifiRecord {
    char ssid[33] = {};
    char password[64] = {};
    char lastIp[20] = {};
    char lastGw[20] = {};
    bool openAp = false;
};

class AppStorage {
public:
    void begin();

    NetMode netMode();
    void setNetMode(NetMode mode);

    bool hasElmHost();
    String elmHost();
    void setElmHost(const char *host);

    int savedWifiCount();
    bool getSavedWifi(int index, SavedWifiRecord &out);
    bool findSavedWifi(const char *ssid, SavedWifiRecord &out);
    void upsertSavedWifi(const char *ssid, const char *password, const char *lastIp,
                         const char *lastGw, bool openAp = false);
    void removeSavedWifi(const char *ssid);
    void clearAllSavedWifi();

    String lastUsedWifiSsid();
    void setLastUsedWifiSsid(const char *ssid);

    bool getObdWifi(String &ssid, String &password);
    void setObdWifi(const char *ssid, const char *password);
    void clearObdWifi();

    bool hasLockedEndpoint();
    void getLockedEndpoint(String &host, uint16_t &port);
    void setLockedEndpoint(const char *host, uint16_t port);
    void clearLockedEndpoint();

    bool isProvisioned();
    void setProvisioned(bool done);

    void clearAll();

private:
    void migrateLegacyWifi();
    int findSavedIndex(const char *ssid);
    void writeSavedSlot(int index, const SavedWifiRecord &rec);
    void readSavedSlot(int index, SavedWifiRecord &out);

    Preferences prefs_;
    static constexpr const char *NS = "obd_dash";
};
