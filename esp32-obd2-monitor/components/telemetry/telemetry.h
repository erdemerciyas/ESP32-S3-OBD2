#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "connectivity.h"
#include "obd_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    obd_data_t obd;
    connectivity_state_t conn_state;
    connection_type_t conn_type;
    bool bt_linked;
    bool bt_serial_up;
    char conn_status[64];
    char adapter_label[32];
} telemetry_snapshot_t;

void telemetry_init(void);
void telemetry_refresh(void);
void telemetry_get_snapshot(telemetry_snapshot_t *out);

#ifdef __cplusplus
}
#endif
