#include "obd_pids.h"
#include "obd_dtc.h"
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

#define VOLTAGE_POLL_MS    1500
#define VOLTAGE_MIN_V      9.0f
#define VOLTAGE_MAX_V      15.5f
#define ATRV_TIMEOUT_MS    1500
#define POLL_TASK_STACK   4096
#define POLL_TASK_PRIO    4

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
static bool s_discovery_done;
static uint32_t s_disc_sent_at;
static bool s_use_atrv;
static uint32_t s_voltage_last;
static uint32_t s_dtc_last;

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
    return ((a * 256.0f) + b) / 4.0f;
}

static float decode_speed(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return (float)a;
}

static float decode_temp(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return (float)a - 40.0f;
}

static float decode_pct(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return a * 100.0f / 255.0f;
}

static float decode_map(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return (float)a;
}

static float decode_timing(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return (a / 2.0f) - 64.0f;
}

static float decode_trim(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return ((a - 128.0f) * 100.0f) / 128.0f;
}

static float decode_o2_voltage(const char *hex)
{
    int a = 0;
    sscanf(hex, "%2x", &a);
    return a * 0.005f;
}

static float decode_voltage_ecu(const char *hex)
{
    int a = 0, b = 0;
    sscanf(hex, "%2x%2x", &a, &b);
    return ((a * 256.0f) + b) / 1000.0f;
}

static bool voltage_valid(float v)
{
    return v >= VOLTAGE_MIN_V && v <= VOLTAGE_MAX_V;
}

static void set_voltage(float v, const char *source)
{
    if (!voltage_valid(v)) {
        app_log_warn(TAG, "Reject voltage %.2fV from %s", v, source);
        return;
    }
    vehicle_data_t *vd = vehicle_data_get();
    vehicle_data_lock();
    float prev = vd->voltage;
    vd->voltage = v;
    vehicle_data_unlock();
    if (prev < 0.1f || (v > prev + 0.04f) || (v < prev - 0.04f)) {
        app_log_info(TAG, "Voltage %.2fV (%s)", v, source);
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

static float parse_atrv_voltage(const char *resp)
{
    if (!resp || resp[0] == '\0') {
        return 0.0f;
    }

    char line[48];
    size_t len = 0;
    while (resp[len] && resp[len] != '\r' && resp[len] != '\n' && len < sizeof(line) - 1) {
        line[len] = resp[len];
        len++;
    }
    line[len] = '\0';

    char *start = line;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    size_t slen = strlen(start);
    while (slen > 0 && (start[slen - 1] == 'V' || start[slen - 1] == 'v' ||
                        isspace((unsigned char)start[slen - 1]))) {
        start[--slen] = '\0';
    }

    float v = 0.0f;
    if (sscanf(start, "%f", &v) != 1) {
        return 0.0f;
    }

    const vehicle_profile_t *profile = vehicle_profile_get();
    if (profile->use_atrv_voltage && profile->atrv_voltage_scale > 0.0f &&
        profile->atrv_voltage_scale != 1.0f) {
        /* Clone ELM327 ATRV reads consistently high — always apply scale. */
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
    if (!vehicle_data_is_pid_supported(0x42)) {
        s_use_atrv = true;
        return;
    }
    const char *data = extract_data_bytes(resp, 0x42);
    if (!data) {
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
    float raw = 0.0f;
    if (resp && resp[0] != '\0') {
        char line[48];
        size_t len = 0;
        while (resp[len] && resp[len] != '\r' && resp[len] != '\n' && len < sizeof(line) - 1) {
            line[len] = resp[len];
            len++;
        }
        line[len] = '\0';
        sscanf(line, "%f", &raw);
    }

    float v = parse_atrv_voltage(resp);
    if (voltage_valid(v)) {
        set_voltage(v, "ATRV");
        return;
    }
    app_log_warn(TAG, "ATRV parse failed raw=%.2fV resp=%s", raw, resp ? resp : "(null)");
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

static void discover_cb(const char *resp, void *user_data)
{
    int block = (int)(intptr_t)user_data;
    if (!response_is_error(resp)) {
        parse_supported_pids(resp, block);
    }
    s_disc_idx = block + 1;
    s_disc_busy = false;
}

static const uint8_t s_dash_pids[] = { 0x0C, 0x0C, 0x0D, 0x0C, 0x05 };
static const uint8_t s_grid_pids[] = { 0x11, 0x0B, 0x04, 0x0F, 0x0E, 0x06, 0x07, 0x14, 0x15 };

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

    /* Kalos/KWP: ECU has no PID 0x42 — use ATRV only when profile says so. */
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
    for (size_t n = 0; n < sizeof(s_dash_pids) / sizeof(s_dash_pids[0]); n++) {
        size_t idx = (s_dash_slot + n) % (sizeof(s_dash_pids) / sizeof(s_dash_pids[0]));
        if (poll_entry_by_pid(s_dash_pids[idx], now, true)) {
            s_dash_slot = (idx + 1) % (sizeof(s_dash_pids) / sizeof(s_dash_pids[0]));
            return true;
        }
    }
    return false;
}

static bool run_grid_poll(uint32_t now)
{
    size_t count = sizeof(s_grid_pids) / sizeof(s_grid_pids[0]);

    for (size_t n = 0; n < count; n++) {
        size_t idx = (s_grid_slot + n) % count;
        if (poll_entry_by_pid(s_grid_pids[idx], now, false)) {
            s_grid_slot = (idx + 1) % count;
            return true;
        }
    }
    return false;
}

static bool profile_has_known_pids(void)
{
    const vehicle_profile_t *p = vehicle_profile_get();
    for (int i = 0; i < 4; i++) {
        if (p->known_pid_masks[i] != 0) {
            return true;
        }
    }
    return false;
}

static void poll_task(void *arg)
{
    static const char *disc_cmds[] = { "0100", "0120" };

    init_poll_entries();

    while (1) {
        const vehicle_profile_t *profile = vehicle_profile_get();
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (!elm327_is_ready()) {
            s_disc_idx = 0;
            s_disc_busy = false;
            s_discovery_done = false;
            s_use_atrv = profile->use_atrv_voltage;
            obd_dtc_reset();
            init_poll_entries();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!s_discovery_done) {
            vehicle_data_set_state(OBD_STATE_PID_DISCOVERY, "Discovering PIDs...");
            poll_voltage(now);
            if (profile_has_known_pids()) {
                vehicle_profile_apply_known_pids();
                s_discovery_done = true;
                s_voltage_last = 0;
                vehicle_data_set_state(OBD_STATE_READY, "Connected");
                app_log_info(TAG, "Ready (known PIDs): %s", profile->display_name);
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            if (s_disc_idx < 2) {
                if (s_disc_busy && now - s_disc_sent_at > profile->disc_timeout_ms + 300) {
                    s_disc_busy = false;
                    s_disc_idx++;
                }
                if (!s_disc_busy &&
                    elm327_send_cmd(disc_cmds[s_disc_idx], discover_cb,
                                    (void *)(intptr_t)s_disc_idx, profile->disc_timeout_ms)) {
                    s_disc_busy = true;
                    s_disc_sent_at = now;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            vehicle_profile_apply_known_pids();
            s_discovery_done = true;
            s_voltage_last = 0;
            vehicle_data_set_state(OBD_STATE_READY, "Connected");
            app_log_info(TAG, "Ready: %s", profile->display_name);
        }

        int tab = ui_get_active_tab();
        bool dash_focus = tab == UI_TAB_DASH;
        bool grid_focus = tab == UI_TAB_GRID;
        bool dtc_focus = tab == UI_TAB_DTC;

        /* ATRV on its own timer — not starved by RPM/speed round-robin. */
        poll_voltage(now);

        if (dtc_focus) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        bool queued = false;
        if (dash_focus) {
            queued = run_dash_poll(now);
        } else if (grid_focus) {
            queued = run_grid_poll(now);
            if (!obd_dtc_is_busy() && now - s_dtc_last >= 30000) {
                s_dtc_last = now;
                obd_dtc_read_all();
            }
        } else {
            queued = run_dash_poll(now);
        }

        vTaskDelay(pdMS_TO_TICKS(queued ? 2 : 5));
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
