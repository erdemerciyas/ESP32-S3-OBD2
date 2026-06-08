#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "pin_config.h"
#include "display.h"
#include "elm327.h"
#include "obd2.h"
#include "ui.h"
#include "web_server.h"

ELM327 elm327;

OBD2Data obd_data[20] = {};

TaskHandle_t obdTaskHandle = NULL;
TaskHandle_t lvglTaskHandle = NULL;

static uint32_t last_reconnect_attempt = 0;
static uint32_t reconnect_count = 0;
static uint32_t last_rescan_attempt = 0;

// ═══════════════════════════════════════════════════════════
//  OBD2 polling task — Core 1 (NEVER GIVE UP)
// ═══════════════════════════════════════════════════════════
void obd_task(void* param) {
    uint8_t pid_index = 0;

    // Priority: RPM, Speed, Coolant, Load, Throttle first
    const uint8_t priority_pids[] = { 0, 1, 2, 4, 5, 3, 6, 10, 8, 9, 12, 13, 14, 11, 16, 17, 15, 19, 7, 18 };
    const int priority_count = sizeof(priority_pids) / sizeof(priority_pids[0]);

    // Wait a moment for tasks to settle
    vTaskDelay(pdMS_TO_TICKS(500));

    for (;;) {
        // ── Web request: manually rescan WiFi ──
        if (WebServer_PollRescan()) {
            Serial.println("[OBD] Web requested WiFi rescan...");
            elm327.scanAndDetect(1);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // ── Web request: manual SSID select ──
        char newSsid[64];
        if (WebServer_PollRepick(newSsid, sizeof(newSsid))) {
            Serial.printf("[OBD] Web selected SSID: %s\n", newSsid);
            strncpy(elm327.detectedSSID, newSsid, sizeof(elm327.detectedSSID) - 1);
            elm327.detectedSSID[sizeof(elm327.detectedSSID) - 1] = '\0';
            if (elm327.client.connected()) elm327.client.stop();
            WiFi.disconnect(true);
            delay(500);
            elm327.reconnect();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!elm327.isConnected()) {
            // Show current status on screen
            UI_SetStatus(elm327.statusText);

            // Reconnect every RECONNECT_INTERVAL_MS (3 seconds)
            if (millis() - last_reconnect_attempt > RECONNECT_INTERVAL_MS) {
                last_reconnect_attempt = millis();
                reconnect_count++;

                Serial.printf("[OBD] Reconnect attempt #%d\n", reconnect_count);

                if (elm327.reconnect()) {
                    // SUCCESS
                    reconnect_count = 0;
                    pid_index = 0;
                    char status[80];
                    snprintf(status, sizeof(status), "%s @ %s:%d",
                             elm327.detectedSSID, elm327.detectedIP, elm327.detectedPort);
                    UI_SetStatus(status);
                    Serial.printf("[OBD] CONNECTED: %s\n", status);
                }
                // NO restart - keep trying forever
            }

            // Periodically re-scan for ELM327 (every 30 seconds if still failing)
            if (reconnect_count > 0 && (reconnect_count % 10 == 0) &&
                millis() - last_rescan_attempt > 30000) {
                last_rescan_attempt = millis();
                Serial.println("[OBD] Re-scanning WiFi for ELM327...");
                elm327.scanAndDetect(2);
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint8_t table_idx = priority_pids[pid_index % priority_count];
        const PIDQuery& pid = PID_TABLE[table_idx];

        if (elm327.sendPIDRequest(pid.request, 800)) {
            float value = decodePIDValue(pid.pid, elm327.getResponseData());
            if (value > -998.0f) {
                obd_data[table_idx].value = value;
                obd_data[table_idx].valid = true;
                obd_data[table_idx].lastUpdate = millis();
            }
        }

        pid_index++;
        vTaskDelay(pdMS_TO_TICKS(OBD2_POLL_INTERVAL_MS / priority_count));
    }
}

// ═══════════════════════════════════════════════════════════
//  LVGL rendering task — Core 0
// ═══════════════════════════════════════════════════════════
void lvgl_task(void* param) {
    uint32_t last_ui_update = 0;

    for (;;) {
        lv_timer_handler();
        WebServer_Loop();  // Handle HTTP requests (Core 0)

        if (millis() - last_ui_update > 100) {
            last_ui_update = millis();

            if (ui_current_page == 0) {
                if (obd_data[0].valid)  UI_UpdateArc(0, obd_data[0].value, "%.0f");
                if (obd_data[1].valid)  UI_UpdateSpeed(obd_data[1].value);
                if (obd_data[2].valid)  UI_UpdateArc(2, obd_data[2].value, "%.0f");
                if (obd_data[5].valid)  UI_UpdateArc(5, obd_data[5].value, "%.0f");
                if (obd_data[4].valid)  UI_UpdateArc(4, obd_data[4].value, "%.0f");
            } else if (ui_current_page == 1) {
                if (obd_data[3].valid)  UI_UpdateArc(3, obd_data[3].value, "%.0f");
                if (obd_data[6].valid)  UI_UpdateArc(6, obd_data[6].value, "%.0f");
                if (obd_data[8].valid)  UI_UpdateArc(8, obd_data[8].value, "%.1f");
                if (obd_data[10].valid) UI_UpdateArc(10, obd_data[10].value, "%.1f");
                if (obd_data[11].valid) UI_UpdateArc(11, obd_data[11].value, "%.1f");
                if (obd_data[13].valid) UI_UpdateArc(13, obd_data[13].value * 10, "%.0f");
            } else if (ui_current_page == 2) {
                if (obd_data[12].valid) UI_UpdateArc(12, obd_data[12].value, "%.0f");
                if (obd_data[14].valid) UI_UpdateArc(14, obd_data[14].value, "%.0f");
                if (obd_data[16].valid) UI_UpdateArc(16, obd_data[16].value, "%.0f");
                if (obd_data[15].valid) UI_UpdateArc(15, obd_data[15].value * 10, "%.0f");
                if (obd_data[19].valid) UI_UpdateArc(19, obd_data[19].value, "%.0f");
                if (obd_data[17].valid) UI_UpdateArc(17, obd_data[17].value, "%.0f");
            }

            UI_HandleTouch();
        }

        vTaskDelay(pdMS_TO_TICKS(LVGL_TICK_MS));
    }
}

// ═══════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(1000);  // Extra delay for serial to stabilize
    Serial.println("\n========================================");
    Serial.println("  ESP32-S3 OBD2 WiFi Gauge v1.0");
    Serial.println("  Waveshare ESP32-S3-Touch-LCD-2.1");
    Serial.println("========================================\n");

    // 1. Display init
    Serial.println("[Boot] 1/5 Display init...");
    if (!Display_Init()) {
        Serial.println("[Boot] DISPLAY FAIL");
        delay(5000);
        ESP.restart();
    }

    // 2. UI + Touch
    Serial.println("[Boot] 2/5 UI init...");
    UI_Init();
    UI_InitTouch();
    UI_SetStatus("Booting...");
    lv_timer_handler();

    // 3. WiFi scan + auto-detect ELM327 (with retries)
    Serial.println("[Boot] 3/5 WiFi scan...");
    UI_SetStatus("Scanning WiFi...");
    lv_timer_handler();

    elm327.scanAndDetect(3);  // Up to 3 scan attempts
    if (strlen(elm327.detectedSSID) > 0) {
        char status[64];
        snprintf(status, sizeof(status), "Found: %s", elm327.detectedSSID);
        UI_SetStatus(status);
    } else {
        UI_SetStatus("No WiFi found - using default");
    }
    lv_timer_handler();

    // 4. Connect to ELM327
    Serial.println("[Boot] 4/5 ELM327 connect...");
    UI_SetStatus("Connecting...");
    lv_timer_handler();

    if (elm327.connect()) {
        char status[80];
        snprintf(status, sizeof(status), "%s @ %s:%d",
                 elm327.detectedSSID, elm327.detectedIP, elm327.detectedPort);
        UI_SetStatus(status);
        Serial.printf("[Boot] SUCCESS: %s\n", status);
    } else {
        UI_SetStatus("ELM327 not found - retrying...");
        Serial.println("[Boot] Initial connect FAILED - will keep retrying in background");
    }
    lv_timer_handler();

    // 5. FreeRTOS tasks
    Serial.println("[Boot] 5/5 Starting tasks...");
    xTaskCreatePinnedToCore(obd_task, "OBD2", 8192, NULL, 3, &obdTaskHandle, 1);
    xTaskCreatePinnedToCore(lvgl_task, "LVGL", 8192, NULL, 2, &lvglTaskHandle, 0);

    // 6. Web server
    Serial.println("[Boot] 6/5 Web server...");
    WebServer_Init();
    Serial.printf("[Boot] Web UI: http://%s\n", WiFi.localIP().toString().c_str());

    Serial.println("[Boot] READY!\n");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
