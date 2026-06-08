#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include "elm327.h"
#include "obd2.h"

extern ELM327 elm327;
extern OBD2Data obd_data[];

static WebServer webServer(80);
static bool web_requested_scan   = false;
static bool web_requested_repick = false;
static char web_manual_ssid[64]  = "";

// ═══════════════════════════════════════════════════════════
//  HTML Page (embedded)
// ═══════════════════════════════════════════════════════════
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 OBD2 Setup</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
         background: #111; color: #eee; padding: 16px; line-height: 1.5; }
  h1 { color: #E94560; font-size: 22px; margin-bottom: 12px; }
  h2 { color: #42A5F5; font-size: 16px; margin: 18px 0 8px; }
  .card { background: #1a1a1a; border: 1px solid #333; border-radius: 8px;
          padding: 14px; margin-bottom: 12px; }
  .status { font-size: 14px; }
  .ok { color: #4CAF50; }
  .bad { color: #F44336; }
  .warn { color: #FFC107; }
  .net { display: flex; justify-content: space-between; align-items: center;
         padding: 10px; border: 1px solid #2a2a2a; border-radius: 6px;
         margin: 6px 0; background: #222; }
  .net.selected { border-color: #4CAF50; background: #1a2a1a; }
  .net .ssid { font-weight: 600; font-size: 14px; }
  .net .rssi { color: #888; font-size: 12px; }
  .net .open { color: #4CAF50; font-size: 11px; }
  .net .enc { color: #FFC107; font-size: 11px; }
  .btn { background: #E94560; color: #fff; border: 0; padding: 8px 14px;
         border-radius: 6px; cursor: pointer; font-size: 13px; }
  .btn:disabled { background: #555; cursor: not-allowed; }
  .btn.secondary { background: #333; }
  .pid-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .pid { background: #222; padding: 8px; border-radius: 6px; }
  .pid .name { color: #888; font-size: 11px; }
  .pid .val { color: #fff; font-size: 18px; font-weight: 600; }
  .pid .unit { color: #888; font-size: 11px; }
  .refresh { display: inline-block; margin-left: 8px; }
  pre { background: #000; padding: 8px; border-radius: 4px;
        font-size: 12px; overflow-x: auto; color: #4CAF50; }
</style>
</head>
<body>
  <h1>ESP32-S3 OBD2 WiFi Gauge</h1>

  <div class="card">
    <h2>Connection Status</h2>
    <div id="status" class="status">Loading...</div>
    <button class="btn secondary refresh" onclick="loadStatus()">Refresh</button>
  </div>

  <div class="card">
    <h2>Available WiFi Networks</h2>
    <button class="btn" onclick="scanWifi()">Scan Networks</button>
    <span id="scanMsg" class="warn"></span>
    <div id="networks" style="margin-top:10px;"></div>
  </div>

  <div class="card">
    <h2>Live OBD2 Data</h2>
    <div id="pidGrid" class="pid-grid">
      <div class="pid"><div class="name">Waiting...</div><div class="val">---</div></div>
    </div>
  </div>

  <div class="card">
    <h2>Debug Log</h2>
    <pre id="debug">No data yet</pre>
  </div>

<script>
async function loadStatus() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    let html = '';
    html += '<div>WiFi: ' + (d.wifi_ok ? '<span class="ok">CONNECTED</span>' : '<span class="bad">DISCONNECTED</span>') + '</div>';
    html += '<div>SSID: <b>' + (d.ssid || 'none') + '</b></div>';
    html += '<div>IP: ' + d.local_ip + '</div>';
    html += '<div>Gateway: ' + d.gateway + '</div>';
    html += '<div>TCP: ' + (d.tcp_ok ? '<span class="ok">CONNECTED</span>' : '<span class="bad">NO TCP</span>') + '</div>';
    html += '<div>ELM327: ' + (d.elm_ready ? '<span class="ok">READY</span>' : '<span class="warn">NOT READY</span>') + '</div>';
    if (d.detected_ip) html += '<div>OBD IP: ' + d.detected_ip + ':' + d.detected_port + '</div>';
    html += '<div>State: ' + d.state + '</div>';
    html += '<div>Status: ' + d.status + '</div>';
    document.getElementById('status').innerHTML = html;
  } catch (e) { document.getElementById('status').innerHTML = '<span class="bad">Error: ' + e + '</span>'; }
}

async function scanWifi() {
  document.getElementById('scanMsg').textContent = 'Scanning...';
  document.getElementById('networks').innerHTML = '';
  try {
    const r = await fetch('/api/scan');
    const d = await r.json();
    if (d.error) { document.getElementById('scanMsg').innerHTML = '<span class="bad">' + d.error + '</span>'; return; }
    document.getElementById('scanMsg').textContent = d.count + ' network(s) found';
    let html = '';
    d.networks.forEach(n => {
      const sel = (n.ssid === d.current) ? ' selected' : '';
      html += '<div class="net' + sel + '">';
      html += '<div><div class="ssid">' + n.ssid + '</div>';
      html += '<div class="rssi">RSSI: ' + n.rssi + ' dBm';
      html += ' ' + (n.open ? '<span class="open">OPEN</span>' : '<span class="enc">ENCRYPTED</span>') + '</div></div>';
      html += '<button class="btn" onclick="selectSsid(\'' + n.ssid + '\')">Select</button>';
      html += '</div>';
    });
    document.getElementById('networks').innerHTML = html;
  } catch (e) { document.getElementById('scanMsg').innerHTML = '<span class="bad">Error: ' + e + '</span>'; }
}

async function selectSsid(s) {
  if (!confirm('Connect to "' + s + '"? ESP32 will re-scan and reconnect.')) return;
  try {
    const r = await fetch('/api/select?ssid=' + encodeURIComponent(s));
    const d = await r.json();
    alert(d.msg || 'OK');
    setTimeout(loadStatus, 2000);
  } catch (e) { alert('Error: ' + e); }
}

async function loadPids() {
  try {
    const r = await fetch('/api/pids');
    const d = await r.json();
    let html = '';
    for (const k in d.pids) {
      const p = d.pids[k];
      html += '<div class="pid"><div class="name">' + p.name + '</div>';
      html += '<div class="val">' + p.val + ' <span class="unit">' + p.unit + '</span></div></div>';
    }
    document.getElementById('pidGrid').innerHTML = html;
    document.getElementById('debug').textContent = d.debug || '';
  } catch (e) {}
}

loadStatus();
loadPids();
setInterval(loadStatus, 3000);
setInterval(loadPids, 500);
</script>
</body>
</html>
)rawliteral";

// ═══════════════════════════════════════════════════════════
//  HTML page
// ═══════════════════════════════════════════════════════════
static void handleRoot() {
    webServer.send(200, "text/html", INDEX_HTML);
}

// ═══════════════════════════════════════════════════════════
//  /api/status - connection info
// ═══════════════════════════════════════════════════════════
static void handleStatus() {
    char json[512];
    snprintf(json, sizeof(json),
        "{\"wifi_ok\":%s,\"tcp_ok\":%s,\"elm_ready\":%s,"
        "\"ssid\":\"%s\",\"local_ip\":\"%s\",\"gateway\":\"%s\","
        "\"detected_ip\":\"%s\",\"detected_port\":%d,"
        "\"state\":%d,\"status\":\"%s\"}",
        (WiFi.status() == WL_CONNECTED) ? "true" : "false",
        elm327.client.connected() ? "true" : "false",
        elm327.isConnected() ? "true" : "false",
        elm327.detectedSSID,
        WiFi.localIP().toString().c_str(),
        WiFi.gatewayIP().toString().c_str(),
        elm327.detectedIP,
        elm327.detectedPort,
        (int)elm327.state,
        elm327.statusText);
    webServer.send(200, "application/json", json);
}

// ═══════════════════════════════════════════════════════════
//  /api/scan - list WiFi networks
// ═══════════════════════════════════════════════════════════
static void handleScan() {
    // Signal OBD task to do the scan (it's the only safe thread for WiFi)
    web_requested_scan = true;

    // Wait up to 5 seconds for scan to complete
    for (int i = 0; i < 50; i++) {
        if (!web_requested_scan) break;
        delay(100);
    }

    if (web_requested_scan) {
        webServer.send(200, "application/json", "{\"error\":\"scan timeout\"}");
        return;
    }

    // Read scan results
    int n = WiFi.scanComplete();
    if (n < 0) {
        webServer.send(200, "application/json", "{\"error\":\"scan failed\"}");
        return;
    }

    String json = "{\"count\":" + String(n) + ",\"current\":\"" + String(elm327.detectedSSID) + "\",\"networks\":[";
    for (int i = 0; i < n; i++) {
        if (i) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    json += "]}";
    WiFi.scanDelete();
    webServer.send(200, "application/json", json);
}

// ═══════════════════════════════════════════════════════════
//  /api/select?ssid=X - choose a network manually
// ═══════════════════════════════════════════════════════════
static void handleSelect() {
    if (!webServer.hasArg("ssid")) {
        webServer.send(400, "application/json", "{\"msg\":\"missing ssid\"}");
        return;
    }
    String s = webServer.arg("ssid");
    if (s.length() == 0 || s.length() >= 64) {
        webServer.send(400, "application/json", "{\"msg\":\"invalid ssid\"}");
        return;
    }
    s.toCharArray(web_manual_ssid, sizeof(web_manual_ssid));
    web_requested_repick = true;

    char json[128];
    snprintf(json, sizeof(json), "{\"msg\":\"selected %s - reconnecting...\"}", web_manual_ssid);
    webServer.send(200, "application/json", json);
}

// ═══════════════════════════════════════════════════════════
//  /api/pids - live OBD2 data
// ═══════════════════════════════════════════════════════════
static void handlePids() {
    String json = "{\"pids\":{";
    for (int i = 0; i < PID_TABLE_SIZE; i++) {
        if (i) json += ",";
        char key[8];
        snprintf(key, sizeof(key), "\"%02X\"", PID_TABLE[i].pid);
        json += key;
        json += ":";

        char val[32];
        if (obd_data[i].valid) {
            // Use format hint based on PID type
            if (PID_TABLE[i].pid == PID_FUEL_RATE ||
                PID_TABLE[i].pid == PID_MAF_FLOW) {
                snprintf(val, sizeof(val), "%.1f", obd_data[i].value);
            } else {
                snprintf(val, sizeof(val), "%.0f", obd_data[i].value);
            }
        } else {
            snprintf(val, sizeof(val), "---");
        }

        char entry[128];
        snprintf(entry, sizeof(entry), "{\"name\":\"%s\",\"val\":\"%s\",\"unit\":\"%s\"}",
                 PID_TABLE[i].name, val, PID_TABLE[i].unit);
        json += entry;
    }
    json += "},";

    // Debug
    char dbg[160];
    snprintf(dbg, sizeof(dbg),
        "\"debug\":\"%s | %s:%d | state=%d\"",
        elm327.detectedSSID, elm327.detectedIP, elm327.detectedPort,
        (int)elm327.state);
    json += dbg;
    json += "}";

    webServer.send(200, "application/json", json);
}

// ═══════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════
inline void WebServer_Init() {
    webServer.on("/", HTTP_GET, handleRoot);
    webServer.on("/api/status", HTTP_GET, handleStatus);
    webServer.on("/api/scan", HTTP_GET, handleScan);
    webServer.on("/api/select", HTTP_GET, handleSelect);
    webServer.on("/api/pids", HTTP_GET, handlePids);
    webServer.begin();
    Serial.println("[Web] HTTP server started on port 80");
}

inline void WebServer_Loop() {
    webServer.handleClient();
}

// Called by OBD task to check if a web request wants to rescan or repick
inline bool WebServer_PollRescan() {
    if (web_requested_scan) {
        web_requested_scan = false;
        return true;
    }
    return false;
}

inline bool WebServer_PollRepick(char* outSsid, int maxLen) {
    if (web_requested_repick) {
        web_requested_repick = false;
        strncpy(outSsid, web_manual_ssid, maxLen - 1);
        outSsid[maxLen - 1] = '\0';
        return true;
    }
    return false;
}
