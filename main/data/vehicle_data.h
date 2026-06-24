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
threshold_level_t vehicle_data_coolant_level(float c);
threshold_level_t vehicle_data_voltage_level(float v);
threshold_level_t vehicle_data_rpm_level(float rpm);

float vehicle_data_convert_speed(float kmh, bool metric);
float vehicle_data_convert_temp(float c, bool metric);
const char *vehicle_data_speed_unit(bool metric);
const char *vehicle_data_temp_unit(bool metric);
