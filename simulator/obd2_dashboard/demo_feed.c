#include "demo_feed.h"
#include "vehicle_data.h"
#include "lvgl.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static lv_timer_t *s_demo_timer;
static lv_timer_t *s_connect_timer;
static int s_connect_step;
static float s_phase;

static void apply_live_values(void)
{
    vehicle_data_t *vd = vehicle_data_get();
    /* Synchronized RPM and speed ramp to verify gauge/needle sync */
    float base = 0.5f + 0.5f * sinf(s_phase);
    float rpm = 800.0f + 5000.0f * base;
    float speed = 10.0f + 180.0f * base;

    /* Occasionally push RPM into redline to test shift-light blink */
    if ((int)(s_phase / M_PI) % 5 == 0) {
        rpm += 1500.0f * (0.5f + 0.5f * sinf(s_phase * 3.0f));
    }

    vehicle_data_set_float(&vd->rpm, rpm);
    vehicle_data_set_float(&vd->speed, speed);
    vehicle_data_set_float(&vd->coolant, 88.0f + 6.0f * sinf(s_phase * 0.3f));
    vehicle_data_set_float(&vd->voltage, 13.8f + 0.3f * sinf(s_phase * 0.2f));
    vehicle_data_set_float(&vd->throttle, 18.0f + 60.0f * base);
    vehicle_data_set_float(&vd->map, 32.0f + 20.0f * base);
    vehicle_data_set_float(&vd->iat, 24.0f + 5.0f * sinf(s_phase * 0.4f));
    vehicle_data_set_float(&vd->timing, 12.0f + 6.0f * sinf(s_phase * 0.6f));
    vehicle_data_set_float(&vd->fuel_trim_st, -1.5f + 3.0f * sinf(s_phase * 0.5f));
    vehicle_data_set_float(&vd->fuel_trim_lt, 0.5f + 2.0f * sinf(s_phase * 0.35f));
    vehicle_data_set_float(&vd->load, 22.0f + 50.0f * base);
    vehicle_data_set_float(&vd->o2_voltage, 0.45f + 0.2f * sinf(s_phase * 1.4f));
    vehicle_data_set_float(&vd->o2_b1s2, 0.42f + 0.18f * sinf(s_phase * 1.2f));
}

static void demo_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_phase += 0.06f;
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

    s_connect_step = 0;
    s_connect_timer = lv_timer_create(connect_timer_cb, 600, NULL);
    connect_timer_cb(s_connect_timer);
}
