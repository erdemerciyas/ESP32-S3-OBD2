/*
 * ESP32-S3 OBD2 Dashboard
 * Ana ekran: OBD | Cift dokun: WiFi | Uzun dokun: Arac (VIN/DTC)
 */

#include <Arduino.h>
#include "config.h"
#include "display_hal.h"
#include "wifi_manager.h"
#include "wifi_menu_ui.h"
#include "elm327_client.h"
#include "obd_service.h"
#include "obd_extras.h"
#include "dashboard_ui.h"
#include "vehicle_info_ui.h"
#include "splash_ui.h"
#include "app_logger.h"
#include <cstring>

static WifiManager wifi;
static WifiMenuUi wifiMenu;
static Elm327Client elm;
static ObdService obd;
static ObdExtras obdExtras;
static DashboardUi dashboard;
static VehicleInfoUi vehicleInfo;
static SplashUi splash;

static uint32_t bootStartMs = 0;
static bool homeScreenActive = false;
static bool nvsLoggedDelayed = false;
static uint32_t lastUiMs = 0;
static uint32_t lastMenuUiMs = 0;
static uint32_t lastVehicleUiMs = 0;
static bool obdPipelineStarted = false;
static char lastElmHost_[40] = {};
static char lastWifiSsid_[33] = {};
static uint32_t wifiLinkUpMs_ = 0;
static String serialCmdBuf_;

static void pollSerialCommands() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            serialCmdBuf_.trim();
            if (serialCmdBuf_.length() == 0) {
                continue;
            }
            if (serialCmdBuf_.equalsIgnoreCase("log dump")) {
                AppLogger::dumpToSerial();
            } else if (serialCmdBuf_.equalsIgnoreCase("log clear")) {
                AppLogger::clear();
            } else if (serialCmdBuf_.equalsIgnoreCase("log stat")) {
                AppLogger::printStats();
            }
            serialCmdBuf_ = "";
        } else if (serialCmdBuf_.length() < 48) {
            serialCmdBuf_ += c;
        }
    }
}

static void startObdPipeline() {
    const char *host = wifi.elmHost();
    if (!host || !host[0]) {
        host = ELM327_HOST;
    }

    if (obdPipelineStarted && strncmp(lastElmHost_, host, sizeof(lastElmHost_)) == 0) {
        return;
    }

    strncpy(lastElmHost_, host, sizeof(lastElmHost_) - 1);
    lastElmHost_[sizeof(lastElmHost_) - 1] = '\0';

    elm.begin(lastElmHost_);
    obd.begin(&elm);
    obdExtras.begin(&elm);
    obdPipelineStarted = true;
    OBD_LOG("[OBD] ELM327 TCP basladi (hedef %s)", lastElmHost_);
}

static void stopObdPipeline() {
    elm.reset();
    obdPipelineStarted = false;
    lastElmHost_[0] = '\0';
}

static void refreshDashboard() {
    if (!displayHalLvglLock(10)) {
        return;
    }
    dashboard.update(obd.snapshot(), wifi, elm, obd.lastPidName());
    displayHalLvglUnlock();
}

static void openWifiMenu() {
    if (!displayHalLvglLock(50)) {
        return;
    }
    wifi.setMenuActive(true);
    wifiMenu.show(wifi);
    wifiMenu.tick(wifi);
    if (!wifi.isScanning() && !wifi.hasScanResults()) {
        wifi.startScan();
    }
    displayHalLvglUnlock();
}

static void showDashboardHome() {
    dashboard.show();
}

static void openVehicleInfo() {
    if (!displayHalLvglLock(50)) {
        return;
    }
    vehicleInfo.show(obdExtras);
    vehicleInfo.tick(obdExtras);
    displayHalLvglUnlock();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    AppLogger::begin();
    OBD_LOG("\n=== ESP32-S3 OBD Dashboard ===");

    if (!displayHalInit()) {
        while (true) {
            delay(1000);
        }
    }

    if (displayHalLvglLock(-1)) {
        dashboard.create();
        wifiMenu.create();
        vehicleInfo.create();
        splash.create();
        wifiMenu.setNavHomeCallback(showDashboardHome);
        vehicleInfo.setNavHomeCallback(showDashboardHome);
        splash.show();
        bootStartMs = millis();
        displayHalLvglUnlock();
    }

    wifi.begin();

    if (wifi.isConnected()) {
        startObdPipeline();
    }

    OBD_LOG("Cift dokun: WiFi | Uzun dokun: Arac (VIN/DTC)");
    OBD_LOG("[LOG] Kalici log aktif — komutlar: log dump | log stat | log clear");
}

void loop() {
    const uint32_t now = millis();
    const bool menuOpen = homeScreenActive && wifiMenu.isVisible();
    const bool vehicleOpen = homeScreenActive && vehicleInfo.isVisible();
    const bool overlayOpen = menuOpen || vehicleOpen;

    pollSerialCommands();
    AppLogger::tick(now);
    wifi.loop();

    if (!nvsLoggedDelayed && now > 4000) {
        nvsLoggedDelayed = true;
        wifi.logStoredNetworks();
    }

    if (wifi.consumeLinkUpEvent()) {
        stopObdPipeline();
        wifiLinkUpMs_ = now;
        OBD_LOG("[OBD] WiFi baglandi — ELM TCP %u sn icinde baslar",
                static_cast<unsigned>(ELM_TCP_SETTLE_MS / 1000));
    } else if (wifi.isConnected() && wifiLinkUpMs_ == 0 && !obdPipelineStarted) {
        wifiLinkUpMs_ = now;
    }

    if (wifi.isConnected()) {
        const char *curSsid = wifi.connectedSsid();
        if (curSsid[0] && lastWifiSsid_[0] &&
            strncmp(lastWifiSsid_, curSsid, sizeof(lastWifiSsid_)) != 0) {
            if (obdPipelineStarted) {
                stopObdPipeline();
            }
            wifiLinkUpMs_ = now;
        }
        if (curSsid[0]) {
            strncpy(lastWifiSsid_, curSsid, sizeof(lastWifiSsid_) - 1);
            lastWifiSsid_[sizeof(lastWifiSsid_) - 1] = '\0';
        }

        const char *elmTarget = wifi.elmHost();
        if (elmTarget[0] && obdPipelineStarted &&
            strncmp(lastElmHost_, elmTarget, sizeof(lastElmHost_)) != 0) {
            stopObdPipeline();
            wifiLinkUpMs_ = now;
        }

        const bool elmSettled =
            wifiLinkUpMs_ == 0 || (now - wifiLinkUpMs_ >= ELM_TCP_SETTLE_MS);
        if (!obdPipelineStarted && elmSettled) {
            startObdPipeline();
        }

        if (obdPipelineStarted) {
            elm.loop();
        }
        if (obdPipelineStarted && elm.isReady()) {
            if (vehicleOpen || obdExtras.isBusy()) {
                obdExtras.loop();
            } else {
                obd.loop();
            }
        }
    } else if (!overlayOpen) {
        wifiLinkUpMs_ = 0;
        stopObdPipeline();
    }

    const int lockMs = overlayOpen ? 15 : 5;
    if (homeScreenActive && displayHalLvglLock(lockMs)) {
        if (!overlayOpen && dashboard.consumeOpenWifiMenuRequest()) {
            displayHalLvglUnlock();
            openWifiMenu();
        } else if (!overlayOpen && dashboard.consumeOpenVehicleInfoRequest()) {
            displayHalLvglUnlock();
            openVehicleInfo();
        } else {
            if (menuOpen && now - lastMenuUiMs >= WIFI_MENU_TICK_MS) {
                lastMenuUiMs = now;
                wifiMenu.tick(wifi);
            }
            if (vehicleOpen && now - lastVehicleUiMs >= WIFI_MENU_TICK_MS) {
                lastVehicleUiMs = now;
                vehicleInfo.tick(obdExtras);
            }

            displayHalLvglUnlock();
        }
    }

    if (!homeScreenActive) {
        if (now - bootStartMs >= BOOT_SPLASH_MS) {
            if (displayHalLvglLock(50)) {
                splash.hide();
                dashboard.show();
                homeScreenActive = true;
                displayHalLvglUnlock();
            }
        }
    } else {
        const uint32_t uiInterval =
            (wifi.isConnected() && obdPipelineStarted && !elm.isReady())
                ? UI_REFRESH_PENDING_MS
                : UI_REFRESH_MS;
        if (now - lastUiMs >= uiInterval) {
            lastUiMs = now;
            if (!overlayOpen) {
                refreshDashboard();
            }
        }
    }

    yield();
}
