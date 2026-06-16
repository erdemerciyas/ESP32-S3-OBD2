#include "vehicle_data.h"
#include "vehicle_profile.h"
#include <string.h>

static vehicle_data_t s_data;

void vehicle_data_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_data.metric_units = true;
    s_data.auto_connect = true;
    s_data.center_gauge_rpm = true;
    s_data.state = OBD_STATE_DISCONNECTED;
    strncpy(s_data.status_msg, "Hazir", sizeof(s_data.status_msg) - 1);
}

void vehicle_data_lock(void)
{
}

void vehicle_data_unlock(void)
{
}

vehicle_data_t *vehicle_data_get(void)
{
    return &s_data;
}

void vehicle_data_set_state(obd_state_t state, const char *msg)
{
    s_data.state = state;
    if (msg) {
        strncpy(s_data.status_msg, msg, sizeof(s_data.status_msg) - 1);
        s_data.status_msg[sizeof(s_data.status_msg) - 1] = '\0';
    }
}

void vehicle_data_set_adapter(const char *name, const char *addr)
{
    if (name) {
        strncpy(s_data.adapter_name, name, sizeof(s_data.adapter_name) - 1);
        s_data.adapter_name[sizeof(s_data.adapter_name) - 1] = '\0';
    }
    if (addr) {
        strncpy(s_data.adapter_addr, addr, sizeof(s_data.adapter_addr) - 1);
        s_data.adapter_addr[sizeof(s_data.adapter_addr) - 1] = '\0';
    }
}

void vehicle_data_set_pid_supported(int block, uint32_t mask)
{
    if (block < 0 || block >= 4) {
        return;
    }
    s_data.supported_pids[block] = mask;
}

void vehicle_data_clear_supported_pids(void)
{
    memset(s_data.supported_pids, 0, sizeof(s_data.supported_pids));
}

bool vehicle_data_is_pid_supported(uint8_t pid)
{
    int block = (pid - 1) / 32;
    int bit = (pid - 1) % 32;
    if (block < 0 || block >= 4) {
        return false;
    }
    return (s_data.supported_pids[block] >> bit) & 1;
}

void vehicle_data_set_float(float *field, float value)
{
    *field = value;
}

threshold_level_t vehicle_data_coolant_level(float c)
{
    if (c < 70.0f) {
        return THRESHOLD_WARN;
    }
    if (c > 105.0f) {
        return THRESHOLD_CRIT;
    }
    if (c > 95.0f) {
        return THRESHOLD_WARN;
    }
    return THRESHOLD_OK;
}

threshold_level_t vehicle_data_voltage_level(float v)
{
    if (v < 11.5f) {
        return THRESHOLD_CRIT;
    }
    if (v < 12.2f) {
        return THRESHOLD_WARN;
    }
    return THRESHOLD_OK;
}

threshold_level_t vehicle_data_rpm_level(float rpm)
{
    const vehicle_profile_t *profile = vehicle_profile_get();
    float redline = (float)profile->rpm_redline;
    float warn = redline - 1000.0f;

    if (rpm > redline) {
        return THRESHOLD_CRIT;
    }
    if (rpm > warn) {
        return THRESHOLD_WARN;
    }
    return THRESHOLD_OK;
}

float vehicle_data_convert_speed(float kmh, bool metric)
{
    return metric ? kmh : kmh * 0.621371f;
}

float vehicle_data_convert_temp(float c, bool metric)
{
    return metric ? c : (c * 9.0f / 5.0f) + 32.0f;
}

const char *vehicle_data_speed_unit(bool metric)
{
    return metric ? "km/h" : "mph";
}

const char *vehicle_data_temp_unit(bool metric)
{
    return metric ? "C" : "F";
}
