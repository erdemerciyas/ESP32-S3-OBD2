#pragma once

#include <stdint.h>

typedef enum {
    PID_ENGINE_LOAD = 0x04,
    PID_COOLANT_TEMP = 0x05,
    PID_FUEL_PRESSURE = 0x0A,
    PID_INTAKE_MAP = 0x0B,
    PID_RPM = 0x0C,
    PID_SPEED = 0x0D,
    PID_TIMING_ADVANCE = 0x0E,
    PID_INTAKE_TEMP = 0x0F,
    PID_MAF_RATE = 0x10,
    PID_THROTTLE_POS = 0x11,
    PID_AIR_STATUS = 0x13,
    PID_O2_VOLTAGE = 0x14,
    PID_RUNTIME = 0x1F,
    PID_FUEL_LEVEL = 0x2F,
    PID_DISTANCE_MIL = 0x21,
    PID_FUEL_SYS_STATUS = 0x03,
} pid_type_t;

typedef struct {
    pid_type_t pid;
    uint8_t bytes;
    float multiplier;
    float offset;
    const char *name;
} pid_info_t;

static const pid_info_t pid_table[] = {
    {PID_ENGINE_LOAD, 1, 1.0, 0, "Engine Load"},
    {PID_COOLANT_TEMP, 1, 1.0, -40, "Coolant Temp"},
    {PID_FUEL_PRESSURE, 1, 3.0, 0, "Fuel Pressure"},
    {PID_INTAKE_MAP, 1, 1.0, 0, "Intake MAP"},
    {PID_RPM, 2, 0.25, 0, "RPM"},
    {PID_SPEED, 1, 1.0, 0, "Vehicle Speed"},
    {PID_TIMING_ADVANCE, 1, 0.5, -64, "Timing Advance"},
    {PID_INTAKE_TEMP, 1, 1.0, -40, "Intake Temp"},
    {PID_MAF_RATE, 2, 0.01, 0, "MAF Rate"},
    {PID_THROTTLE_POS, 1, 0.392, 0, "Throttle Position"},
    {PID_FUEL_LEVEL, 1, 0.392, 0, "Fuel Level"},
    {PID_RUNTIME, 2, 1.0, 0, "Runtime"},
};

#define PID_TABLE_SIZE (sizeof(pid_table) / sizeof(pid_info_t))

const pid_info_t *pid_get_info(pid_type_t pid);
