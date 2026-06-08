#include "pid_table.h"
#include "vehicle_profile.h"
#include <stddef.h>

/*
 * Kalos 2005 profiline göre PID tablosu.
 * ECU desteklemediğinde pid_support + gauge_sync_availability otomatik gizler.
 */
static const pid_info_t pid_table[] = {
    /* Hızlı poll */
    {PID_RPM, 2, 0.25f, 0, PID_POLL_FAST, OBD_GAUGE_RPM, "RPM"},
    {PID_SPEED, 1, 1.0f, 0, PID_POLL_FAST, OBD_GAUGE_SPEED, "Speed"},
    {PID_THROTTLE_POS, 1, 100.0f / 255.0f, 0, PID_POLL_FAST, OBD_GAUGE_THROTTLE, "Throttle"},
    /* Slow poll */
    {PID_COOLANT_TEMP, 1, 1.0f, -40.0f, PID_POLL_SLOW, OBD_GAUGE_COOLANT, "Coolant"},
    {PID_FUEL_LEVEL, 1, 100.0f / 255.0f, 0, PID_POLL_SLOW, OBD_GAUGE_FUEL, "Fuel"},
    {PID_ENGINE_LOAD, 1, 100.0f / 255.0f, 0, PID_POLL_SLOW, OBD_GAUGE_LOAD, "Engine Load"},
    {PID_INTAKE_TEMP, 1, 1.0f, -40.0f, PID_POLL_SLOW, OBD_GAUGE_INTAKE, "Intake Temp"},
    {PID_MAF_RATE, 2, 0.01f, 0, PID_POLL_SLOW, OBD_GAUGE_NONE, "MAF"},
    {PID_CONTROL_MODULE_VOLTAGE, 2, 0.001f, 0, PID_POLL_SLOW, OBD_GAUGE_VOLTAGE, "Voltage"},
};

const pid_info_t *pid_get_info(pid_type_t pid)
{
    for (unsigned i = 0; i < PID_TABLE_SIZE; i++) {
        if (pid_table[i].pid == pid) {
            return &pid_table[i];
        }
    }
    return NULL;
}

const pid_info_t *pid_get_by_index(unsigned index)
{
    if (index >= PID_TABLE_SIZE) {
        return NULL;
    }
    return &pid_table[index];
}

const pid_info_t *pid_find_by_gauge(int8_t gauge_id)
{
    if (gauge_id < 0) {
        return NULL;
    }
    for (unsigned i = 0; i < PID_TABLE_SIZE; i++) {
        if (pid_table[i].gauge_id == gauge_id) {
            return &pid_table[i];
        }
    }
    return NULL;
}
