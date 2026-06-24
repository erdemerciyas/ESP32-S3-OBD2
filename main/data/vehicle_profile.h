#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define VEHICLE_PROFILE_ID_LEN         16
#define VEHICLE_PROFILE_NAME_LEN       32
#define VEHICLE_PROFILE_ENGINE_LEN     32
#define VEHICLE_PROFILE_PROTOCOL_LEN   32
#define VEHICLE_PROFILE_CMD_LEN        16
#define VEHICLE_PROFILE_MAX_COUNT      8

typedef struct {
    uint8_t pid;
    uint32_t interval_ms;
    bool priority;
    bool live;
} vehicle_pid_poll_t;

/* Runtime profile: includes pointers to static PID tables. */
typedef struct {
    char profile_id[VEHICLE_PROFILE_ID_LEN];
    char display_name[VEHICLE_PROFILE_NAME_LEN];
    char engine[VEHICLE_PROFILE_ENGINE_LEN];
    char protocol[VEHICLE_PROFILE_PROTOCOL_LEN];
    char init_protocol_cmd[VEHICLE_PROFILE_CMD_LEN];
    char init_timeout_cmd[VEHICLE_PROFILE_CMD_LEN];
    uint32_t live_timeout_ms;
    uint32_t slow_timeout_ms;
    uint32_t disc_timeout_ms;
    int rpm_max;
    int rpm_redline;
    bool use_atrv_voltage;
    float atrv_voltage_scale;
    uint32_t known_pid_masks[4];
    const vehicle_pid_poll_t *live_pids;
    size_t live_count;
    const vehicle_pid_poll_t *fast_pids;
    size_t fast_count;
    const vehicle_pid_poll_t *slow_pids;
    size_t slow_count;
} vehicle_profile_t;

/* Serializable profile storage (no pointers). */
typedef struct {
    char profile_id[VEHICLE_PROFILE_ID_LEN];
    char display_name[VEHICLE_PROFILE_NAME_LEN];
    char engine[VEHICLE_PROFILE_ENGINE_LEN];
    char protocol[VEHICLE_PROFILE_PROTOCOL_LEN];
    char init_protocol_cmd[VEHICLE_PROFILE_CMD_LEN];
    char init_timeout_cmd[VEHICLE_PROFILE_CMD_LEN];
    uint32_t live_timeout_ms;
    uint32_t slow_timeout_ms;
    uint32_t disc_timeout_ms;
    int rpm_max;
    int rpm_redline;
    bool use_atrv_voltage;
    float atrv_voltage_scale;
    uint32_t known_pid_masks[4];
    bool is_default;
} vehicle_profile_storage_t;

const vehicle_profile_t *vehicle_profile_get(void);
const vehicle_profile_t *vehicle_profile_get_default(void);

bool vehicle_profile_init(void);
bool vehicle_profile_load_all(void);
bool vehicle_profile_save(const vehicle_profile_t *profile, bool is_default);
bool vehicle_profile_delete(const char *profile_id);
bool vehicle_profile_set_active(const char *profile_id);
const vehicle_profile_t *vehicle_profile_get_by_id(const char *profile_id);
int vehicle_profile_get_count(void);
const vehicle_profile_t *vehicle_profile_get_at(int index);
const char *vehicle_profile_get_active_id(void);
void vehicle_profile_apply_known_pids(void);
