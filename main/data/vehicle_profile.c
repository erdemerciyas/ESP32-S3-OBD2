#include "vehicle_profile.h"
#include "vehicle_data.h"

/*
 * Chevrolet Kalos 2005 1.4 16V benzin (Daewoo Kalos / GM Family 1)
 *
 * OBD-II protokol: ISO 14230-4 KWP FAST (CAN degil)
 * ECU tipi: GM/Daewoo D-ECU, MAP tabanli (MAF sensoru yok)
 *
 * PID 0100 destek maskesi (OBDKey Kalos 2004 referans): BE 3E B8 11
 * Desteklenen Mode 01 PID'ler:
 *   01-07  Monitor, yakit sistemi, yuk, sicaklik, yakit trim
 *   0B-0F  MAP, RPM, hiz, zamanlama, emme sicakligi
 *   11-12  Gaz kelebegi, ikincil hava
 *   15-16  O2 sensor B1S2, O2 sensor B1S3/locations
 *   1C     OBD standardi
 *   20     PID 21-40 destek listesi
 *
 * Desteklenmeyen: 0x0A yakit basinci, 0x10 MAF, 0x42 ECU voltaji
 * Voltaj: ELM327 ATRV komutu ile okunur
 */

static const vehicle_pid_poll_t s_kalos_live[] = {
    { 0x0C, 150, true, true  },
    { 0x0D, 250, true, true  },
    { 0x05, 500, true, true  },
};

static const vehicle_pid_poll_t s_kalos_fast[] = {
    { 0x11, 200,  true, false },
    { 0x0B, 250,  true, false },
    { 0x04, 400,  true, false },
    { 0x0F, 600,  true, false },
    { 0x0E, 600,  true, false },
    { 0x03, 1200, true, false },
};

static const vehicle_pid_poll_t s_kalos_slow[] = {
    { 0x06, 3500, false, false },
    { 0x07, 3500, false, false },
    { 0x14, 4000, true,  false },
    { 0x15, 4000, false, false },
    { 0x12, 5000, false, false },
};

static const vehicle_profile_t s_kalos_2005 = {
    .display_name       = "Chevrolet Kalos 2005 1.4",
    .engine             = "B14XLS 1.4 16V Benzin",
    .protocol           = "ISO 14230-4 KWP FAST",
    .init_protocol_cmd  = "ATSP5",
    .init_timeout_cmd   = "ATST32",
    .live_timeout_ms    = 350,
    .slow_timeout_ms    = 700,
    .disc_timeout_ms    = 2000,
    .rpm_max            = 7000,
    .rpm_redline        = 6500,
    .use_atrv_voltage   = true,
    .atrv_voltage_scale = 0.7524f,
    .known_pid_masks    = {
        /* PID 0100 yaniti BE3EB811 -> bit mask (pid-1) */
        0x8833787E,
        0x00000000,
        0x00000000,
        0x00000000,
    },
    .live_pids          = s_kalos_live,
    .live_count         = sizeof(s_kalos_live) / sizeof(s_kalos_live[0]),
    .fast_pids          = s_kalos_fast,
    .fast_count         = sizeof(s_kalos_fast) / sizeof(s_kalos_fast[0]),
    .slow_pids          = s_kalos_slow,
    .slow_count         = sizeof(s_kalos_slow) / sizeof(s_kalos_slow[0]),
};

const vehicle_profile_t *vehicle_profile_get(void)
{
    return &s_kalos_2005;
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
