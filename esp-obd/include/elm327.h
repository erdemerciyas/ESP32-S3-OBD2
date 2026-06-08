#pragma once
#include <WiFi.h>
#include <WiFiClient.h>
#include "config.h"

// ── ELM327 SSID auto-detect patterns (case-insensitive substring) ──
static const char* ELM327_SSID_PATTERNS[] = {
    "OBD", "ELM", "WIFI_OBD", "CAR", "OBDII", "OBD2", "VLINKED"
};
static const int ELM327_PATTERN_COUNT = sizeof(ELM327_SSID_PATTERNS) / sizeof(ELM327_SSID_PATTERNS[0]);

// ── TCP connection candidates ──
static const char* ELM327_IP_CANDIDATES[] = {
    "192.168.0.10", "192.168.0.1", "192.168.1.1", "10.0.0.1", "10.10.1.1"
};
static const int ELM327_IP_COUNT = sizeof(ELM327_IP_CANDIDATES) / sizeof(ELM327_IP_CANDIDATES[0]);

static const uint16_t ELM327_PORT_CANDIDATES[] = { 35000, 35001, 23 };
static const int ELM327_PORT_COUNT = sizeof(ELM327_PORT_CANDIDATES) / sizeof(ELM327_PORT_CANDIDATES[0]);

enum ELM327_State {
    ELM327_DISCONNECTED,
    ELM327_SCANNING,
    ELM327_WIFI_CONNECTING,
    ELM327_TCP_CONNECTING,
    ELM327_INITIALIZING,
    ELM327_READY,
    ELM327_ERROR
};

class ELM327 {
public:
    ELM327_State state = ELM327_DISCONNECTED;
    WiFiClient client;
    char responseBuffer[512];
    uint16_t responseLen = 0;
    char detectedSSID[64] = "";
    char detectedIP[32] = "";
    uint16_t detectedPort = 0;
    char statusText[80] = "Idle";

    // ═══════════════════════════════════════════════════════════
    //  SCAN + AUTO-DETECT (with retry)
    // ═══════════════════════════════════════════════════════════
    bool scanAndDetect(int maxRetries = 3) {
        state = ELM327_SCANNING;

        for (int attempt = 1; attempt <= maxRetries; attempt++) {
            Serial.printf("[WiFi] Scan attempt %d/%d...\n", attempt, maxRetries);

            WiFi.mode(WIFI_STA);
            WiFi.disconnect(true);  // force disconnect
            delay(500);  // let radio settle

            int n = WiFi.scanNetworks(false, false, false, 300);  // async=false, hidden=false, passive=false, max 300ms/channel
            Serial.printf("[WiFi] Found %d networks\n", n);

            int bestIdx = -1;
            int bestRSSI = -100;

            for (int i = 0; i < n; i++) {
                String ssid = WiFi.SSID(i);
                bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                int rssi = WiFi.RSSI(i);
                Serial.printf("  [%d] %-24s RSSI:%3d %s\n", i, ssid.c_str(), rssi, isOpen ? "[OPEN]" : "[ENC]");

                if (ssid.length() == 0) continue;

                String ssidUpper = ssid;
                ssidUpper.toUpperCase();
                for (int p = 0; p < ELM327_PATTERN_COUNT; p++) {
                    if (ssidUpper.indexOf(ELM327_SSID_PATTERNS[p]) >= 0) {
                        if (isOpen && rssi > bestRSSI) {
                            bestIdx = i;
                            bestRSSI = rssi;
                        }
                        break;
                    }
                }
            }

            // Found a matching ELM327 network
            if (bestIdx >= 0) {
                strncpy(detectedSSID, WiFi.SSID(bestIdx).c_str(), sizeof(detectedSSID) - 1);
                detectedSSID[sizeof(detectedSSID) - 1] = '\0';
                Serial.printf("[WiFi] >> ELM327 detected: \"%s\" (RSSI: %d)\n", detectedSSID, bestRSSI);
                WiFi.scanDelete();
                return true;
            }

            // Fallback: strongest open network
            bestIdx = -1;
            bestRSSI = -100;
            for (int i = 0; i < n; i++) {
                if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN && WiFi.RSSI(i) > bestRSSI) {
                    bestIdx = i;
                    bestRSSI = WiFi.RSSI(i);
                }
            }
            if (bestIdx >= 0) {
                strncpy(detectedSSID, WiFi.SSID(bestIdx).c_str(), sizeof(detectedSSID) - 1);
                detectedSSID[sizeof(detectedSSID) - 1] = '\0';
                Serial.printf("[WiFi] >> Fallback open network: \"%s\" (RSSI: %d)\n", detectedSSID, bestRSSI);
                WiFi.scanDelete();
                return true;
            }

            WiFi.scanDelete();
            Serial.println("[WiFi] >> No suitable network found");

            if (attempt < maxRetries) {
                Serial.printf("[WiFi] >> Retrying in 3s (ELM327 may still be booting)...\n");
                snprintf(statusText, sizeof(statusText), "Scan: no ELM found, retry %d/%d", attempt, maxRetries);
                delay(3000);
            }
        }

        // All retries exhausted - use hardcoded default
        strncpy(detectedSSID, ELM327_WIFI_SSID, sizeof(detectedSSID) - 1);
        Serial.printf("[WiFi] >> Giving up scan, using default SSID: \"%s\"\n", detectedSSID);
        state = ELM327_ERROR;
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    //  FULL CONNECTION SEQUENCE
    // ═══════════════════════════════════════════════════════════
    bool connect() {
        // Step 1: WiFi (skip if already connected to same SSID)
        if (WiFi.status() != WL_CONNECTED) {
            state = ELM327_WIFI_CONNECTING;
            snprintf(statusText, sizeof(statusText), "WiFi: connecting to %s", detectedSSID);
            if (!connectWiFi()) return false;
        }

        // Step 2: TCP connection
        state = ELM327_TCP_CONNECTING;
        snprintf(statusText, sizeof(statusText), "TCP: searching ELM327...");

        // Build IP list: gateway first, then detected, then candidates
        IPAddress gwIP = WiFi.gatewayIP();
        char gwStr[32];
        snprintf(gwStr, sizeof(gwStr), "%d.%d.%d.%d", gwIP[0], gwIP[1], gwIP[2], gwIP[3]);
        Serial.printf("[TCP] Gateway IP: %s\n", gwStr);

        // Try gateway IP first (most ELM327 adapters are the gateway)
        if (tryTCPConnect(gwStr, 35000)) {
            strncpy(detectedIP, gwStr, sizeof(detectedIP) - 1);
            detectedPort = 35000;
            if (initialize()) {
                snprintf(statusText, sizeof(statusText), "OK: %s:%d", detectedIP, detectedPort);
                return true;
            }
            client.stop();
        }

        // Try all candidate IPs and ports
        for (int i = 0; i < ELM327_IP_COUNT; i++) {
            // Skip if same as gateway (already tried)
            if (strcmp(ELM327_IP_CANDIDATES[i], gwStr) == 0) continue;

            for (int p = 0; p < ELM327_PORT_COUNT; p++) {
                snprintf(statusText, sizeof(statusText), "TCP: %s:%d", ELM327_IP_CANDIDATES[i], ELM327_PORT_CANDIDATES[p]);

                if (tryTCPConnect(ELM327_IP_CANDIDATES[i], ELM327_PORT_CANDIDATES[p])) {
                    strncpy(detectedIP, ELM327_IP_CANDIDATES[i], sizeof(detectedIP) - 1);
                    detectedPort = ELM327_PORT_CANDIDATES[p];
                    if (initialize()) {
                        snprintf(statusText, sizeof(statusText), "OK: %s:%d", detectedIP, detectedPort);
                        return true;
                    }
                    client.stop();
                }
            }
        }

        Serial.println("[ELM327] ALL connection attempts FAILED");
        snprintf(statusText, sizeof(statusText), "Failed: no ELM327 response");
        state = ELM327_ERROR;
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    //  RECONNECT (persistent, never gives up)
    // ═══════════════════════════════════════════════════════════
    bool reconnect() {
        // Kill existing TCP first
        if (client.connected()) client.stop();
        delay(100);
        state = ELM327_DISCONNECTED;

        // Check if WiFi is still alive
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] WiFi lost - reconnecting...");
            WiFi.disconnect(true);
            delay(500);
            // Re-scan to find ELM327 (may have different SSID or moved)
            scanAndDetect(2);  // Don't fail on scan - try with existing SSID anyway
        }

        return connect();
    }

    void disconnect() {
        if (client.connected()) client.stop();
        state = ELM327_DISCONNECTED;
    }

    bool isConnected() {
        return state == ELM327_READY && client.connected();
    }

    // ═══════════════════════════════════════════════════════════
    //  AT COMMAND (handles both '>' and newline termination)
    // ═══════════════════════════════════════════════════════════
    bool sendATCommand(const char* cmd, uint32_t timeoutMs = 2000) {
        if (!client.connected()) {
            state = ELM327_ERROR;
            return false;
        }
        flushBuffer();
        client.print(cmd);
        client.print("\r");

        uint32_t start = millis();
        bool gotPrompt = false;

        while (millis() - start < timeoutMs) {
            while (client.available()) {
                char c = client.read();
                if (responseLen < sizeof(responseBuffer) - 1)
                    responseBuffer[responseLen++] = c;

                if (c == '>') {
                    responseBuffer[responseLen] = '\0';
                    gotPrompt = true;
                    Serial.printf("[AT] %s -> %s\n", cmd, responseBuffer);
                    return true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(3));
        }

        responseBuffer[responseLen] = '\0';

        // ATZ may not send '>' - check if we got meaningful data
        if (strcmp(cmd, "ATZ") == 0 && responseLen > 3) {
            Serial.printf("[AT] %s -> (no prompt, got data): %s\n", cmd, responseBuffer);
            return true;
        }

        Serial.printf("[AT] %s -> TIMEOUT (%dms, %d bytes): %s\n", cmd, timeoutMs, responseLen, responseBuffer);
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    //  PID REQUEST
    // ═══════════════════════════════════════════════════════════
    bool sendPIDRequest(const char* pid, uint32_t timeoutMs = 1000) {
        if (!client.connected()) {
            state = ELM327_ERROR;
            return false;
        }
        flushBuffer();
        client.print(pid);
        client.print("\r");

        uint32_t start = millis();
        while (millis() - start < timeoutMs) {
            while (client.available()) {
                char c = client.read();
                if (responseLen < sizeof(responseBuffer) - 1)
                    responseBuffer[responseLen++] = c;
                if (c == '>') {
                    responseBuffer[responseLen] = '\0';
                    return parsePIDResponse();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        responseBuffer[responseLen] = '\0';
        return false;
    }

    const char* getResponseData() { return parsedData; }

private:
    char parsedData[128];

    // ── TCP connect with short timeout ──
    bool tryTCPConnect(const char* ip, uint16_t port) {
        Serial.printf("[TCP] Trying %s:%d... ", ip, port);

        // Short timeout so we don't block forever on bad IPs
        client.setTimeout(2000);  // 2 seconds

        IPAddress addr;
        addr.fromString(ip);

        if (client.connect(addr, port)) {
            client.setNoDelay(true);
            client.setTimeout(2000);
            Serial.println("CONNECTED");
            delay(200);  // Let ELM327 settle after TCP accept

            // Flush any garbage the adapter sends on connect
            while (client.available()) client.read();

            return true;
        }
        Serial.println("fail");
        return false;
    }

    // ── WiFi connect ──
    bool connectWiFi() {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);  // Critical: prevents TCP drops

        const char* ssid = (strlen(detectedSSID) > 0) ? detectedSSID : ELM327_WIFI_SSID;

        Serial.printf("[WiFi] Connecting to \"%s\"...\n", ssid);

        // Explicit NULL password for open networks (ESP32 Arduino Core compatibility)
        WiFi.begin(ssid, (const char*)NULL);

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(250));
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("[WiFi] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
            return true;
        }

        Serial.println("\n[WiFi] Connection FAILED!");
        state = ELM327_ERROR;
        return false;
    }

    // ── ELM327 initialization sequence ──
    bool initialize() {
        state = ELM327_INITIALIZING;
        Serial.println("[ELM327] Initializing...");
        snprintf(statusText, sizeof(statusText), "ELM: initializing...");

        // ATZ = reset. Some clones need extra time to boot after this.
        if (!sendATCommand("ATZ", 4000)) {
            Serial.println("[ELM327] ATZ failed, retrying in 2s...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            flushBuffer();
            if (!sendATCommand("ATZ", 4000)) {
                Serial.println("[ELM327] ATZ FAILED after retry");
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1500));

        if (strstr(responseBuffer, "ELM") || strstr(responseBuffer, "elm") || responseLen > 5) {
            Serial.println("[ELM327] ELM chip identified!");
        } else {
            Serial.printf("[ELM327] WARNING: Unexpected ATZ response (%d bytes)\n", responseLen);
        }

        flushBuffer();

        // Standard init sequence
        if (!sendATCommand("ATE0")) return false;   // Echo off
        if (!sendATCommand("ATL0")) return false;   // Linefeeds off
        if (!sendATCommand("ATS0")) return false;   // Spaces off
        if (!sendATCommand("ATH0")) return false;   // Headers off

        // ── Protocol detection with KWP2000 fallback ──
        // ATSP0 = auto detect (works for CAN, ISO9141, KWP2000)
        if (!sendATCommand("ATSP0", 5000)) return false;
        vTaskDelay(pdMS_TO_TICKS(100));

        // Optional timing
        sendATCommand("ATAT2");    // Adaptive timing
        sendATCommand("ATST32");   // Timeout 128ms

        // Test with PID 0100 (supported PIDs)
        bool pid_ok = sendATCommand("0100", 5000);

        if (!pid_ok) {
            // Auto-detect failed - try KWP2000 fast init (common on GM/Daewoo 2000-2010)
            Serial.println("[ELM327] Auto-protocol failed, trying KWP2000 fast init (ATSP4)...");
            sendATCommand("ATSP4", 5000);
            vTaskDelay(pdMS_TO_TICKS(500));
            pid_ok = sendATCommand("0100", 5000);
        }

        if (!pid_ok) {
            // Try KWP2000 slow init (5-baud)
            Serial.println("[ELM327] KWP fast failed, trying KWP slow init (ATSP5)...");
            sendATCommand("ATSP5", 5000);
            vTaskDelay(pdMS_TO_TICKS(500));
            pid_ok = sendATCommand("0100", 5000);
        }

        if (!pid_ok) {
            // Try ISO 9141-2 (older vehicles)
            Serial.println("[ELM327] KWP slow failed, trying ISO9141 (ATSP3)...");
            sendATCommand("ATSP3", 5000);
            vTaskDelay(pdMS_TO_TICKS(500));
            pid_ok = sendATCommand("0100", 5000);
        }

        if (!pid_ok) {
            // Fallback to auto again
            Serial.println("[ELM327] ISO9141 failed, back to auto (ATSP0)...");
            sendATCommand("ATSP0", 5000);
            // Don't fail - car may not be ON, OBD task will keep trying
        }

        // Log detected protocol
        if (sendATCommand("ATDP", 2000)) {
            Serial.printf("[ELM327] Protocol: %s\n", responseBuffer);
        }

        if (pid_ok) {
            Serial.printf("[ELM327] Supported PIDs (0100): %s\n", parsedData);

            // Decode PID 0100 bitmask to show which PIDs are available
            if (strlen(parsedData) >= 8) {
                uint32_t bitmask = (uint32_t)strtol(parsedData, NULL, 16);
                Serial.println("[ELM327] Available PIDs:");
                for (int i = 1; i <= 32; i++) {
                    if (bitmask & (1UL << (32 - i))) {
                        Serial.printf("  PID 0x%02X (0x%02X)\n", i, i);
                    }
                }
            }
        } else {
            Serial.println("[ELM327] WARNING: PID 0100 failed - car ignition may be OFF");
        }

        // Try reading VIN
        sendATCommand("0902", 3000);

        state = ELM327_READY;
        Serial.println("[ELM327] ═══ READY ═══");
        return true;
    }

    // ── Flush buffers ──
    void flushBuffer() {
        responseLen = 0;
        memset(responseBuffer, 0, sizeof(responseBuffer));
        memset(parsedData, 0, sizeof(parsedData));
        while (client.available()) client.read();
    }

    // ── Parse PID response ──
    bool parsePIDResponse() {
        // Search for "41" which indicates a valid Mode 01 response
        char* ptr = strstr(responseBuffer, "41");
        if (!ptr) {
            // Also try lowercase (some ELM clones use lowercase)
            ptr = strstr(responseBuffer, "41");
            if (!ptr) {
                if (strstr(responseBuffer, "NODATA") || strstr(responseBuffer, "NO DATA") ||
                    strstr(responseBuffer, "ERROR") || strstr(responseBuffer, "?")) {
                    strcpy(parsedData, "NODATA");
                    return false;
                }
                // Try from beginning of buffer
                ptr = responseBuffer;
            }
        } else {
            ptr += 4;  // Skip "41XX" (mode + PID echo)
        }

        // Extract hex digits, skip spaces/newlines
        int i = 0;
        while (*ptr && *ptr != '>' && i < (int)sizeof(parsedData) - 1) {
            if (isxdigit((unsigned char)*ptr)) {
                parsedData[i++] = *ptr;
            } else if (*ptr == '\r' || *ptr == '\n') {
                break;  // End of response line
            }
            // Skip spaces and other non-hex chars
            ptr++;
        }
        parsedData[i] = '\0';
        return i > 0;
    }
};
