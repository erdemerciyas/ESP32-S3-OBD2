#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OBD_STATE_DISCONNECTED = 0,
    OBD_STATE_SCANNING,
    OBD_STATE_CONNECTING,
    OBD_STATE_ELM_INIT,
    OBD_STATE_PID_DISCOVERY,
    OBD_STATE_READY,
    OBD_STATE_ERROR,
} obd_state_t;

typedef enum {
    THRESHOLD_OK = 0,
    THRESHOLD_WARN,
    THRESHOLD_CRIT,
} threshold_level_t;

typedef struct {
    float rpm;
    float speed;
    float coolant;
    float voltage;
    float throttle;
    float map;
    float maf;
    float iat;
    float timing;
    float fuel_trim_st;
    float fuel_trim_lt;
    float load;
    float fuel_pressure;
    float fuel_level;       /* PID 0x2F — Fuel tank level 0-100% */
    float o2_voltage;
    float o2_b1s2;
    float fuel_system1;
    float fuel_system2;
    float secondary_air;

    /* Timestamps (lv_tick_get() ms) of the last valid update per key value.
     * Used by the UI to detect stale data and show "--" for values that have
     * not refreshed within a few seconds. */
    uint32_t rpm_ts;
    uint32_t speed_ts;
    uint32_t coolant_ts;
    uint32_t voltage_ts;

    /* Pair timestamp: set when RPM and Speed are received together via a
     * multi-PID batch request, so the UI knows they are from the same ECU
     * sample. Zero means they were polled individually. */
    uint32_t dash_pair_ts;

    uint32_t supported_pids[4];

    obd_state_t state;
    char adapter_name[32];
    char adapter_addr[18];
    char status_msg[64];

    bool metric_units;
    bool auto_connect;
    bool center_gauge_rpm;
    uint8_t theme_id;
} vehicle_data_t;

/* Atomic snapshot used by UI to read consistent values in a single frame. */
typedef struct {
    float rpm;
    float speed;
    float coolant;
    float voltage;
    float throttle;
    float map;
    float maf;
    float iat;
    float timing;
    float fuel_trim_st;
    float fuel_trim_lt;
    float load;
    float fuel_level;
    float o2_voltage;
    float o2_b1s2;

    /* Timestamps (lv_tick_get() ms) for stale-value detection in the UI. */
    uint32_t rpm_ts;
    uint32_t speed_ts;
    uint32_t coolant_ts;
    uint32_t voltage_ts;

    /* Pair timestamp from batch RPM+Speed request (0 = individual poll). */
    uint32_t dash_pair_ts;

    obd_state_t state;
    char adapter_name[32];
    char adapter_addr[18];
    char status_msg[64];

    bool metric_units;
    bool center_gauge_rpm;
} vehicle_data_snapshot_t;

void vehicle_data_init(void);
void vehicle_data_snapshot(vehicle_data_snapshot_t *snap);
void vehicle_data_lock(void);
void vehicle_data_unlock(void);
vehicle_data_t *vehicle_data_get(void);

void vehicle_data_set_state(obd_state_t state, const char *msg);
void vehicle_data_set_adapter(const char *name, const char *addr);
void vehicle_data_set_pid_supported(int block, uint32_t mask);
void vehicle_data_clear_supported_pids(void);
bool vehicle_data_is_pid_supported(uint8_t pid);

void vehicle_data_set_float(float *field, float value);

/* Update a key value together with its freshness timestamp. Callers in the OBD
 * layer should prefer these over vehicle_data_set_float for rpm/speed/coolant/
 * voltage so the UI can detect stale readings. */
void vehicle_data_set_rpm(float value);
void vehicle_data_set_speed(float value);
void vehicle_data_set_coolant(float value);
void vehicle_data_set_voltage(float value);

/* Atomically set both RPM and Speed with a single timestamp (batch poll).
 * When the ECU returns both PIDs in one response this guarantees the UI sees
 * them as a coherent pair from the same sample instant. */
void vehicle_data_set_dash_pair(float rpm, float speed);

/* Returns true if a value last updated at ts is still fresh (within max_age_ms
 * of now). now is typically lv_tick_get(); pass 0 to use the stored value. */
bool vehicle_data_is_fresh(uint32_t ts, uint32_t now, uint32_t max_age_ms);

threshold_level_t vehicle_data_coolant_level(float c);
threshold_level_t vehicle_data_voltage_level(float v);
threshold_level_t vehicle_data_rpm_level(float rpm);

float vehicle_data_convert_speed(float kmh, bool metric);
float vehicle_data_convert_temp(float c, bool metric);
const char *vehicle_data_speed_unit(bool metric);
const char *vehicle_data_temp_unit(bool metric);
