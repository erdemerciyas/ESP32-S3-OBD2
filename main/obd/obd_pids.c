#include "obd_pids.h"
#include "elm327.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "ui.h"
#include "app_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "obd_pids";

#define VOLTAGE_POLL_MS    1000
#define VOLTAGE_MIN_V      9.0f
#define VOLTAGE_MAX_V      15.5f
#define ATRV_TIMEOUT_MS    1200

/* EMA filter: lower = smoother (0.3 gives ~3-sample averaging effect) */
#define VOLTAGE_EMA_ALPHA  0.30f
/* Reject readings that deviate more than this from filtered value */
#define VOLTAGE_SPIKE_MAX  0.60f
/* Accept larger jump on first valid reading (cold start) */
#define VOLTAGE_INIT_JUMP  3.0f
#define POLL_TASK_STACK   4096
#define POLL_TASK_PRIO    6

/* Value range limits for spike filtering */
#define RPM_MIN            0.0f
#define RPM_MAX            8000.0f
#define SPEED_MIN          0.0f
#define SPEED_MAX          300.0f
#define TEMP_MIN           -40.0f
#define TEMP_MAX           150.0f
#define PCT_MIN            0.0f
#define PCT_MAX            100.0f
#define MAP_MIN            0.0f
#define MAP_MAX            260.0f
#define TIMING_MIN         -64.0f
#define TIMING_MAX         64.0f
#define TRIM_MIN           -100.0f
#define TRIM_MAX           100.0f
#define O2_VOLT_MIN        0.0f
#define O2_VOLT_MAX        1.5f

static float clampf(float val, float lo, float hi);

typedef struct {
    char cmd[8];
    uint8_t pid;
    uint32_t interval_ms;
    uint32_t last_poll;
    bool priority;
    bool live;
} pid_entry_t;

static TaskHandle_t s_poll_task;
static size_t s_dash_slot;
static size_t s_grid_slot;
static int s_disc_idx;
static bool s_disc_busy;
static bool s_disc_ready;
static bool s_disc_complete;
static uint32_t s_disc_sent_at;
static bool s_use_atrv;
static uint32_t s_voltage_last;

/* Voltage EMA filter state */
static float s_voltage_filtered;
static bool  s_voltage_filter_init;
static uint8_t s_voltage_spike_count;

static pid_entry_t s_live_entries[8];
static pid_entry_t s_fast_entries[12];
static pid_entry_t s_slow_entries[12];
static size_t s_live_count;
static size_t s_fast_count;
static size_t s_slow_count;

static bool has_any_supported_pids(void)
{
    vehicle_data_t *vd = vehicle_data_get();
    for (int i = 0; i < 4; i++) {
        if (vd->supported_pids[i] != 0) {
            return true;
        }
    }
    return false;
}

static bool should_poll_pid(const pid_entry_t *e)
{
    if (e->pid == 0) {
        return false;
    }
    if (e->priority) {
        return true;
    }
    if (!has_any_supported_pids()) {
        return true;
    }
    return vehicle_data_is_pid_supported(e->pid);
}

static void parse_supported_pids(const char *resp, int block);
static void pid_response_cb(const char *resp, void *user_data);
static void discover_cb(const char *resp, void *user_data);
static void atrv_response_cb(const char *resp, void *user_data);
static void voltage_pid_cb(const char *resp, void *user_data);
static void poll_task(void *arg);

static void init_poll_entries(void)
{
    const vehicle_profile_t *profile = vehicle_profile_get();

    s_live_count = 0;
    for (size_t i = 0; i < profile->live_count && i < sizeof(s_live_entries) / sizeof(s_live_entries[0]); i++) {
        const vehicle_pid_poll_t *src = &profile->live_pids[i];
        pid_entry_t *dst = &s_live_entries[s_live_count++];
        snprintf(dst->cmd, sizeof(dst->cmd), "01%02X", src->pid);
        dst->pid = src->pid;
        dst->interval_ms = src->interval_ms;
        dst->last_poll = 0;
        dst->priority = src->priority;
        dst->live = src->live;
    }

    s_fast_count = 0;
    for (size_t i = 0; i < profile->fast_count && i < sizeof(s_fast_entries) / sizeof(s_fast_entries[0]); i++) {
        const vehicle_pid_poll_t *src = &profile->fast_pids[i];
        pid_entry_t *dst = &s_fast_entries[s_fast_count++];
        snprintf(dst->cmd, sizeof(dst->cmd), "01%02X", src->pid);
        dst->pid = src->pid;
        dst->interval_ms = src->interval_ms;
        dst->last_poll = 0;
        dst->priority = src->priority;
        dst->live = src->live;
    }

    s_slow_count = 0;
    for (size_t i = 0; i < profile->slow_count && i < sizeof(s_slow_entries) / sizeof(s_slow_entries[0]); i++) {
        const vehicle_pid_poll_t *src = &profile->slow_pids[i];
        pid_entry_t *dst = &s_slow_entries[s_slow_count++];
        snprintf(dst->cmd, sizeof(dst->cmd), "01%02X", src->pid);
        dst->pid = src->pid;
        dst->interval_ms = src->interval_ms;
        dst->last_poll = 0;
        dst->priority = src->priority;
        dst->live = src->live;
    }

    s_use_atrv = profile->use_atrv_voltage;
}

static float decode_rpm(const char *hex)
{
    int a = 0, b = 0;
    sscanf(hex, "%2x%2x", &a, &b);
    return clampf(((a * 256.0f) + b) / 4.0f, RPM_MIN, RPM_MAX);
}

static float decode_speed(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return clampf((float)a, SPEED_MIN, SPEED_MAX);
}

static float decode_temp(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return clampf((float)a - 40.0f, TEMP_MIN, TEMP_MAX);
}

static float decode_pct(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return clampf(a * 100.0f / 255.0f, PCT_MIN, PCT_MAX);
}

static float decode_map(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return clampf((float)a, MAP_MIN, MAP_MAX);
}

static float decode_timing(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return clampf((a / 2.0f) - 64.0f, TIMING_MIN, TIMING_MAX);
}

static float decode_trim(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return clampf(((a - 128.0f) * 100.0f) / 128.0f, TRIM_MIN, TRIM_MAX);
}

static float decode_o2_voltage(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return clampf(a * 0.005f, O2_VOLT_MIN, O2_VOLT_MAX);
}

static float decode_maf(const char *hex)
{
    int a = 0, b = 0;
    sscanf(hex, "%2x%2x", &a, &b);
    return ((a * 256.0f) + b) / 100.0f;
}

static float decode_voltage_ecu(const char *hex)
{
    int a = 0, b = 0;
    sscanf(hex, "%2x%2x", &a, &b);
    return ((a * 256.0f) + b) / 1000.0f;
}

static float clampf(float val, float lo, float hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static bool voltage_valid(float v)
{
    return v >= VOLTAGE_MIN_V && v <= VOLTAGE_MAX_V;
}

/**
 * Apply EMA filter + spike rejection to raw voltage.
 * Returns true if the value was accepted and stored.
 */
static bool voltage_filter_apply(float raw)
{
    if (!s_voltage_filter_init) {
        /* First valid reading — seed the filter directly */
        s_voltage_filtered = raw;
        s_voltage_filter_init = true;
        s_voltage_spike_count = 0;
        return true;
    }

    float diff = raw - s_voltage_filtered;
    if (diff < 0) diff = -diff;

    /* Spike rejection: if reading is too far from filtered, reject it.
     * Allow up to 3 consecutive spikes before resetting the filter
     * (handles genuine rapid changes like engine start). */
    if (diff > VOLTAGE_SPIKE_MAX) {
        s_voltage_spike_count++;
        if (s_voltage_spike_count < 3) {
            app_log_warn(TAG, "Voltage spike rejected: %.2fV (filtered=%.2fV, diff=%.2fV)",
                         raw, s_voltage_filtered, diff);
            return false;
        }
        /* 3+ consecutive spikes: accept as real change, reset filter */
        app_log_info(TAG, "Voltage filter reset: %.2fV -> %.2fV (forced)",
                     s_voltage_filtered, raw);
        s_voltage_filtered = raw;
        s_voltage_spike_count = 0;
        return true;
    }

    /* Normal EMA update */
    s_voltage_filtered = s_voltage_filtered + VOLTAGE_EMA_ALPHA * (raw - s_voltage_filtered);
    s_voltage_spike_count = 0;
    return true;
}

static void set_voltage(float v, const char *source)
{
    if (!voltage_valid(v)) {
        app_log_warn(TAG, "Reject voltage %.2fV from %s (out of range)", v, source);
        return;
    }
    if (!voltage_filter_apply(v)) {
        return;
    }

    vehicle_data_t *vd = vehicle_data_get();
    vehicle_data_lock();
    float prev = vd->voltage;
    vd->voltage = s_voltage_filtered;
    vehicle_data_unlock();

    if (prev < 0.1f || (s_voltage_filtered > prev + 0.03f) || (s_voltage_filtered < prev - 0.03f)) {
        app_log_info(TAG, "Voltage %.2fV raw=%.2fV (%s)", s_voltage_filtered, v, source);
    }
}

static const char *extract_data_bytes(const char *resp, uint8_t expected_pid)
{
    const char *p = resp;
    while ((p = strstr(p, "41")) != NULL) {
        int pid = 0;
        if (sscanf(p + 2, "%2x", &pid) == 1 && pid == expected_pid) {
            return p + 4;
        }
        p += 2;
    }
    return NULL;
}

static bool response_is_error(const char *resp)
{
    return strstr(resp, "NO DATA") != NULL ||
           strstr(resp, "UNABLE") != NULL ||
           strstr(resp, "ERROR") != NULL;
}

/**
 * Parse ATRV voltage response from ELM327.
 * Typical responses: "14.8V", "14.8", " 14.8V\r\n"
 * Returns 0.0f on parse failure.
 */
static float parse_atrv_voltage(const char *resp)
{
    if (!resp || resp[0] == '\0') {
        return 0.0f;
    }

    /* Skip leading whitespace */
    const char *p = resp;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    /* Parse the float value directly — stop at V/v or whitespace */
    float v = 0.0f;
    if (sscanf(p, "%f", &v) != 1) {
        return 0.0f;
    }

    /* Apply calibration scale if configured */
    const vehicle_profile_t *profile = vehicle_profile_get();
    if (profile->use_atrv_voltage && profile->atrv_voltage_scale > 0.0f &&
        profile->atrv_voltage_scale != 1.0f) {
        v *= profile->atrv_voltage_scale;
    }

    return voltage_valid(v) ? v : 0.0f;
}

static void update_pid_value(uint8_t pid, const char *resp)
{
    vehicle_data_t *vd = vehicle_data_get();
    const char *data = extract_data_bytes(resp, pid);
    if (!data) {
        return;
    }

    switch (pid) {
    case 0x0C:
        vehicle_data_set_float(&vd->rpm, decode_rpm(data));
        break;
    case 0x0D:
        vehicle_data_set_float(&vd->speed, decode_speed(data));
        break;
    case 0x05:
        vehicle_data_set_float(&vd->coolant, decode_temp(data));
        break;
    case 0x11:
        vehicle_data_set_float(&vd->throttle, decode_pct(data));
        break;
    case 0x0B:
        vehicle_data_set_float(&vd->map, decode_map(data));
        break;
    case 0x0F:
        vehicle_data_set_float(&vd->iat, decode_temp(data));
        break;
    case 0x0E:
        vehicle_data_set_float(&vd->timing, decode_timing(data));
        break;
    case 0x06:
        vehicle_data_set_float(&vd->fuel_trim_st, decode_trim(data));
        break;
    case 0x07:
        vehicle_data_set_float(&vd->fuel_trim_lt, decode_trim(data));
        break;
    case 0x04:
        vehicle_data_set_float(&vd->load, decode_pct(data));
        break;
    case 0x03: {
        int sys1 = 0, sys2 = 0;
        sscanf(data, "%2x%2x", &sys1, &sys2);
        vehicle_data_set_float(&vd->fuel_system1, (float)sys1);
        vehicle_data_set_float(&vd->fuel_system2, (float)sys2);
        break;
    }
    case 0x12:
        vehicle_data_set_float(&vd->secondary_air, decode_map(data));
        break;
    case 0x14:
        vehicle_data_set_float(&vd->o2_voltage, decode_o2_voltage(data));
        break;
    case 0x15:
        vehicle_data_set_float(&vd->o2_b1s2, decode_o2_voltage(data));
        break;
    case 0x10:
        vehicle_data_set_float(&vd->maf, decode_maf(data));
        break;
    default:
        break;
    }
}

static void pid_response_cb(const char *resp, void *user_data)
{
    uint8_t pid = (uint8_t)(uintptr_t)user_data;
    if (response_is_error(resp)) {
        return;
    }
    update_pid_value(pid, resp);
}

static void voltage_pid_cb(const char *resp, void *user_data)
{
    (void)user_data;
    if (response_is_error(resp)) {
        s_use_atrv = true;
        app_log_info(TAG, "PID 0x42 unavailable, using ATRV");
        return;
    }
    const char *data = extract_data_bytes(resp, 0x42);
    if (!data) {
        s_use_atrv = true;
        return;
    }
    float v = decode_voltage_ecu(data);
    if (voltage_valid(v)) {
        set_voltage(v, "PID 0x42");
        s_use_atrv = false;
    }
}

static void atrv_response_cb(const char *resp, void *user_data)
{
    (void)user_data;
    float v = parse_atrv_voltage(resp);
    if (voltage_valid(v)) {
        set_voltage(v, "ATRV");
        return;
    }
    app_log_warn(TAG, "ATRV parse failed resp=%s", resp ? resp : "(null)");
}

static void parse_supported_pids(const char *resp, int block)
{
    char *p = strstr(resp, "41");
    if (!p) {
        return;
    }
    int base = 0;
    sscanf(p + 2, "%2x", &base);

    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        int byte = 0;
        sscanf(p + 4 + i * 2, "%2x", &byte);
        for (int b = 0; b < 8; b++) {
            int pid = base + i * 8 + b + 1;
            if ((byte >> (7 - b)) & 1) {
                int bit_idx = pid - 1;
                if (bit_idx / 32 == block) {
                    mask |= (1u << (bit_idx % 32));
                }
            }
        }
    }
    vehicle_data_set_pid_supported(block, mask);
}

#define DASH_PID_RPM      0x0C

#define PID_DISC_BLOCK_COUNT 7
static const char *const s_disc_cmds[PID_DISC_BLOCK_COUNT] = {
    "0100", "0120", "0140", "0160", "0180", "01A0", "01C0",
};

static void mark_disc_ready(void)
{
    if (s_disc_ready) {
        return;
    }
    s_disc_ready = true;
    s_voltage_last = 0;
    vehicle_data_set_state(OBD_STATE_READY, "Connected");
    app_log_info(TAG, "Ready: %s (PID scan continues)", vehicle_profile_get()->display_name);
}

static void mark_disc_complete(void)
{
    if (s_disc_complete) {
        return;
    }
    s_disc_complete = true;
    vehicle_profile_apply_known_pids();
    app_log_info(TAG, "PID discovery complete");
}

static void discover_cb(const char *resp, void *user_data)
{
    int block = (int)(intptr_t)user_data;
    if (!response_is_error(resp)) {
        parse_supported_pids(resp, block);
    }
    s_disc_idx = block + 1;
    s_disc_busy = false;

    if (block == 0) {
        mark_disc_ready();
    }
    if (s_disc_idx >= PID_DISC_BLOCK_COUNT) {
        mark_disc_complete();
    }
}

static void advance_disc_on_timeout(uint32_t now)
{
    const vehicle_profile_t *profile = vehicle_profile_get();
    if (!s_disc_busy || now - s_disc_sent_at <= profile->disc_timeout_ms + 300) {
        return;
    }

    int timed_block = s_disc_idx;
    s_disc_busy = false;
    s_disc_idx++;

    if (timed_block == 0) {
        mark_disc_ready();
    }
    if (s_disc_idx >= PID_DISC_BLOCK_COUNT) {
        mark_disc_complete();
    }
}

static bool try_send_disc_block(bool high_priority)
{
    if (s_disc_complete || s_disc_busy || s_disc_idx >= PID_DISC_BLOCK_COUNT) {
        return false;
    }
    if (!elm327_can_queue(high_priority)) {
        return false;
    }

    const vehicle_profile_t *profile = vehicle_profile_get();
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (!elm327_send_cmd_prio(s_disc_cmds[s_disc_idx], discover_cb,
                              (void *)(intptr_t)s_disc_idx, profile->disc_timeout_ms,
                              high_priority)) {
        return false;
    }

    s_disc_busy = true;
    s_disc_sent_at = now;
    if (!s_disc_ready) {
        vehicle_data_set_state(OBD_STATE_PID_DISCOVERY, "Discovering PIDs...");
    }
    return true;
}

static pid_entry_t *find_entry_by_pid(uint8_t pid)
{
    for (size_t i = 0; i < s_live_count; i++) {
        if (s_live_entries[i].pid == pid) {
            return &s_live_entries[i];
        }
    }
    for (size_t i = 0; i < s_fast_count; i++) {
        if (s_fast_entries[i].pid == pid) {
            return &s_fast_entries[i];
        }
    }
    for (size_t i = 0; i < s_slow_count; i++) {
        if (s_slow_entries[i].pid == pid) {
            return &s_slow_entries[i];
        }
    }
    return NULL;
}

static bool poll_due(const pid_entry_t *e, uint32_t now)
{
    return now - e->last_poll >= e->interval_ms;
}

static bool send_pid_poll(pid_entry_t *e, uint32_t now, bool high_priority)
{
    if (!elm327_can_queue(high_priority)) {
        return false;
    }
    const vehicle_profile_t *profile = vehicle_profile_get();
    if (!elm327_send_cmd_prio(e->cmd, pid_response_cb, (void *)(uintptr_t)e->pid,
                              e->live ? profile->live_timeout_ms : profile->slow_timeout_ms,
                              high_priority)) {
        return false;
    }
    e->last_poll = now;
    return true;
}

static bool poll_entry_by_pid(uint8_t pid, uint32_t now, bool high_priority)
{
    pid_entry_t *e = find_entry_by_pid(pid);
    if (!e || !should_poll_pid(e) || !poll_due(e, now)) {
        return false;
    }
    return send_pid_poll(e, now, high_priority);
}

static bool poll_voltage(uint32_t now)
{
    const vehicle_profile_t *profile = vehicle_profile_get();

    if (now - s_voltage_last < VOLTAGE_POLL_MS) {
        return false;
    }
    if (!elm327_can_queue(true)) {
        return false;
    }

    /* Try PID 0x42 first; fall back to ATRV when ECU or profile requires it. */
    if (!profile->use_atrv_voltage && !s_use_atrv) {
        if (elm327_send_cmd_prio("0142", voltage_pid_cb, NULL, profile->slow_timeout_ms, true)) {
            s_voltage_last = now;
            return true;
        }
        /* queue full; try ATRV as immediate fallback */
    }

    if (profile->use_atrv_voltage || s_use_atrv) {
        if (elm327_send_cmd_prio("ATRV", atrv_response_cb, NULL, ATRV_TIMEOUT_MS, true)) {
            s_voltage_last = now;
            return true;
        }
    }
    return false;
}

static bool run_dash_poll(uint32_t now)
{
    /* Dashboard mode: RPM always wins when due for smooth gauge. */
    if (poll_entry_by_pid(DASH_PID_RPM, now, true)) {
        return true;
    }

    for (size_t n = 0; n < s_live_count; n++) {
        size_t idx = (s_dash_slot + n) % s_live_count;
        uint8_t pid = s_live_entries[idx].pid;
        if (pid == DASH_PID_RPM) {
            continue;
        }
        if (poll_entry_by_pid(pid, now, true)) {
            s_dash_slot = (idx + 1) % s_live_count;
            return true;
        }
    }
    return false;
}

static bool run_grid_poll(uint32_t now)
{
    size_t total = s_fast_count + s_slow_count;
    if (total == 0) {
        return false;
    }

    for (size_t n = 0; n < total; n++) {
        size_t idx = (s_grid_slot + n) % total;
        uint8_t pid = (idx < s_fast_count)
                          ? s_fast_entries[idx].pid
                          : s_slow_entries[idx - s_fast_count].pid;
        if (poll_entry_by_pid(pid, now, false)) {
            s_grid_slot = (idx + 1) % total;
            return true;
        }
    }
    return false;
}

static void poll_task(void *arg)
{
    init_poll_entries();

    while (1) {
        const vehicle_profile_t *profile = vehicle_profile_get();
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (!elm327_is_ready()) {
            s_disc_idx = 0;
            s_disc_busy = false;
            s_disc_ready = false;
            s_disc_complete = false;
            s_use_atrv = profile->use_atrv_voltage;
            s_voltage_filter_init = false;
            s_voltage_filtered = 0.0f;
            s_voltage_spike_count = 0;
            init_poll_entries();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        advance_disc_on_timeout(now);

        if (!s_disc_ready) {
            poll_voltage(now);
            try_send_disc_block(true);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        int tab = ui_get_active_tab();
        bool grid_focus = tab == UI_TAB_GRID;

        bool queued = false;
        if (grid_focus) {
            poll_voltage(now);
            queued = run_grid_poll(now);
        } else {
            queued = run_dash_poll(now);
            if (!queued) {
                queued = poll_voltage(now);
            }
        }

        if (!queued) {
            try_send_disc_block(false);
        }

        vTaskDelay(pdMS_TO_TICKS(queued ? 1 : 4));
    }
}

void obd_pids_init(void)
{
}

void obd_pids_start(void)
{
    xTaskCreatePinnedToCore(poll_task, "obd_poll", POLL_TASK_STACK, NULL, POLL_TASK_PRIO,
                            &s_poll_task, 0);
}

void obd_pids_stop(void)
{
    if (s_poll_task) {
        vTaskDelete(s_poll_task);
        s_poll_task = NULL;
    }
}

void obd_pids_discover(void)
{
}
