#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PID_ENGINE_LOAD = 0x04,
    PID_COOLANT_TEMP = 0x05,
    PID_CONTROL_MODULE_VOLTAGE = 0x42,
    PID_RPM = 0x0C,
    PID_SPEED = 0x0D,
    PID_INTAKE_TEMP = 0x0F,
    PID_MAF_RATE = 0x10,
    PID_THROTTLE_POS = 0x11,
    PID_FUEL_LEVEL = 0x2F,
} pid_type_t;

typedef enum {
    PID_POLL_FAST = 0,
    PID_POLL_SLOW,
} pid_poll_tier_t;

/** Must match gauge_type_t order in display/ui/gauge.h (GAUGE_MAX entries use -1). */
typedef enum {
    OBD_GAUGE_NONE = -1,
    OBD_GAUGE_RPM = 0,
    OBD_GAUGE_SPEED,
    OBD_GAUGE_COOLANT,
    OBD_GAUGE_VOLTAGE,
    OBD_GAUGE_THROTTLE,
    OBD_GAUGE_FUEL,
    OBD_GAUGE_LOAD,
    OBD_GAUGE_INTAKE,
    OBD_GAUGE_FUEL_CONSUMPTION,
    OBD_GAUGE_DTC_WARNING,
} obd_gauge_id_t;

#define OBD_GAUGE_COUNT 10

typedef struct {
    pid_type_t pid;
    uint8_t bytes;
    float multiplier;
    float offset;
    pid_poll_tier_t tier;
    int8_t gauge_id;
    const char *name;
} pid_info_t;

#define PID_TABLE_SIZE 9
#define OBD_PID_FAIL_STREAK_MAX 3

const pid_info_t *pid_get_info(pid_type_t pid);
const pid_info_t *pid_get_by_index(unsigned index);
const pid_info_t *pid_find_by_gauge(int8_t gauge_id);
