#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t pid;
    uint32_t interval_ms;
    bool priority;
    bool live;
} vehicle_pid_poll_t;

typedef struct {
    const char *display_name;
    const char *engine;
    const char *protocol;
    const char *init_protocol_cmd;
    const char *init_timeout_cmd;
    uint32_t live_timeout_ms;
    uint32_t slow_timeout_ms;
    uint32_t disc_timeout_ms;
    int rpm_max;
    int rpm_redline;
    bool use_atrv_voltage;
    float atrv_voltage_scale;
    const uint32_t known_pid_masks[4];
    const vehicle_pid_poll_t *live_pids;
    size_t live_count;
    const vehicle_pid_poll_t *fast_pids;
    size_t fast_count;
    const vehicle_pid_poll_t *slow_pids;
    size_t slow_count;
} vehicle_profile_t;

const vehicle_profile_t *vehicle_profile_get(void);
void vehicle_profile_apply_known_pids(void);
