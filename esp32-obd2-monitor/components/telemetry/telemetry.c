#include "telemetry.h"
#include "connectivity.h"
#include "wifi_manager.h"
#include "app.h"
#include <string.h>

static telemetry_snapshot_t snapshot;

extern app_settings_t g_settings;

void telemetry_init(void)
{
    memset(&snapshot, 0, sizeof(snapshot));
}

void telemetry_refresh(void)
{
    obd_service_get_data(&snapshot.obd);
    snapshot.conn_state = connectivity_get_state();
    snapshot.conn_type = connectivity_get_current_type();
    snapshot.wifi_ap_up = wifi_is_ap_connected();
    snapshot.wifi_tcp_up = wifi_tcp_ready();
    strncpy(snapshot.conn_status, connectivity_get_status_text(), sizeof(snapshot.conn_status) - 1);

    snapshot.adapter_label[0] = '\0';
    switch (snapshot.conn_type) {
        case CONN_TYPE_WIFI:
            if (g_settings.wifi_ssid[0] != '\0') {
                strncpy(snapshot.adapter_label, g_settings.wifi_ssid, sizeof(snapshot.adapter_label) - 1);
            } else {
                strncpy(snapshot.adapter_label, "WiFi", sizeof(snapshot.adapter_label) - 1);
            }
            break;
        case CONN_TYPE_USB:
            strncpy(snapshot.adapter_label, "USB", sizeof(snapshot.adapter_label) - 1);
            break;
        default:
            strncpy(snapshot.adapter_label, "—", sizeof(snapshot.adapter_label) - 1);
            break;
    }
}

void telemetry_get_snapshot(telemetry_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    telemetry_refresh();
    *out = snapshot;
}
