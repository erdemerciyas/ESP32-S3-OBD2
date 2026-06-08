#include "app_storage.h"
#include "config.h"
#include <cstring>

void AppStorage::begin() {
    prefs_.begin(NS, false);
    migrateLegacyWifi();
}

void AppStorage::migrateLegacyWifi() {
    if (prefs_.getUChar("wifi_cnt", 0) > 0) {
        return;
    }
    if (!prefs_.isKey("obd_ssid")) {
        return;
    }
    const String ssid = prefs_.getString("obd_ssid", "");
    const String pass = prefs_.getString("obd_pass", "");
    if (ssid.length() == 0) {
        return;
    }
    SavedWifiRecord rec{};
    ssid.toCharArray(rec.ssid, sizeof(rec.ssid));
    pass.toCharArray(rec.password, sizeof(rec.password));
    writeSavedSlot(0, rec);
    prefs_.putUChar("wifi_cnt", 1);
    prefs_.putString("wifi_last", ssid);
    prefs_.remove("obd_ssid");
    prefs_.remove("obd_pass");
}

int AppStorage::savedWifiCount() {
    return static_cast<int>(prefs_.getUChar("wifi_cnt", 0));
}

void AppStorage::readSavedSlot(int index, SavedWifiRecord &out) {
    out = SavedWifiRecord{};
    if (index < 0 || index >= WIFI_SAVED_MAX) {
        return;
    }
    char key[12];
    snprintf(key, sizeof(key), "w%u_s", static_cast<unsigned>(index));
    const String ssid = prefs_.getString(key, "");
    snprintf(key, sizeof(key), "w%u_p", static_cast<unsigned>(index));
    const String pass = prefs_.getString(key, "");
    snprintf(key, sizeof(key), "w%u_i", static_cast<unsigned>(index));
    const String ip = prefs_.getString(key, "");
    snprintf(key, sizeof(key), "w%u_g", static_cast<unsigned>(index));
    const String gw = prefs_.getString(key, "");
    snprintf(key, sizeof(key), "w%u_o", static_cast<unsigned>(index));
    const bool openAp = prefs_.getBool(key, false);
    ssid.toCharArray(out.ssid, sizeof(out.ssid));
    pass.toCharArray(out.password, sizeof(out.password));
    ip.toCharArray(out.lastIp, sizeof(out.lastIp));
    gw.toCharArray(out.lastGw, sizeof(out.lastGw));
    out.openAp = openAp;
}

void AppStorage::writeSavedSlot(int index, const SavedWifiRecord &rec) {
    if (index < 0 || index >= WIFI_SAVED_MAX) {
        return;
    }
    char key[12];
    snprintf(key, sizeof(key), "w%u_s", static_cast<unsigned>(index));
    prefs_.putString(key, rec.ssid);
    snprintf(key, sizeof(key), "w%u_p", static_cast<unsigned>(index));
    prefs_.putString(key, rec.password);
    snprintf(key, sizeof(key), "w%u_i", static_cast<unsigned>(index));
    prefs_.putString(key, rec.lastIp);
    snprintf(key, sizeof(key), "w%u_g", static_cast<unsigned>(index));
    prefs_.putString(key, rec.lastGw);
    snprintf(key, sizeof(key), "w%u_o", static_cast<unsigned>(index));
    prefs_.putBool(key, rec.openAp);
}

int AppStorage::findSavedIndex(const char *ssid) {
    if (!ssid || !ssid[0]) {
        return -1;
    }
    const int cnt = prefs_.getUChar("wifi_cnt", 0);
    for (int i = 0; i < cnt; ++i) {
        SavedWifiRecord rec{};
        readSavedSlot(i, rec);
        if (strcmp(rec.ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

bool AppStorage::getSavedWifi(int index, SavedWifiRecord &out) {
    if (index < 0 || index >= savedWifiCount()) {
        return false;
    }
    readSavedSlot(index, out);
    return out.ssid[0] != '\0';
}

bool AppStorage::findSavedWifi(const char *ssid, SavedWifiRecord &out) {
    const int idx = findSavedIndex(ssid);
    if (idx < 0) {
        return false;
    }
    readSavedSlot(idx, out);
    return true;
}

void AppStorage::upsertSavedWifi(const char *ssid, const char *password, const char *lastIp,
                                 const char *lastGw, bool openAp) {
    if (!ssid || !ssid[0]) {
        return;
    }

    SavedWifiRecord rec{};
    strncpy(rec.ssid, ssid, sizeof(rec.ssid) - 1);
    rec.openAp = openAp;
    if (openAp) {
        rec.password[0] = '\0';
    } else if (password && password[0]) {
        strncpy(rec.password, password, sizeof(rec.password) - 1);
    }
    if (lastIp && lastIp[0]) {
        strncpy(rec.lastIp, lastIp, sizeof(rec.lastIp) - 1);
    }
    if (lastGw && lastGw[0]) {
        strncpy(rec.lastGw, lastGw, sizeof(rec.lastGw) - 1);
    }

    if (openAp) {
        rec.openAp = true;
        rec.password[0] = '\0';
    }

    int idx = findSavedIndex(ssid);
    if (idx >= 0) {
        SavedWifiRecord prev{};
        readSavedSlot(idx, prev);
        if (openAp) {
            rec.openAp = true;
            rec.password[0] = '\0';
        } else {
            if (!rec.password[0] && prev.password[0]) {
                strncpy(rec.password, prev.password, sizeof(rec.password) - 1);
            }
            rec.openAp = prev.openAp;
        }
        if (!rec.lastIp[0] && prev.lastIp[0]) {
            strncpy(rec.lastIp, prev.lastIp, sizeof(rec.lastIp) - 1);
        }
        if (!rec.lastGw[0] && prev.lastGw[0]) {
            strncpy(rec.lastGw, prev.lastGw, sizeof(rec.lastGw) - 1);
        }
        writeSavedSlot(idx, rec);
        setLastUsedWifiSsid(ssid);
        return;
    }

    int cnt = savedWifiCount();
    if (cnt >= WIFI_SAVED_MAX) {
        for (int i = 1; i < cnt; ++i) {
            SavedWifiRecord shift{};
            readSavedSlot(i, shift);
            writeSavedSlot(i - 1, shift);
        }
        cnt = WIFI_SAVED_MAX - 1;
        prefs_.putUChar("wifi_cnt", static_cast<uint8_t>(cnt));
    }

    writeSavedSlot(cnt, rec);
    prefs_.putUChar("wifi_cnt", static_cast<uint8_t>(cnt + 1));
    setLastUsedWifiSsid(ssid);
}

void AppStorage::removeSavedWifi(const char *ssid) {
    const int idx = findSavedIndex(ssid);
    if (idx < 0) {
        return;
    }
    int cnt = savedWifiCount();
    for (int i = idx + 1; i < cnt; ++i) {
        SavedWifiRecord rec{};
        readSavedSlot(i, rec);
        writeSavedSlot(i - 1, rec);
    }
    if (cnt > 0) {
        SavedWifiRecord empty{};
        writeSavedSlot(cnt - 1, empty);
        prefs_.putUChar("wifi_cnt", static_cast<uint8_t>(cnt - 1));
    }

    const String last = lastUsedWifiSsid();
    if (last == ssid) {
        prefs_.remove("wifi_last");
        if (savedWifiCount() > 0) {
            SavedWifiRecord first{};
            getSavedWifi(0, first);
            setLastUsedWifiSsid(first.ssid);
        }
    }
}

void AppStorage::clearAllSavedWifi() {
    const int cnt = savedWifiCount();
    SavedWifiRecord empty{};
    for (int i = 0; i < cnt; ++i) {
        writeSavedSlot(i, empty);
    }
    prefs_.putUChar("wifi_cnt", 0);
    prefs_.remove("wifi_last");
}

String AppStorage::lastUsedWifiSsid() {
    return prefs_.getString("wifi_last", "");
}

void AppStorage::setLastUsedWifiSsid(const char *ssid) {
    if (ssid && ssid[0]) {
        prefs_.putString("wifi_last", ssid);
    }
}

bool AppStorage::getObdWifi(String &ssid, String &password) {
    const String last = lastUsedWifiSsid();
    if (last.length() > 0) {
        SavedWifiRecord rec{};
        if (findSavedWifi(last.c_str(), rec)) {
            ssid = rec.ssid;
            password = rec.password;
            return true;
        }
    }
    if (savedWifiCount() > 0) {
        SavedWifiRecord rec{};
        if (getSavedWifi(0, rec)) {
            ssid = rec.ssid;
            password = rec.password;
            return true;
        }
    }
    return false;
}

void AppStorage::setObdWifi(const char *ssid, const char *password) {
    upsertSavedWifi(ssid, password, nullptr, nullptr);
}

void AppStorage::clearObdWifi() {
    clearAllSavedWifi();
}

NetMode AppStorage::netMode() {
    return static_cast<NetMode>(prefs_.getUChar("net_mode", 0));
}

void AppStorage::setNetMode(NetMode mode) {
    prefs_.putUChar("net_mode", static_cast<uint8_t>(mode));
}

bool AppStorage::isProvisioned() {
    return prefs_.getBool("provisioned", false) || savedWifiCount() > 0;
}

void AppStorage::setProvisioned(bool done) {
    prefs_.putBool("provisioned", done);
}

bool AppStorage::hasElmHost() {
    return prefs_.isKey("elm_host");
}

String AppStorage::elmHost() {
    if (hasElmHost()) {
        return prefs_.getString("elm_host", ELM327_HOST);
    }
    return ELM327_HOST;
}

void AppStorage::setElmHost(const char *host) {
    if (host && host[0]) {
        prefs_.putString("elm_host", host);
    }
}

bool AppStorage::hasLockedEndpoint() {
    return prefs_.isKey("elm_lock_host") && prefs_.isKey("elm_lock_port");
}

void AppStorage::getLockedEndpoint(String &host, uint16_t &port) {
    host = prefs_.getString("elm_lock_host", ELM327_HOST);
    port = static_cast<uint16_t>(prefs_.getUShort("elm_lock_port", 35000));
}

void AppStorage::setLockedEndpoint(const char *host, uint16_t port) {
    if (host && host[0]) {
        prefs_.putString("elm_lock_host", host);
        prefs_.putUShort("elm_lock_port", port);
    }
}

void AppStorage::clearLockedEndpoint() {
    prefs_.remove("elm_lock_host");
    prefs_.remove("elm_lock_port");
}

void AppStorage::clearAll() {
    prefs_.clear();
}
