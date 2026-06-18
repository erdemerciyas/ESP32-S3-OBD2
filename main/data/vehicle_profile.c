#include "vehicle_profile.h"
#include "vehicle_data.h"

/*
 * Universal OBD-II profile — any ELM327-compatible vehicle.
 *
 * Protocol: ATSP0 (auto-detect ISO/CAN/J1850).
 * PIDs: discovered at runtime via Mode 01 support bitmask (0100..01C0).
 * Voltage: PID 0x42 when available, ATRV fallback at runtime.
 */

static const vehicle_pid_poll_t s_univ_live[] = {
    { 0x0C, 50,  true, true  },  /* RPM */
    { 0x0D, 100, true, true  },  /* Speed */
    { 0x05, 500, true, true  },  /* Coolant */
};

static const vehicle_pid_poll_t s_univ_fast[] = {
    { 0x11, 200,  true,  false }, /* TPS */
    { 0x0B, 250,  true,  false }, /* MAP */
    { 0x04, 400,  true,  false }, /* Engine load */
    { 0x0F, 600,  true,  false }, /* IAT */
    { 0x0E, 600,  true,  false }, /* Timing advance */
    { 0x10, 800,  false, false }, /* MAF (when supported) */
};

static const vehicle_pid_poll_t s_univ_slow[] = {
    { 0x06, 3500, false, false }, /* STFT */
    { 0x07, 3500, false, false }, /* LTFT */
    { 0x14, 4000, true,  false }, /* O2 B1S1 */
    { 0x15, 4000, false, false }, /* O2 B1S2 */
};

static const vehicle_profile_t s_universal = {
    .display_name       = "Universal OBD-II",
    .engine             = "Auto-detect",
    .protocol           = "Auto-detect (ATSP0)",
    .init_protocol_cmd  = "ATSP0",
    .init_timeout_cmd   = "ATST32",
    .live_timeout_ms    = 350,
    .slow_timeout_ms    = 700,
    .disc_timeout_ms    = 2500,
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

const vehicle_profile_t *vehicle_profile_get(void)
{
    return &s_universal;
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
