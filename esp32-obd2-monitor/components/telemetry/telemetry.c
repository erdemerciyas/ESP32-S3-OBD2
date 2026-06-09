#include "telemetry.h"
#include "connectivity.h"
#include "bt_manager.h"
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
    connectivity_sync_transport_state();

    obd_service_get_data(&snapshot.obd);
    snapshot.conn_state = connectivity_get_state();
    snapshot.conn_type = connectivity_get_current_type();
    snapshot.bt_serial_up = bt_serial_ready();
    snapshot.bt_saved_profile = connectivity_has_saved_bt_profile();
    snapshot.bt_auto_pending = connectivity_bt_auto_connect_pending();
    snapshot.bt_linked = snapshot.bt_serial_up;
    if (!snapshot.bt_linked && snapshot.conn_type == CONN_TYPE_BLUETOOTH) {
        snapshot.bt_linked = (snapshot.conn_state == CONN_STATE_LINK_UP ||
                              snapshot.conn_state == CONN_STATE_ELM_INIT ||
                              snapshot.conn_state == CONN_STATE_OBD_READY);
    }
    strncpy(snapshot.conn_status, connectivity_get_status_text(), sizeof(snapshot.conn_status) - 1);
    snapshot.adapter_label[0] = '\0';
    if (g_settings.bt_device_name[0] != '\0') {
        strncpy(snapshot.adapter_label, g_settings.bt_device_name, sizeof(snapshot.adapter_label) - 1);
    } else if (g_settings.bt_device_addr[0] != '\0') {
        strncpy(snapshot.adapter_label, g_settings.bt_device_addr, sizeof(snapshot.adapter_label) - 1);
    } else if (snapshot.conn_type == CONN_TYPE_USB) {
        strncpy(snapshot.adapter_label, "USB", sizeof(snapshot.adapter_label) - 1);
    } else {
        strncpy(snapshot.adapter_label, "Bluetooth", sizeof(snapshot.adapter_label) - 1);
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
