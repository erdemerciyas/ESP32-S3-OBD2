#include "vehicle_profile.h"
#include "vehicle_data.h"

#include <string.h>
#include <stdio.h>

#ifdef CONFIG_IDF_TARGET
#include "app_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#define PROFILE_LOG_INFO(tag, fmt, ...) app_log_info(tag, fmt, ##__VA_ARGS__)
#define PROFILE_LOG_WARN(tag, fmt, ...) app_log_warn(tag, fmt, ##__VA_ARGS__)
#else
#define PROFILE_LOG_INFO(tag, fmt, ...) do { } while (0)
#define PROFILE_LOG_WARN(tag, fmt, ...) do { } while (0)
#endif

static const char *TAG = "vehicle_profile";

#ifdef CONFIG_IDF_TARGET
static const char *NVS_NS = "obd_profiles";
static const char *NVS_KEY_ACTIVE = "active_id";
static const char *PROFILE_KEY_PREFIX = "p_";
#endif

/*
 * Universal OBD-II profile — any ELM327-compatible vehicle.
 *
 * Protocol: ATSP0 (auto-detect ISO/CAN/J1850).
 * PIDs: discovered at runtime via Mode 01 support bitmask (0100..01C0).
 * Voltage: PID 0x42 when available, ATRV fallback at runtime.
 */

static const vehicle_pid_poll_t s_univ_live[] = {
    { 0x0C,  35, true, true  },  /* RPM        — ultra-responsive gauge */
    { 0x0D,  50, true, true  },  /* Speed      — frequent updates */
    { 0x05, 150, true, true  },  /* Coolant    — lively sub-cell */
};

static const vehicle_pid_poll_t s_univ_fast[] = {
    { 0x11, 120, true,  false }, /* TPS */
    { 0x0B, 150, true,  false }, /* MAP */
    { 0x04, 250, true,  false }, /* Engine load */
    { 0x0F, 400, true,  false }, /* IAT */
    { 0x0E, 400, true,  false }, /* Timing advance */
    { 0x10, 600, false, false }, /* MAF */
};

static const vehicle_pid_poll_t s_univ_slow[] = {
    { 0x06, 2000, false, false }, /* STFT */
    { 0x07, 2000, false, false }, /* LTFT */
    { 0x14, 2500, true,  false }, /* O2 B1S1 */
    { 0x15, 2500, false, false }, /* O2 B1S2 */
    { 0x2F, 3000, true,  false }, /* Fuel Level */
};

static const vehicle_profile_t s_default_profile = {
    .profile_id         = "universal",
    .display_name       = "Universal OBD-II",
    .engine             = "Auto-detect",
    .protocol           = "Auto-detect (ATSP0)",
    .init_protocol_cmd  = "ATSP0",
    .init_timeout_cmd   = "ATST32",
    .live_timeout_ms    = 250,
    .slow_timeout_ms    = 500,
    .disc_timeout_ms    = 2000,
    .rpm_max            = 8000,
    .rpm_redline        = 6500,
    .use_atrv_voltage   = false,
    .atrv_voltage_scale = 1.0f,
    .known_pid_masks    = { 0, 0, 0, 0 },
    .live_pids          = s_univ_live,
    .live_count         = sizeof(s_univ_live) / sizeof(s_univ_live[0]),
    .fast_pids          = s_univ_fast,
    .fast_count         = sizeof(s_univ_fast) / sizeof(s_univ_fast[0]),
    .slow_pids          = s_univ_slow,
    .slow_count         = sizeof(s_univ_slow) / sizeof(s_univ_slow[0]),
};

static vehicle_profile_t s_profiles[VEHICLE_PROFILE_MAX_COUNT];
static int s_profile_count;
static int s_active_index;
static bool s_initialized;

static void copy_string(char *dst, size_t dst_len, const char *src)
{
    if (!dst || !src || dst_len == 0) {
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void reset_profiles(void)
{
    memset(s_profiles, 0, sizeof(s_profiles));
    s_profile_count = 0;
    s_active_index = 0;
}

static bool add_profile(const vehicle_profile_t *profile)
{
    if (s_profile_count >= VEHICLE_PROFILE_MAX_COUNT || !profile) {
        return false;
    }
    memcpy(&s_profiles[s_profile_count], profile, sizeof(vehicle_profile_t));
    s_profile_count++;
    return true;
}

static bool profile_id_valid(const char *id)
{
    if (!id || id[0] == '\0') {
        return false;
    }
    size_t len = strlen(id);
    if (len >= VEHICLE_PROFILE_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static int find_profile_index(const char *profile_id)
{
    if (!profile_id) {
        return -1;
    }
    for (int i = 0; i < s_profile_count; i++) {
        if (strcmp(s_profiles[i].profile_id, profile_id) == 0) {
            return i;
        }
    }
    return -1;
}

static void attach_pid_tables(vehicle_profile_t *profile)
{
    profile->live_pids = s_univ_live;
    profile->live_count = sizeof(s_univ_live) / sizeof(s_univ_live[0]);
    profile->fast_pids = s_univ_fast;
    profile->fast_count = sizeof(s_univ_fast) / sizeof(s_univ_fast[0]);
    profile->slow_pids = s_univ_slow;
    profile->slow_count = sizeof(s_univ_slow) / sizeof(s_univ_slow[0]);
}

#ifdef CONFIG_IDF_TARGET

static void profile_from_storage(vehicle_profile_t *dst, const vehicle_profile_storage_t *src)
{
    memset(dst, 0, sizeof(*dst));
    copy_string(dst->profile_id, sizeof(dst->profile_id), src->profile_id);
    copy_string(dst->display_name, sizeof(dst->display_name), src->display_name);
    copy_string(dst->engine, sizeof(dst->engine), src->engine);
    copy_string(dst->protocol, sizeof(dst->protocol), src->protocol);
    copy_string(dst->init_protocol_cmd, sizeof(dst->init_protocol_cmd), src->init_protocol_cmd);
    copy_string(dst->init_timeout_cmd, sizeof(dst->init_timeout_cmd), src->init_timeout_cmd);
    dst->live_timeout_ms = src->live_timeout_ms ? src->live_timeout_ms : s_default_profile.live_timeout_ms;
    dst->slow_timeout_ms = src->slow_timeout_ms ? src->slow_timeout_ms : s_default_profile.slow_timeout_ms;
    dst->disc_timeout_ms = src->disc_timeout_ms ? src->disc_timeout_ms : s_default_profile.disc_timeout_ms;
    dst->rpm_max = src->rpm_max ? src->rpm_max : s_default_profile.rpm_max;
    dst->rpm_redline = src->rpm_redline ? src->rpm_redline : s_default_profile.rpm_redline;
    dst->use_atrv_voltage = src->use_atrv_voltage;
    dst->atrv_voltage_scale = (src->atrv_voltage_scale > 0.0f) ? src->atrv_voltage_scale : 1.0f;
    memcpy(dst->known_pid_masks, src->known_pid_masks, sizeof(dst->known_pid_masks));
    attach_pid_tables(dst);
}

static void profile_to_storage(vehicle_profile_storage_t *dst, const vehicle_profile_t *src, bool is_default)
{
    memset(dst, 0, sizeof(*dst));
    copy_string(dst->profile_id, sizeof(dst->profile_id), src->profile_id);
    copy_string(dst->display_name, sizeof(dst->display_name), src->display_name);
    copy_string(dst->engine, sizeof(dst->engine), src->engine);
    copy_string(dst->protocol, sizeof(dst->protocol), src->protocol);
    copy_string(dst->init_protocol_cmd, sizeof(dst->init_protocol_cmd), src->init_protocol_cmd);
    copy_string(dst->init_timeout_cmd, sizeof(dst->init_timeout_cmd), src->init_timeout_cmd);
    dst->live_timeout_ms = src->live_timeout_ms;
    dst->slow_timeout_ms = src->slow_timeout_ms;
    dst->disc_timeout_ms = src->disc_timeout_ms;
    dst->rpm_max = src->rpm_max;
    dst->rpm_redline = src->rpm_redline;
    dst->use_atrv_voltage = src->use_atrv_voltage;
    dst->atrv_voltage_scale = src->atrv_voltage_scale;
    memcpy(dst->known_pid_masks, src->known_pid_masks, sizeof(dst->known_pid_masks));
    dst->is_default = is_default;
}

static bool load_profiles_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    reset_profiles();
    add_profile(&s_default_profile);

    /* Enumerate possible profile slots. */
    for (int i = 0; i < VEHICLE_PROFILE_MAX_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", PROFILE_KEY_PREFIX, i);

        vehicle_profile_storage_t storage;
        size_t len = sizeof(storage);
        if (nvs_get_blob(h, key, &storage, &len) != ESP_OK || len != sizeof(storage)) {
            continue;
        }
        if (!profile_id_valid(storage.profile_id)) {
            continue;
        }
        /* Skip duplicate built-in id. */
        if (strcmp(storage.profile_id, s_default_profile.profile_id) == 0) {
            continue;
        }

        vehicle_profile_t profile;
        profile_from_storage(&profile, &storage);
        add_profile(&profile);
    }

    /* Restore active profile id. */
    char active_id[VEHICLE_PROFILE_ID_LEN] = {0};
    size_t active_len = sizeof(active_id);
    if (nvs_get_str(h, NVS_KEY_ACTIVE, active_id, &active_len) == ESP_OK) {
        int idx = find_profile_index(active_id);
        if (idx >= 0) {
            s_active_index = idx;
        }
    }

    nvs_close(h);
    return true;
}

#endif /* CONFIG_IDF_TARGET */

bool vehicle_profile_init(void)
{
    if (s_initialized) {
        return true;
    }
    reset_profiles();
    add_profile(&s_default_profile);
#ifdef CONFIG_IDF_TARGET
    load_profiles_from_nvs();
#endif
    s_initialized = true;
    PROFILE_LOG_INFO(TAG, "Profiles loaded: %d, active: %s", s_profile_count, s_profiles[s_active_index].profile_id);
    return true;
}

bool vehicle_profile_load_all(void)
{
#ifdef CONFIG_IDF_TARGET
    return load_profiles_from_nvs();
#else
    return true;
#endif
}

const vehicle_profile_t *vehicle_profile_get(void)
{
    if (!s_initialized) {
        vehicle_profile_init();
    }
    if (s_profile_count == 0) {
        return &s_default_profile;
    }
    if (s_active_index < 0 || s_active_index >= s_profile_count) {
        s_active_index = 0;
    }
    return &s_profiles[s_active_index];
}

const vehicle_profile_t *vehicle_profile_get_default(void)
{
    return &s_default_profile;
}

const vehicle_profile_t *vehicle_profile_get_by_id(const char *profile_id)
{
    if (!s_initialized) {
        vehicle_profile_init();
    }
    int idx = find_profile_index(profile_id);
    if (idx < 0) {
        return NULL;
    }
    return &s_profiles[idx];
}

int vehicle_profile_get_count(void)
{
    if (!s_initialized) {
        vehicle_profile_init();
    }
    return s_profile_count;
}

const vehicle_profile_t *vehicle_profile_get_at(int index)
{
    if (!s_initialized) {
        vehicle_profile_init();
    }
    if (index < 0 || index >= s_profile_count) {
        return NULL;
    }
    return &s_profiles[index];
}

const char *vehicle_profile_get_active_id(void)
{
    return vehicle_profile_get()->profile_id;
}

bool vehicle_profile_set_active(const char *profile_id)
{
    if (!s_initialized) {
        vehicle_profile_init();
    }
    int idx = find_profile_index(profile_id);
    if (idx < 0) {
        PROFILE_LOG_WARN(TAG, "Profile not found: %s", profile_id);
        return false;
    }
    s_active_index = idx;

#ifdef CONFIG_IDF_TARGET
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_ACTIVE, s_profiles[idx].profile_id);
        nvs_commit(h);
        nvs_close(h);
    }
#endif

    PROFILE_LOG_INFO(TAG, "Active profile set to: %s", s_profiles[idx].profile_id);
    return true;
}

bool vehicle_profile_save(const vehicle_profile_t *profile, bool is_default)
{
    if (!s_initialized) {
        vehicle_profile_init();
    }
    if (!profile || !profile_id_valid(profile->profile_id)) {
        PROFILE_LOG_WARN(TAG, "Invalid profile id");
        return false;
    }
    if (strcmp(profile->profile_id, s_default_profile.profile_id) == 0 && !is_default) {
        PROFILE_LOG_WARN(TAG, "Cannot overwrite built-in default profile");
        return false;
    }

    /* Find existing slot or first free slot. */
    int existing_idx = find_profile_index(profile->profile_id);
    int slot = existing_idx;
    if (slot < 0) {
        if (s_profile_count >= VEHICLE_PROFILE_MAX_COUNT) {
            PROFILE_LOG_WARN(TAG, "Profile storage full");
            return false;
        }
        slot = s_profile_count;
    }

#ifdef CONFIG_IDF_TARGET
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }

    vehicle_profile_storage_t storage;
    profile_to_storage(&storage, profile, is_default);

    char key[32];
    snprintf(key, sizeof(key), "%s%d", PROFILE_KEY_PREFIX, slot);
    esp_err_t err = nvs_set_blob(h, key, &storage, sizeof(storage));
    if (err != ESP_OK) {
        nvs_close(h);
        return false;
    }
    nvs_commit(h);
    nvs_close(h);
#endif

    /* Update runtime array. */
    vehicle_profile_t new_profile;
    memcpy(&new_profile, profile, sizeof(vehicle_profile_t));
    attach_pid_tables(&new_profile);

    if (existing_idx < 0) {
        add_profile(&new_profile);
    } else {
        s_profiles[existing_idx] = new_profile;
    }

    PROFILE_LOG_INFO(TAG, "Profile saved: %s", profile->profile_id);
    return true;
}

bool vehicle_profile_delete(const char *profile_id)
{
    if (!s_initialized) {
        vehicle_profile_init();
    }
    if (!profile_id) {
        return false;
    }
    if (strcmp(profile_id, s_default_profile.profile_id) == 0) {
        PROFILE_LOG_WARN(TAG, "Cannot delete built-in default profile");
        return false;
    }

    int idx = find_profile_index(profile_id);
    if (idx < 0) {
        return false;
    }

#ifdef CONFIG_IDF_TARGET
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", PROFILE_KEY_PREFIX, idx);
        nvs_erase_key(h, key);
        /* Compact remaining profiles to keep slots contiguous. */
        for (int i = idx; i < s_profile_count - 1; i++) {
            vehicle_profile_storage_t storage;
            profile_to_storage(&storage, &s_profiles[i + 1], false);
            snprintf(key, sizeof(key), "%s%d", PROFILE_KEY_PREFIX, i);
            nvs_set_blob(h, key, &storage, sizeof(storage));
        }
        /* Erase last used slot. */
        if (s_profile_count > 1) {
            snprintf(key, sizeof(key), "%s%d", PROFILE_KEY_PREFIX, s_profile_count - 1);
            nvs_erase_key(h, key);
        }
        nvs_commit(h);
        nvs_close(h);
    }
#endif

    /* Shift runtime array. */
    for (int i = idx; i < s_profile_count - 1; i++) {
        s_profiles[i] = s_profiles[i + 1];
    }
    s_profile_count--;
    if (s_active_index >= s_profile_count) {
        s_active_index = 0;
        vehicle_profile_set_active(s_profiles[s_active_index].profile_id);
    }

    PROFILE_LOG_INFO(TAG, "Profile deleted: %s", profile_id);
    return true;
}

void vehicle_profile_apply_known_pids(void)
{
    const vehicle_profile_t *p = vehicle_profile_get();
    vehicle_data_t *vd = vehicle_data_get();

    vehicle_data_lock();
    for (int i = 0; i < 4; i++) {
        vd->supported_pids[i] |= p->known_pid_masks[i];
    }
    vehicle_data_unlock();
}
