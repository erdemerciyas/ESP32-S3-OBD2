#include "demo_feed.h"
#include "vehicle_data.h"
#include "lvgl.h"
#include <math.h>
#include <string.h>

static lv_timer_t *s_demo_timer;
static lv_timer_t *s_connect_timer;
static int s_connect_step;
static float s_phase;

static void apply_live_values(void)
{
    vehicle_data_t *vd = vehicle_data_get();
    float rpm = 850.0f + 2200.0f * (0.5f + 0.5f * sinf(s_phase));
    float speed = 35.0f + 65.0f * (0.5f + 0.5f * sinf(s_phase * 0.7f));

    vehicle_data_set_float(&vd->rpm, rpm);
    vehicle_data_set_float(&vd->speed, speed);
    vehicle_data_set_float(&vd->coolant, 88.0f + 4.0f * sinf(s_phase * 0.3f));
    vehicle_data_set_float(&vd->voltage, 13.8f + 0.2f * sinf(s_phase * 0.2f));
    vehicle_data_set_float(&vd->throttle, 18.0f + 40.0f * (0.5f + 0.5f * sinf(s_phase * 1.1f)));
    vehicle_data_set_float(&vd->map, 32.0f + 8.0f * sinf(s_phase * 0.9f));
    vehicle_data_set_float(&vd->iat, 24.0f + 3.0f * sinf(s_phase * 0.4f));
    vehicle_data_set_float(&vd->timing, 12.0f + 4.0f * sinf(s_phase * 0.6f));
    vehicle_data_set_float(&vd->fuel_trim_st, -1.5f + 2.0f * sinf(s_phase * 0.5f));
    vehicle_data_set_float(&vd->fuel_trim_lt, 0.5f + 1.0f * sinf(s_phase * 0.35f));
    vehicle_data_set_float(&vd->load, 22.0f + 30.0f * (0.5f + 0.5f * sinf(s_phase * 0.85f)));
    vehicle_data_set_float(&vd->o2_voltage, 0.45f + 0.15f * sinf(s_phase * 1.4f));
    vehicle_data_set_float(&vd->o2_b1s2, 0.42f + 0.12f * sinf(s_phase * 1.2f));
}

static void demo_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_phase += 0.08f;
    if (vehicle_data_get()->state == OBD_STATE_READY) {
        apply_live_values();
    }
}

static void connect_timer_cb(lv_timer_t *timer)
{
    vehicle_data_t *vd = vehicle_data_get();

    switch (s_connect_step) {
    case 0:
        vehicle_data_set_state(OBD_STATE_SCANNING, "Taraniyor...");
        break;
    case 1:
        vehicle_data_set_state(OBD_STATE_CONNECTING, "Baglaniyor...");
        vehicle_data_set_adapter("ELM327 Sim", "AA:BB:CC:DD:EE:FF");
        break;
    case 2:
        vehicle_data_set_state(OBD_STATE_ELM_INIT, "ELM327 baslatiliyor...");
        break;
    case 3:
        vehicle_data_set_state(OBD_STATE_PID_DISCOVERY, "PID kesfi...");
        vehicle_data_set_pid_supported(0, 0x8833787E);
        break;
    case 4:
        vehicle_data_set_state(OBD_STATE_READY, "Bagli (simulator)");
        apply_live_values();
        lv_timer_del(s_connect_timer);
        s_connect_timer = NULL;
        return;
    default:
        break;
    }

    s_connect_step++;
}

void demo_feed_init(void)
{
    s_phase = 0.0f;
    s_connect_step = 0;
    s_demo_timer = lv_timer_create(demo_timer_cb, 100, NULL);
}

void demo_feed_start_connect(void)
{
    if (s_connect_timer) {
        lv_timer_del(s_connect_timer);
    }

    vehicle_data_t *vd = vehicle_data_get();
    vd->dtc_count = 0;
    vd->dtc_pending_count = 0;
    memset(vd->dtcs, 0, sizeof(vd->dtcs));

    s_connect_step = 0;
    s_connect_timer = lv_timer_create(connect_timer_cb, 600, NULL);
    connect_timer_cb(s_connect_timer);
}

void demo_feed_load_dtcs(void)
{
    vehicle_data_t *vd = vehicle_data_get();

    vd->dtc_count = 2;
    vd->dtc_pending_count = 1;
    strncpy(vd->dtcs[0], "P0420", sizeof(vd->dtcs[0]) - 1);
    strncpy(vd->dtcs[1], "P0171", sizeof(vd->dtcs[1]) - 1);
}

void demo_feed_clear_dtcs(void)
{
    vehicle_data_t *vd = vehicle_data_get();

    vd->dtc_count = 0;
    vd->dtc_pending_count = 0;
    memset(vd->dtcs, 0, sizeof(vd->dtcs));
}
