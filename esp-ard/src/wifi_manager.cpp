#include "wifi_manager.h"
#include "app_storage.h"
#include "app_logger.h"
#include "obd_wifi_profiles.h"
#include "config.h"
#include <cstring>

static AppStorage g_storage;

namespace {

bool ssidLooksLikeObd(const char *ssid) {
    if (!ssid || !ssid[0]) {
        return false;
    }
    char upper[33];
    strncpy(upper, ssid, sizeof(upper) - 1);
    upper[sizeof(upper) - 1] = '\0';
    for (char *p = upper; *p; ++p) {
        if (*p >= 'a' && *p <= 'z') {
            *p = static_cast<char>(*p - 32);
        }
    }
    return strstr(upper, "OBD") != nullptr || strstr(upper, "ELM") != nullptr ||
           strstr(upper, "VLINK") != nullptr || strstr(upper, "V-LINK") != nullptr;
}

int scanIndexForSsid(const WifiManager &wifi, const char *ssid) {
    if (!ssid || !ssid[0]) {
        return -1;
    }
    for (int i = 0; i < wifi.scanCount(); ++i) {
        if (strcmp(wifi.scanEntry(i).ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

const char *wifiStatusName(wl_status_t st) {
    switch (st) {
    case WL_NO_SSID_AVAIL:
        return "SSID yok (adaptör kapali/uzakta?)";
    case WL_CONNECT_FAILED:
        return "baglanti reddedildi";
    case WL_CONNECTION_LOST:
        return "baglanti koptu";
    case WL_DISCONNECTED:
        return "kopuk";
    case WL_CONNECTED:
        return "bagli";
    case WL_IDLE_STATUS:
        return "beklemede";
    default:
        return "bilinmiyor";
    }
}

bool applyObdStaticConfig(const char *ssid) {
    if (!ssid || !ssid[0]) {
        return false;
    }
    if (!ssidLooksLikeObd(ssid) && !isKnownObdSsid(ssid)) {
        return false;
    }
    SavedWifiRecord rec{};
    if (!g_storage.findSavedWifi(ssid, rec)) {
        return false;
    }
    IPAddress ip(192, 168, 0, 11);
    IPAddress gw(192, 168, 0, 10);
    IPAddress mask(255, 255, 255, 0);
    if (rec.lastIp[0]) {
        ip.fromString(rec.lastIp);
    }
    if (rec.lastGw[0]) {
        gw.fromString(rec.lastGw);
    }
    const bool ok = WiFi.config(ip, gw, mask, gw);
    if (ok) {
        OBD_LOG("[WiFi] Statik IP %s GW %s", ip.toString().c_str(),
                gw.toString().c_str());
    }
    return ok;
}

void formatAddrHint(const SavedWifiRecord &rec, char *out, size_t outLen) {
    if (!out || outLen == 0) {
        return;
    }
    out[0] = '\0';
    if (rec.lastIp[0] && rec.lastGw[0]) {
        snprintf(out, outLen, "%s · GW %s", rec.lastIp, rec.lastGw);
    } else if (rec.lastIp[0]) {
        snprintf(out, outLen, "IP %s", rec.lastIp);
    } else if (rec.lastGw[0]) {
        snprintf(out, outLen, "GW %s", rec.lastGw);
    }
}

} // namespace

bool WifiManager::lookupObdPassword(const char *ssid, char *out, size_t outLen) {
    if (!ssid || !ssid[0] || !out || outLen == 0) {
        return false;
    }
    SavedWifiRecord rec{};
    if (g_storage.findSavedWifi(ssid, rec) && rec.password[0]) {
        strncpy(out, rec.password, outLen - 1);
        out[outLen - 1] = '\0';
        return true;
    }
    for (size_t i = 0; i < OBD_WIFI_PROFILE_COUNT; ++i) {
        if (strcmp(ssid, OBD_WIFI_PROFILES[i].ssid) == 0) {
            strncpy(out, OBD_WIFI_PROFILES[i].password, outLen - 1);
            out[outLen - 1] = '\0';
            return out[0] != '\0';
        }
    }
    return false;
}

int WifiManager::savedCount() const {
    return g_storage.savedWifiCount();
}

void WifiManager::ensureDisplayList() {
    if (displayDirty_) {
        rebuildDisplayList();
    }
}

void WifiManager::rebuildDisplayList() {
    displayDirty_ = false;
    displayCount_ = 0;
    auto addDisplay = [this](const WifiDisplayEntry &e) {
        if (displayCount_ >= WIFI_DISPLAY_MAX) {
            return;
        }
        for (int i = 0; i < displayCount_; ++i) {
            if (strcmp(displayList_[i].ssid, e.ssid) == 0) {
                if (e.inRange && displayList_[i].rssi < e.rssi) {
                    displayList_[i].rssi = e.rssi;
                    displayList_[i].inRange = true;
                    displayList_[i].open = e.open;
                }
                if (e.active) {
                    displayList_[i].active = true;
                }
                if (e.addrHint[0]) {
                    strncpy(displayList_[i].addrHint, e.addrHint,
                            sizeof(displayList_[i].addrHint) - 1);
                }
                return;
            }
        }
        displayList_[displayCount_++] = e;
    };

    const int savedN = g_storage.savedWifiCount();
    const bool linked = (state_ == WifiConnState::Connected && savedSsid_[0]);

    if (savedN > 0) {
        WifiDisplayEntry hdr{};
        strncpy(hdr.ssid, "--- Kayitli aglar ---", sizeof(hdr.ssid) - 1);
        addDisplay(hdr);
    }

    for (int s = 0; s < savedN; ++s) {
        SavedWifiRecord rec{};
        if (!g_storage.getSavedWifi(s, rec)) {
            continue;
        }
        WifiDisplayEntry e{};
        strncpy(e.ssid, rec.ssid, sizeof(e.ssid) - 1);
        e.saved = true;
        formatAddrHint(rec, e.addrHint, sizeof(e.addrHint));

        const int scanIdx = scanIndexForSsid(*this, rec.ssid);
        if (scanIdx >= 0) {
            const auto &sc = scanEntry(scanIdx);
            e.rssi = sc.rssi;
            e.open = sc.open;
            e.inRange = true;
        } else {
            e.rssi = -127;
            e.inRange = false;
        }

        if (linked && strcmp(rec.ssid, savedSsid_) == 0) {
            e.active = true;
            if (WiFi.status() == WL_CONNECTED) {
                const String ip = WiFi.localIP().toString();
                const String gw = WiFi.gatewayIP().toString();
                if (ip != "0.0.0.0" && gw != "0.0.0.0") {
                    snprintf(e.addrHint, sizeof(e.addrHint), "%s · GW %s", ip.c_str(),
                             gw.c_str());
                }
            }
        }
        addDisplay(e);
    }

    bool hasScan = scanCount_ > 0;
    for (int i = 0; i < scanCount_; ++i) {
        const auto &sc = scanEntry(i);
        bool already = false;
        for (int d = 0; d < displayCount_; ++d) {
            if (strcmp(displayList_[d].ssid, sc.ssid) == 0) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        if (hasScan) {
            hasScan = false;
            WifiDisplayEntry hdr{};
            strncpy(hdr.ssid, "--- Tarama ---", sizeof(hdr.ssid) - 1);
            addDisplay(hdr);
        }
        WifiDisplayEntry e{};
        strncpy(e.ssid, sc.ssid, sizeof(e.ssid) - 1);
        e.rssi = sc.rssi;
        e.open = sc.open;
        e.inRange = true;
        snprintf(e.addrHint, sizeof(e.addrHint), "%d dBm", sc.rssi);
        addDisplay(e);
    }

    if (displayCount_ == 0 && !isScanning()) {
        WifiDisplayEntry e{};
        strncpy(e.ssid, "(Ag yok — Tara)", sizeof(e.ssid) - 1);
        addDisplay(e);
    }
}

const WifiDisplayEntry &WifiManager::displayEntry(int index) const {
    static WifiDisplayEntry empty = {};
    if (index < 0 || index >= displayCount_) {
        return empty;
    }
    return displayList_[index];
}

bool WifiManager::startConnectSaved(int savedIndex) {
    SavedWifiRecord rec{};
    if (!g_storage.getSavedWifi(savedIndex, rec)) {
        return false;
    }
    return startConnect(rec.ssid, rec.password);
}

void WifiManager::logStoredNetworks() const {
    const int n = g_storage.savedWifiCount();
    OBD_LOG("[NVS] obd_dash: %d kayitli WiFi\n", n);
    const String last = g_storage.lastUsedWifiSsid();
    if (last.length() > 0) {
        OBD_LOG("[NVS] Son kullanilan: %s\n", last.c_str());
    }
    for (int i = 0; i < n; ++i) {
        SavedWifiRecord rec{};
        if (!g_storage.getSavedWifi(i, rec)) {
            continue;
        }
        OBD_LOG("[NVS] [%d] SSID=%s %s IP=%s GW=%s\n", i, rec.ssid,
                      rec.openAp ? "acik (sifresiz)"
                                   : (rec.password[0] ? "sifreli" : "sifre yok"),
                      rec.lastIp[0] ? rec.lastIp : "-",
                      rec.lastGw[0] ? rec.lastGw : "-");
    }
    OBD_LOG("[NVS] elm_host=%s\n", elmHost_[0] ? elmHost_ : "-");
    if (g_storage.hasLockedEndpoint()) {
        String lh;
        uint16_t lp = 0;
        g_storage.getLockedEndpoint(lh, lp);
        OBD_LOG("[NVS] elm_lock=%s:%u\n", lh.c_str(),
                      static_cast<unsigned>(lp));
    } else {
        OBD_LOG("[NVS] elm_lock=yok");
    }
}

void WifiManager::begin() {
    g_storage.begin();
    g_storage.clearLockedEndpoint();
    g_storage.elmHost().toCharArray(elmHost_, sizeof(elmHost_));
    logStoredNetworks();

    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname("obd-dashboard");
    WiFi.disconnect(true);
    delay(20);

#if defined(OBD_WIFI_SSID) && defined(OBD_WIFI_PASSWORD)
    OBD_LOG("[WiFi] config.h agi deneniyor...");
    startConnect(OBD_WIFI_SSID, OBD_WIFI_PASSWORD);
    return;
#endif

    OBD_LOG("[WiFi] Kayitli aglar deneniyor...");
    tryLoadAndConnect();
}

void WifiManager::tryLoadAndConnect() {
    const int n = g_storage.savedWifiCount();
    if (n <= 0) {
        state_ = WifiConnState::Idle;
        autoProvision_ = true;
        lastAutoScanMs_ = 0;
        OBD_LOG("[WiFi] Kayit yok — OBD aglari taraniyor");
        startScan();
        return;
    }

    String tryOrder[WIFI_SAVED_MAX];
    int orderN = 0;
    const String last = g_storage.lastUsedWifiSsid();
    if (last.length() > 0) {
        tryOrder[orderN++] = last;
    }
    for (int i = 0; i < n && orderN < WIFI_SAVED_MAX; ++i) {
        SavedWifiRecord rec{};
        if (!g_storage.getSavedWifi(i, rec)) {
            continue;
        }
        bool dup = false;
        for (int j = 0; j < orderN; ++j) {
            if (tryOrder[j] == rec.ssid) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            tryOrder[orderN++] = rec.ssid;
        }
    }

    const char *firstSsid = orderN > 0 ? tryOrder[0].c_str() : nullptr;
    SavedWifiRecord firstRec{};
    if (firstSsid && g_storage.findSavedWifi(firstSsid, firstRec)) {
        OBD_LOG("[WiFi] Kayitli ag once dogrudan: %s", firstSsid);
        autoProvision_ = true;
        lastAutoScanMs_ = millis();
        startConnect(firstSsid, firstRec.password);
        return;
    }

    OBD_LOG("[WiFi] Kayitli ag gorunene kadar tarama (son: %s)",
            firstSsid ? firstSsid : "?");
    autoProvision_ = true;
    lastAutoScanMs_ = 0;
    state_ = WifiConnState::Idle;
    startScan();
}

void WifiManager::tryDirectSavedConnect(const char *ssid) {
    if (!ssid || !ssid[0] || state_ == WifiConnState::Connecting ||
        state_ == WifiConnState::Connected) {
        return;
    }
    if (millis() - lastDirectConnectMs_ < 12000) {
        return;
    }
    SavedWifiRecord rec{};
    if (!g_storage.findSavedWifi(ssid, rec)) {
        return;
    }
    lastDirectConnectMs_ = millis();
    OBD_LOG("[WiFi] Taramada yok — dogrudan deneniyor: %s", ssid);
    autoProvision_ = false;
    startConnect(ssid, rec.password);
}

void WifiManager::tryAutoProvision() {
    if (!autoProvision_ || scanCount_ <= 0) {
        return;
    }
    if (state_ == WifiConnState::Connecting || state_ == WifiConnState::Connected) {
        return;
    }

    int bestIdx = -1;
    int8_t bestRssi = -127;
    char pass[64] = {};

    for (int s = 0; s < g_storage.savedWifiCount(); ++s) {
        SavedWifiRecord rec{};
        if (!g_storage.getSavedWifi(s, rec)) {
            continue;
        }
        const int idx = scanIndexForSsid(*this, rec.ssid);
        if (idx < 0) {
            continue;
        }
        if (scanList_[idx].rssi > bestRssi) {
            bestRssi = scanList_[idx].rssi;
            bestIdx = idx;
            strncpy(pass, rec.password, sizeof(pass) - 1);
            pass[sizeof(pass) - 1] = '\0';
        }
    }

    const String lastUsed = g_storage.lastUsedWifiSsid();
    if (lastUsed.length() > 0) {
        const int lastIdx = scanIndexForSsid(*this, lastUsed.c_str());
        if (lastIdx >= 0) {
            bestIdx = lastIdx;
            SavedWifiRecord rec{};
            if (g_storage.findSavedWifi(lastUsed.c_str(), rec)) {
                strncpy(pass, rec.password, sizeof(pass) - 1);
                pass[sizeof(pass) - 1] = '\0';
            }
            OBD_LOG("[WiFi] Son kullanilan goruldu: %s (RSSI %d)",
                    scanList_[bestIdx].ssid, scanList_[bestIdx].rssi);
        }
    }

    if (bestIdx >= 0) {
        OBD_LOG("[WiFi] Kayitli ag (tarama): %s\n", scanList_[bestIdx].ssid);
        autoProvision_ = false;
        startConnect(scanList_[bestIdx].ssid, pass);
        return;
    }

    for (int i = 0; i < scanCount_; ++i) {
        const char *ssid = scanList_[i].ssid;
        if (!isKnownObdSsid(ssid) && !ssidLooksLikeObd(ssid)) {
            continue;
        }
        if (!lookupObdPassword(ssid, pass, sizeof(pass))) {
            if (!scanList_[i].open) {
                continue;
            }
            pass[0] = '\0';
        }
        if (scanList_[i].rssi > bestRssi) {
            bestRssi = scanList_[i].rssi;
            bestIdx = i;
        }
    }

    if (bestIdx < 0) {
        return;
    }

    const char *ssid = scanList_[bestIdx].ssid;
    if (!lookupObdPassword(ssid, pass, sizeof(pass)) && !scanList_[bestIdx].open) {
        return;
    }

    OBD_LOG("[WiFi] Otomatik: %s (RSSI %d)\n", ssid, scanList_[bestIdx].rssi);
    autoProvision_ = false;
    startConnect(ssid, pass);
}

void WifiManager::startScan() {
    if (state_ == WifiConnState::Connected) {
        OBD_LOG("[WiFi] Bagliyken tarama atlandi (OBD kopar)");
        return;
    }
    if (state_ == WifiConnState::Connecting) {
        cancelConnect();
    }
    state_ = WifiConnState::Scanning;
    scanCount_ = 0;
    displayCount_ = 0;
    OBD_LOG("[WiFi] Tarama...");
    WiFi.scanNetworks(true, true);
}

void WifiManager::cancelConnect() {
    if (state_ != WifiConnState::Connecting) {
        return;
    }
    WiFi.disconnect(true);
    delay(20);
    state_ = WifiConnState::Idle;
    OBD_LOG("[WiFi] Baglanti iptal");
}

bool WifiManager::isSavedSsid(const char *ssid) const {
    if (!ssid || !ssid[0]) {
        return false;
    }
    SavedWifiRecord rec{};
    return g_storage.findSavedWifi(ssid, rec);
}

bool WifiManager::savedNetworkHint(const char *ssid, char *out,
                                 size_t outLen) const {
    if (!ssid || !out || outLen == 0) {
        return false;
    }
    SavedWifiRecord rec{};
    if (!g_storage.findSavedWifi(ssid, rec)) {
        return false;
    }
    formatAddrHint(rec, out, outLen);
    return out[0] != '\0';
}

bool WifiManager::getSavedPasswordFor(const char *ssid, char *out,
                                    size_t outLen) const {
    if (!ssid || !ssid[0] || !out || outLen == 0) {
        return false;
    }
    SavedWifiRecord rec{};
    if (!g_storage.findSavedWifi(ssid, rec)) {
        return false;
    }
    strncpy(out, rec.password, outLen - 1);
    out[outLen - 1] = '\0';
    return true;
}

void WifiManager::forgetSavedFor(const char *ssid) {
    if (!ssid || !ssid[0]) {
        return;
    }
    if (!isSavedSsid(ssid)) {
        return;
    }
    if (state_ == WifiConnState::Connecting &&
        strncmp(pendingSsid_, ssid, sizeof(pendingSsid_)) == 0) {
        cancelConnect();
    }
    if (state_ == WifiConnState::Connected && strcmp(savedSsid_, ssid) == 0) {
        WiFi.disconnect(true);
        state_ = WifiConnState::Idle;
        savedSsid_[0] = '\0';
    }
    g_storage.removeSavedWifi(ssid);
    if (g_storage.savedWifiCount() == 0) {
        g_storage.setProvisioned(false);
        g_storage.clearLockedEndpoint();
        autoProvision_ = true;
        lastAutoScanMs_ = 0;
    }
    markDisplayDirty();
    OBD_LOG("[WiFi] Unutuldu: %s (kalan %d)\n", ssid, g_storage.savedWifiCount());
}

void WifiManager::forgetSaved() {
    if (state_ == WifiConnState::Connected && savedSsid_[0]) {
        forgetSavedFor(savedSsid_);
        return;
    }
    g_storage.clearAllSavedWifi();
    g_storage.setProvisioned(false);
    g_storage.clearLockedEndpoint();
    savedSsid_[0] = '\0';
    savedPass_[0] = '\0';
    WiFi.disconnect(true);
    state_ = WifiConnState::Idle;
    autoProvision_ = true;
    lastAutoScanMs_ = 0;
    OBD_LOG("[WiFi] Tum kayitli aglar silindi");
}

bool WifiManager::startConnect(const char *ssid, const char *password) {
    if (!ssid || !ssid[0]) {
        return false;
    }

    strncpy(pendingSsid_, ssid, sizeof(pendingSsid_) - 1);
    pendingSsid_[sizeof(pendingSsid_) - 1] = '\0';
    if (password && password[0]) {
        strncpy(pendingPass_, password, sizeof(pendingPass_));
        pendingPass_[sizeof(pendingPass_) - 1] = '\0';
    } else {
        pendingPass_[0] = '\0';
    }

    OBD_LOG("[WiFi] Baglaniyor: [%s]\n", pendingSsid_);

    const int scanSt = WiFi.scanComplete();
    if (scanSt >= 0) {
        WiFi.scanDelete();
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false);
    delay(30);

    applyObdStaticConfig(pendingSsid_);

    if (pendingPass_[0]) {
        WiFi.begin(pendingSsid_, pendingPass_);
    } else {
        WiFi.begin(pendingSsid_);
    }

    state_ = WifiConnState::Connecting;
    connectStartMs_ = millis();
    return true;
}

void WifiManager::onConnectSuccess() {
    state_ = WifiConnState::Connected;
    autoProvision_ = false;
    strncpy(savedSsid_, pendingSsid_, sizeof(savedSsid_) - 1);
    strncpy(savedPass_, pendingPass_, sizeof(savedPass_) - 1);

    const String ip = WiFi.localIP().toString();
    const String gw = WiFi.gatewayIP().toString();
    const char *ipC = (ip != "0.0.0.0") ? ip.c_str() : "";
    const char *gwC = (gw != "0.0.0.0") ? gw.c_str() : "";

    const bool openNet = (pendingPass_[0] == '\0') ||
                         ssidLooksLikeObd(savedSsid_) || isKnownObdSsid(savedSsid_);
    g_storage.upsertSavedWifi(savedSsid_, savedPass_, ipC, gwC, openNet);
    g_storage.setProvisioned(true);
    markDisplayDirty();

    SavedWifiRecord rec{};
    if (g_storage.findSavedWifi(savedSsid_, rec) && rec.lastGw[0]) {
        strncpy(elmHost_, rec.lastGw, sizeof(elmHost_) - 1);
    } else if (gwC[0]) {
        strncpy(elmHost_, gwC, sizeof(elmHost_) - 1);
    }
    if (elmHost_[0]) {
        g_storage.setElmHost(elmHost_);
    }

    linkUpEvent_ = true;
    OBD_LOG("[WiFi] OK: %s IP %s GW %s ELM=%s (%d kayitli ag)\n", savedSsid_,
                  ipC, gwC, elmHost_[0] ? elmHost_ : "?", g_storage.savedWifiCount());
}

void WifiManager::onConnectFailed() {
    const wl_status_t st = WiFi.status();
    state_ = WifiConnState::Idle;
    lastFailMs_ = millis();
    autoProvision_ = true;
    lastAutoScanMs_ = millis() - OBD_AUTO_RESCAN_MS + WIFI_FAIL_RESCAN_MS;
    OBD_LOG("[WiFi] Basarisiz: %s (kod=%d) — %u sn sonra tarama",
            wifiStatusName(st), static_cast<int>(st),
            static_cast<unsigned>(WIFI_FAIL_RESCAN_MS / 1000));
    WiFi.disconnect(false);
    delay(20);
}

void WifiManager::loop() {
    if (state_ == WifiConnState::Scanning) {
        const int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            return;
        }
        if (n == WIFI_SCAN_FAILED) {
            state_ = WifiConnState::Idle;
            scanCount_ = 0;
            OBD_LOG("[WiFi] Tarama hatasi");
            rebuildDisplayList();
            return;
        }

        scanCount_ = min(n, static_cast<int>(WIFI_SCAN_MAX));
        for (int i = 0; i < scanCount_; ++i) {
            const String s = WiFi.SSID(i);
            s.toCharArray(scanList_[i].ssid, sizeof(scanList_[i].ssid));
            scanList_[i].rssi = static_cast<int8_t>(WiFi.RSSI(i));
            const auto mode = WiFi.encryptionType(i);
            scanList_[i].open = (mode == WIFI_AUTH_OPEN);
        }
        WiFi.scanDelete();
        state_ = WifiConnState::Idle;
        OBD_LOG("[WiFi] %d ag bulundu", scanCount_);
        if (autoProvision_ && g_storage.savedWifiCount() > 0) {
            const String last = g_storage.lastUsedWifiSsid();
            if (last.length() > 0 && scanIndexForSsid(*this, last.c_str()) < 0) {
                OBD_LOG("[WiFi] '%s' gorunmuyor — OBD adaptör acik mi?",
                        last.c_str());
                if (!menuActive_) {
                    tryDirectSavedConnect(last.c_str());
                }
            }
        }
        markDisplayDirty();
        rebuildDisplayList();
        if (!menuActive_ && state_ != WifiConnState::Connecting) {
            tryAutoProvision();
        }
        return;
    }

    if (state_ == WifiConnState::Connecting) {
        const wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            onConnectSuccess();
            markDisplayDirty();
            rebuildDisplayList();
            return;
        }
        if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
            onConnectFailed();
            return;
        }
        if (millis() - connectStartMs_ > WIFI_CONNECT_TIMEOUT_MS) {
            onConnectFailed();
        }
        return;
    }

    if (state_ == WifiConnState::Connected && WiFi.status() != WL_CONNECTED) {
        OBD_LOG("[WiFi] Kopma");
        state_ = WifiConnState::Idle;
        autoProvision_ = true;
        lastAutoScanMs_ = millis();
        markDisplayDirty();
        rebuildDisplayList();
    }

    if (!menuActive_ && autoProvision_ && state_ != WifiConnState::Scanning &&
        state_ != WifiConnState::Connecting && !isConnected()) {
        const uint32_t sinceFail = millis() - lastFailMs_;
        const uint32_t interval =
            (sinceFail < 60000) ? WIFI_FAIL_RESCAN_MS : OBD_AUTO_RESCAN_MS;
        if (millis() - lastAutoScanMs_ >= interval) {
            lastAutoScanMs_ = millis();
            OBD_LOG("[WiFi] Otomatik tarama...");
            startScan();
        }
    }
}

const WifiScanEntry &WifiManager::scanEntry(int index) const {
    static WifiScanEntry empty = {};
    if (index < 0 || index >= scanCount_) {
        return empty;
    }
    return scanList_[index];
}

const char *WifiManager::statusText() const {
    if (state_ == WifiConnState::Connected && savedSsid_[0]) {
        static char buf[64];
        snprintf(buf, sizeof(buf), "Bağlı: %s", savedSsid_);
        return buf;
    }
    switch (state_) {
    case WifiConnState::Scanning:
        return "Taranıyor...";
    case WifiConnState::Connecting:
        return "Bağlanıyor...";
    case WifiConnState::Connected:
        return "Bağlı";
    case WifiConnState::Failed:
        return "Bağlantı hatası";
    default:
        return autoProvision_ ? "OBD aranıyor..." : "Ağ seçin";
    }
}
