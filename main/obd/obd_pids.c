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

#define VOLTAGE_POLL_MS    200
#define VOLTAGE_MIN_V      9.0f
#define VOLTAGE_MAX_V      16.5f
/* ATRV timeout: previously 800 ms — a single stuck ATRV blocked the ELM327
 * for 800 ms starving all other PIDs (coolant, RPM, Speed).
 * 300 ms is still generous for a local AT command response. */
#define ATRV_TIMEOUT_MS    150
/* Periyodik PID 0x42 re-probe: ATRV modundayken bile ara sıra dene
 * (bazı araçlarda init sonrası 0x42 çalışır hale gelir). */
#define PID42_REPROBE_MS   30000
/* Out-of-range sayaç: ardışık OOR okumaları bu eşiği aşarsa ATRV'ye geç */
#define PID42_OOR_FALLBACK 3

/* EMA filter: higher = faster response (less latency) */
#define VOLTAGE_EMA_ALPHA  0.70f
/* Reject readings that deviate more than this from filtered value */
#define VOLTAGE_SPIKE_MAX  0.50f
/* Accept larger jump on first valid reading (cold start) */
#define VOLTAGE_INIT_JUMP  3.0f
#define POLL_TASK_STACK   4096
#define POLL_TASK_PRIO    6

/* Per-PID filter: EMA + spike rejection.
 * Sıralı okumalarda diff > spike_max ise reddedilir; 3 ardışık spike'da
 * filtre resetlenir (gerçek hızlı değişim kabul edilir). */
typedef struct {
    float    filtered;
    bool     init;
    uint8_t  spike_count;
} pid_filter_t;

/* Per-PID konfigürasyon: alpha (EMA) + spike_max.
 * İndeks doğrudan PID değeridir (örn. RPM 0x0C → s_pid_filter_cfgs[0x0C]).
 * Kullanılmayan PID'ler için alpha=0 ile raw kullanılır.
 *
 * Alpha: yüksek = daha hızlı tepki (düşük gecikme), düşük = daha yumuşak.
 * spike_max: bu eşik üstü tek okuma reddedilir, 3 ardışık aşımda reset. */
typedef struct {
    float alpha;
    float spike_max;
} pid_filter_cfg_t;

static const pid_filter_cfg_t s_pid_filter_cfgs[256] = {
    /* 0x04 LOAD          */ [0x04] = { 0.65f,  30.0f },
    /* 0x05 COOLANT       */ [0x05] = { 0.55f,  15.0f },
    /* 0x06 STFT          */ [0x06] = { 0.40f,  15.0f },
    /* 0x07 LTFT          */ [0x07] = { 0.40f,  15.0f },
    /* 0x0A FUEL_PRESS    */ [0x0A] = { 0.40f,  80.0f },
    /* 0x0B MAP           */ [0x0B] = { 0.70f,  30.0f },
    /* 0x0C RPM           */ [0x0C] = { 0.90f, 1500.0f },
    /* 0x0D SPEED         */ [0x0D] = { 0.94f,  30.0f },
    /* 0x0E TIMING        */ [0x0E] = { 0.60f,  15.0f },
    /* 0x0F IAT           */ [0x0F] = { 0.55f,  20.0f },
    /* 0x10 MAF           */ [0x10] = { 0.60f,  30.0f },
    /* 0x11 TPS           */ [0x11] = { 0.70f,  30.0f },
    /* 0x14 O2 B1S1       */ [0x14] = { 0.50f,   0.8f },
    /* 0x15 O2 B1S2       */ [0x15] = { 0.50f,   0.8f },
    /* 0x2F FUEL LEVEL    */ [0x2F] = { 0.35f,  10.0f },
    /* 0x42 VOLTAGE       */ [0x42] = { VOLTAGE_EMA_ALPHA, VOLTAGE_SPIKE_MAX },
    /* Diğer PIDs: sıfır alpha → decode sonrası ham kullanılır (filtre yok) */
};

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
    uint32_t last_poll;    /* response alındığı anki zaman; pending false iken güncellenir */
    bool priority;
    bool live;
    bool pending;          /* cevap beklenirken true; tekrar queue yapmayı engeller */
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
static bool s_voltage_pending;      /* ATRV/0142 cevap beklerken true */
static bool s_reprobe_inflight;     /* ATRV modunda geçici 0142 re-probe uçuşta mı */
static uint32_t s_042_reprobe_last;

/* RPM+Speed tek komut batch ("010C0D") — RPM ve Speed'i tek BLE round-trip'te
 * senkron getirir. Varsayılan KAPALI; başarılı probe'dan sonra açılır, 3
 * başarısızlıkta ayrı polling'e geri döner (regresyonsuz). */
#define DASH_BATCH_CMD    "010C0D"
#define BATCH_INTERVAL_MS 60
#define BATCH_PROBE_MS    1000
#define BATCH_FAIL_MAX    3
static int8_t   s_batch_state;      /* 0=bilinmiyor(probe), 1=açık, -1=kapalı */
static uint8_t  s_batch_fail;
static bool     s_batch_pending;
static uint32_t s_batch_last;
static uint8_t  s_voltage_oor_count;

/* Tüm PID'ler için EMA + spike rejection state. İndeks = PID değeri.
 * Voltaj da dahil (s_pid_filter_cfgs[0x42] ile). */
static pid_filter_t s_pid_filters[256];

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
        dst->pending = false;
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
        dst->pending = false;
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
        dst->pending = false;
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
 * Genel PID filtresi: EMA + spike rejection.
 * İlk geçerli okumada direkt seed edilir (cold start).
 * diff > spike_max ise reddedilir; 3 ardışık spike'da filtre resetlenir.
 *
 * Returns true → değer kabul edildi (filtered güncellendi, vehicle_data_set_float çağır)
 * Returns false → spike, değer reddedildi (mevcut değer korunur)
 */
static bool pid_filter_apply(pid_filter_t *f, float raw, const pid_filter_cfg_t *cfg)
{
    if (cfg->alpha <= 0.0f) {
        /* Filtre yok — ham değer kabul */
        f->filtered = raw;
        f->init = true;
        f->spike_count = 0;
        return true;
    }

    if (!f->init) {
        /* Cold start: ilk geçerli okuma filtreyi direkt set eder */
        f->filtered = raw;
        f->init = true;
        f->spike_count = 0;
        return true;
    }

    float diff = raw - f->filtered;
    if (diff < 0) diff = -diff;

    if (diff > cfg->spike_max) {
        f->spike_count++;
        if (f->spike_count < 3) {
            app_log_warn(TAG, "PID 0x%02X spike rejected: raw=%.2f filtered=%.2f (diff=%.2f)",
                         (unsigned)(cfg - s_pid_filter_cfgs), raw, f->filtered, diff);
            return false;
        }
        /* 3+ ardışık spike: gerçek hızlı değişim, reset */
        app_log_info(TAG, "PID 0x%02X filter reset: %.2f -> %.2f (forced)",
                     (unsigned)(cfg - s_pid_filter_cfgs), f->filtered, raw);
        f->filtered = raw;
        f->spike_count = 0;
        return true;
    }

    /* Normal EMA update */
    f->filtered = f->filtered + cfg->alpha * (raw - f->filtered);
    f->spike_count = 0;
    return true;
}

static void set_voltage(float v, const char *source)
{
    if (!voltage_valid(v)) {
        app_log_warn(TAG, "Reject voltage %.2fV from %s (out of range)", v, source);
        return;
    }
    pid_filter_t *f = &s_pid_filters[0x42];
    if (!pid_filter_apply(f, v, &s_pid_filter_cfgs[0x42])) {
        return;  /* spike rejected */
    }

    vehicle_data_set_voltage(f->filtered);

    static float s_prev_log_v = -999.0f;
    if (s_prev_log_v < 0.0f || (f->filtered > s_prev_log_v + 0.03f) || (f->filtered < s_prev_log_v - 0.03f)) {
        s_prev_log_v = f->filtered;
        app_log_info(TAG, "Voltage %.2fV raw=%.2fV (%s)", f->filtered, v, source);
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

/**
 * Decode sonrası filtre uygula, kabul edilirse hedef alana yaz.
 * Filter spike_max delta'sı aşılırsa mevcut değer korunur (ekranda pik görünmez).
 */
static void apply_filtered_float(uint8_t pid, float raw, float *target)
{
    if (pid_filter_apply(&s_pid_filters[pid], raw, &s_pid_filter_cfgs[pid])) {
        /* Key dashboard PIDs use timestamp-aware setters so the UI can
         * detect stale values. Others use the generic pointer-based setter. */
        switch (pid) {
        case 0x0C: vehicle_data_set_rpm(s_pid_filters[pid].filtered);     break;
        case 0x0D: vehicle_data_set_speed(s_pid_filters[pid].filtered);   break;
        case 0x05: vehicle_data_set_coolant(s_pid_filters[pid].filtered); break;
        default:   vehicle_data_set_float(target, s_pid_filters[pid].filtered); break;
        }
    }
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
        apply_filtered_float(0x0C, decode_rpm(data), &vd->rpm);
        break;
    case 0x0D:
        apply_filtered_float(0x0D, decode_speed(data), &vd->speed);
        break;
    case 0x05:
        apply_filtered_float(0x05, decode_temp(data), &vd->coolant);
        break;
    case 0x11:
        apply_filtered_float(0x11, decode_pct(data), &vd->throttle);
        break;
    case 0x0B:
        apply_filtered_float(0x0B, decode_map(data), &vd->map);
        break;
    case 0x0F:
        apply_filtered_float(0x0F, decode_temp(data), &vd->iat);
        break;
    case 0x0E:
        apply_filtered_float(0x0E, decode_timing(data), &vd->timing);
        break;
    case 0x06:
        apply_filtered_float(0x06, decode_trim(data), &vd->fuel_trim_st);
        break;
    case 0x07:
        apply_filtered_float(0x07, decode_trim(data), &vd->fuel_trim_lt);
        break;
    case 0x04:
        apply_filtered_float(0x04, decode_pct(data), &vd->load);
        break;
    case 0x0A: {
        /* Fuel pressure — decode_map gibi 1-byte direkt değer */
        int a = 0;
        sscanf(data, "%2x", &a);
        apply_filtered_float(0x0A, (float)a, &vd->fuel_pressure);
        break;
    }
    case 0x03: {
        /* Fuel system status — enum, ham kullanılır (alpha=0) */
        int sys1 = 0, sys2 = 0;
        sscanf(data, "%2x%2x", &sys1, &sys2);
        apply_filtered_float(0x03, (float)sys1, &vd->fuel_system1);
        apply_filtered_float(0x03, (float)sys2, &vd->fuel_system2);
        break;
    }
    case 0x12: {
        /* Secondary air status — ham kullanılır */
        int a = 0;
        sscanf(data, "%2x", &a);
        apply_filtered_float(0x12, (float)a, &vd->secondary_air);
        break;
    }
    case 0x14:
        apply_filtered_float(0x14, decode_o2_voltage(data), &vd->o2_voltage);
        break;
    case 0x15:
        apply_filtered_float(0x15, decode_o2_voltage(data), &vd->o2_b1s2);
        break;
    case 0x2F:
        /* Fuel Level Input — A * 100 / 255  (same as decode_pct) */
        apply_filtered_float(0x2F, decode_pct(data), &vd->fuel_level);
        break;
    case 0x10:
        apply_filtered_float(0x10, decode_maf(data), &vd->maf);
        break;
    default:
        break;
    }
}

static pid_entry_t *find_entry_by_pid(uint8_t pid);

static void pid_response_cb(const char *resp, void *user_data)
{
    uint8_t pid = (uint8_t)(uintptr_t)user_data;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    pid_entry_t *e = find_entry_by_pid(pid);
    if (e) {
        e->pending = false;
    }

    if (response_is_error(resp)) {
        if (e) {
            e->last_poll = now;
        }
        return;
    }

    const char *data = extract_data_bytes(resp, pid);
    if (!data) {
        app_log_warn(TAG, "PID 0x%02X response mismatch, re-polling", pid);
        if (e) {
            e->last_poll = 0;
        }
        return;
    }

    if (e) {
        e->last_poll = now;
    }
    update_pid_value(pid, resp);
}

static void voltage_pid_cb(const char *resp, void *user_data)
{
    (void)user_data;
    s_voltage_pending = false;
    s_reprobe_inflight = false;
    if (response_is_error(resp)) {
        s_use_atrv = true;
        s_voltage_oor_count = 0;
        app_log_info(TAG, "PID 0x42 unsupported (NO DATA/ERROR), using ATRV");
        return;
    }
    const char *data = extract_data_bytes(resp, 0x42);
    if (!data) {
        s_use_atrv = true;
        s_voltage_oor_count = 0;
        app_log_warn(TAG, "PID 0x42 parse failed, using ATRV");
        return;
    }
    float v = decode_voltage_ecu(data);
    if (voltage_valid(v)) {
        set_voltage(v, "PID 0x42");
        s_use_atrv = false;
        s_voltage_oor_count = 0;
        return;
    }
    /* Geçerli format ama aralık dışı değer — clone yanlış encoding kullanıyor
     * olabilir ya da ECU geçici olarak absürt değer göndermiş olabilir.
     * Eşiği aşan ardışık OOR'larda ATRV'ye düş. */
    s_voltage_oor_count++;
    if (s_voltage_oor_count >= PID42_OOR_FALLBACK && !s_use_atrv) {
        s_use_atrv = true;
        app_log_warn(TAG, "PID 0x42 returning invalid %.2fV (x%u) — falling back to ATRV",
                     v, s_voltage_oor_count);
    } else if (!s_use_atrv) {
        app_log_info(TAG, "PID 0x42 OOR %.2fV (x%u/%d) — keeping 0x42",
                     v, s_voltage_oor_count, PID42_OOR_FALLBACK);
    }
}

static void atrv_response_cb(const char *resp, void *user_data)
{
    (void)user_data;
    s_voltage_pending = false;
    s_reprobe_inflight = false;
    float v = parse_atrv_voltage(resp);
    if (voltage_valid(v)) {
        set_voltage(v, "ATRV");
        return;
    }
    app_log_warn(TAG, "ATRV parse failed: resp='%s' parsed=%.2fV",
                 resp ? resp : "(null)", v);
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
#define DASH_PID_SPEED    0x0D

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
    if (e->pending) {
        return false;  /* önceki sorgu halen cevap bekliyor */
    }
    const vehicle_profile_t *profile = vehicle_profile_get();
    if (!elm327_send_cmd_prio(e->cmd, pid_response_cb, (void *)(uintptr_t)e->pid,
                              e->live ? profile->live_timeout_ms : profile->slow_timeout_ms,
                              high_priority)) {
        return false;
    }
    e->pending = true;
    /* last_poll cevap gelince pid_response_cb'de güncellenir */
    return true;
}

static bool poll_entry_by_pid(uint8_t pid, uint32_t now, bool high_priority)
{
    pid_entry_t *e = find_entry_by_pid(pid);
    if (!e || !should_poll_pid(e)) {
        return false;
    }

    /* Cevap bekleyen PID timeout olmuşsa pending'i temizle */
    if (e->pending) {
        const vehicle_profile_t *profile = vehicle_profile_get();
        uint32_t timeout = e->live ? profile->live_timeout_ms : profile->slow_timeout_ms;
        if (now - e->last_poll > timeout + 500) {
            e->pending = false;
            e->last_poll = now;  /* anchor interval to timeout recovery */
        } else {
            return false;  /* halen cevap bekleniyor */
        }
    }

    if (!poll_due(e, now)) {
        return false;
    }
    return send_pid_poll(e, now, high_priority);
}

static bool poll_voltage(uint32_t now)
{
    const vehicle_profile_t *profile = vehicle_profile_get();

    if (s_voltage_pending) {
        /* Önceki voltaj sorgusu halen cevap bekliyor — timeout kontrolü */
        if (now - s_voltage_last < (uint32_t)ATRV_TIMEOUT_MS + 500) {
            return false;
        }
        s_voltage_pending = false;  /* timeout, tekrar dene */
        /* Re-probe 0142 timeout oldu: adaptör 0142'ye hiç yanıt vermiyor.
         * ATRV moduna geri dön, aksi halde kalıcı olarak 0142'de takılıp
         * voltaj hiç gelmez. */
        if (s_reprobe_inflight) {
            s_use_atrv = true;
            s_reprobe_inflight = false;
            app_log_warn(TAG, "0142 re-probe timed out — reverting to ATRV");
        }
    }

    if (now - s_voltage_last < VOLTAGE_POLL_MS) {
        return false;
    }
    if (!elm327_can_queue(true)) {
        return false;
    }

    /* Periyodik 0x42 re-probe: ATRV modundayken bile ara sıra 0x42 dene.
     * Bazı araçlarda init sonrası 0x42 çalışmaya başlar. Callback s_use_atrv'i
     * tekrar false'a çekebilir. */
    if (s_use_atrv && (now - s_042_reprobe_last > PID42_REPROBE_MS)) {
        if (elm327_send_cmd_prio("0142", voltage_pid_cb, NULL,
                                 profile->slow_timeout_ms, true)) {
            s_042_reprobe_last = now;
            s_voltage_last = now;
            s_voltage_pending = true;
            s_reprobe_inflight = true;
            s_use_atrv = false;
            return true;
        }
    }

    /* Try PID 0x42 first; fall back to ATRV when ECU or profile requires it. */
    if (!profile->use_atrv_voltage && !s_use_atrv) {
        if (elm327_send_cmd_prio("0142", voltage_pid_cb, NULL, profile->slow_timeout_ms, true)) {
            s_voltage_last = now;
            s_voltage_pending = true;
            return true;
        }
        /* queue full; try ATRV as immediate fallback */
    }

    if (profile->use_atrv_voltage || s_use_atrv) {
        if (elm327_send_cmd_prio("ATRV", atrv_response_cb, NULL, ATRV_TIMEOUT_MS, true)) {
            s_voltage_last = now;
            s_voltage_pending = true;
            return true;
        }
    }
    return false;
}

/* Batch "010C0D" yanıtını çöz: RPM (0x0C, 2 bayt) + Speed (0x0D, 1 bayt).
 * Yanıttaki boşlukları temizleyip pozisyonel olarak ayrıştırır; her PID için
 * sentetik tek-PID dizesi kurup mevcut update_pid_value() yolunu (decode +
 * filtre) yeniden kullanır. Böylece decode/filtre mantığı tek yerde kalır. */
static void batch_rpm_speed_cb(const char *resp, void *user_data)
{
    (void)user_data;
    s_batch_pending = false;

    pid_entry_t *er = find_entry_by_pid(DASH_PID_RPM);
    pid_entry_t *es = find_entry_by_pid(DASH_PID_SPEED);
    if (er) er->pending = false;
    if (es) es->pending = false;

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool got_rpm = false, got_spd = false;

    if (resp && !response_is_error(resp)) {
        char hex[80];
        size_t hi = 0;
        for (const char *p = resp; *p && hi < sizeof(hex) - 1; p++) {
            if (isxdigit((unsigned char)*p)) {
                hex[hi++] = (char)toupper((unsigned char)*p);
            }
        }
        hex[hi] = '\0';

        char *m = strstr(hex, "410C");
        if (m && strlen(m) >= 8) {          /* "410C" + 4 hex RPM verisi */
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "410C%.4s", m + 4);
            update_pid_value(0x0C, tmp);
            got_rpm = true;
            if (er) er->last_poll = now;

            const char *after = m + 8;      /* RPM verisinden sonra "0D" + 2 hex */
            if (strncmp(after, "0D", 2) == 0 && strlen(after) >= 4) {
                char tmp2[16];
                snprintf(tmp2, sizeof(tmp2), "410D%.2s", after + 2);
                update_pid_value(0x0D, tmp2);
                got_spd = true;
                if (es) es->last_poll = now;
            }
        }
    }

    if (got_rpm && got_spd) {
        s_batch_state = 1;
        s_batch_fail = 0;
    } else if (++s_batch_fail >= BATCH_FAIL_MAX) {
        s_batch_state = -1;             /* kalıcı olarak ayrı polling'e dön */
        app_log_warn(TAG, "RPM+Speed batch unsupported — using separate polls");
    }
}

/* Batch komutunu (010C0D) kuyruğa atar. probe=true iken düşük frekanslı
 * yoklama (mod bilinmiyor), false iken açık moddaki normal cadence. */
static bool poll_batch_rpm_speed(uint32_t now, bool probe)
{
    const vehicle_profile_t *profile = vehicle_profile_get();

    if (s_batch_pending) {
        if (now - s_batch_last < (uint32_t)profile->live_timeout_ms + 500) {
            return false;
        }
        s_batch_pending = false;        /* timeout */
        if (!probe && ++s_batch_fail >= BATCH_FAIL_MAX) {
            s_batch_state = -1;
            app_log_warn(TAG, "RPM+Speed batch timing out — using separate polls");
        }
    }

    uint32_t interval = probe ? BATCH_PROBE_MS : BATCH_INTERVAL_MS;
    if (now - s_batch_last < interval) {
        return false;
    }
    if (!elm327_can_queue(true)) {
        return false;
    }
    if (!elm327_send_cmd_prio(DASH_BATCH_CMD, batch_rpm_speed_cb, NULL,
                              profile->live_timeout_ms, true)) {
        return false;
    }
    s_batch_last = now;
    s_batch_pending = true;
    /* Açık modda ayrı RPM/Speed poll'u çalışmıyor; yine de emniyet için
     * pending işaretle ki live-loop bunları atlasın. */
    if (!probe) {
        pid_entry_t *er = find_entry_by_pid(DASH_PID_RPM);
        pid_entry_t *es = find_entry_by_pid(DASH_PID_SPEED);
        if (er) er->pending = true;
        if (es) es->pending = true;
    }
    return true;
}

static bool run_dash_poll(uint32_t now)
{
    /* Dashboard mode: two-tier polling prioritizes RPM/Speed for lowest
     * latency.  Tier 1 (RPM+Speed) is always attempted.  Tier 2 (Voltage
     * +Coolant) is deferred when Tier 1 commands are in-flight, keeping
     * the ELM327 queue shallow (max 2) for faster BLE round-trips. */
    bool any_queued = false;

    /* Tier 1: RPM + Speed.
     * - Batch açık (state==1): tek "010C0D" komutu ile senkron + düşük gecikme.
     * - Bilinmiyor (state==0): ayrı poll (veri akmaya devam eder) + arka planda
     *   düşük frekanslı batch probe.
     * - Kapalı (state==-1): ayrı poll. */
    if (s_batch_state == 1) {
        if (poll_batch_rpm_speed(now, false)) {
            any_queued = true;
        }
    } else {
        bool rpm_queued = poll_entry_by_pid(DASH_PID_RPM, now, true);
        bool spd_queued = poll_entry_by_pid(DASH_PID_SPEED, now, true);
        any_queued = rpm_queued || spd_queued;
        if (s_batch_state == 0) {
            poll_batch_rpm_speed(now, true);  /* probe; cadence'i bozmamak için sayılmaz */
        }
    }

    /* Tier 2: Coolant + Voltage — kendi interval'leri (poll_due) ve pending
     * bayrakları sıklığı zaten sınırlar; RPM/Speed'e kapı KOYMUYORUZ, aksi
     * halde 60 ms'lik RPM/Speed turları Temp/Volt'u aç bırakıyordu.
     * Kuyruk RESP-REQ + pending sayesinde sığ kalır (en çok ~1'er in-flight). */
    if (poll_entry_by_pid(0x05, now, true)) {
        any_queued = true;
    }
    if (poll_voltage(now)) {
        any_queued = true;
    }

    /* Remaining live PIDs (none currently — RPM/Speed/Coolant are all
     * handled above).  Round-robin for future expansion. */
    for (size_t n = 0; n < s_live_count; n++) {
        size_t idx = (s_dash_slot + n) % s_live_count;
        uint8_t pid = s_live_entries[idx].pid;
        if (pid == DASH_PID_RPM || pid == DASH_PID_SPEED || pid == 0x05) {
            continue;
        }
        if (poll_entry_by_pid(pid, now, true)) {
            s_dash_slot = (idx + 1) % s_live_count;
            any_queued = true;
            break;
        }
    }

    return any_queued;
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
            s_voltage_oor_count = 0;
            s_042_reprobe_last = 0;
            s_voltage_pending = false;
            s_reprobe_inflight = false;
            s_batch_state = 0;   /* her bağlantıda batch yeteneğini yeniden probe et */
            s_batch_fail = 0;
            s_batch_pending = false;
            s_batch_last = 0;
            memset(s_pid_filters, 0, sizeof(s_pid_filters));  /* tüm PID filtreleri reset */
            init_poll_entries();  /* init tüm pending flag'leri false yapar */
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

        vTaskDelay(pdMS_TO_TICKS(queued ? 1 : 2));
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
