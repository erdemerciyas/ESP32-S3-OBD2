#include "ui.h"
#include "sdkconfig.h"

#ifdef CONFIG_UI_SHOT_TOUR

#include "app_mode.h"
#include "clock.h"
#include "nav_mock.h"
#include "nav_state.h"
#include "vehicle_data.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* README ekran görüntüsü turu — yalnız CONFIG_UI_SHOT_TOUR derlemesinde.
 * Görünümleri sırayla açar, lv_snapshot ile kareyi alır ve USB konsoluna
 * base64 satırları olarak yazar (SHOT:BEGIN ad w h n / SHOT:D i <b64> / SHOT:END).
 * PC tarafı: scripts/capture_screens.py → docs/screenshots/<ad>.png.
 * OBD ekranları için demo araç verisi enjekte edilir (NAV modunda OBD
 * radyosu kapalı, gerçek veriyle çakışmaz). */

#define TOUR_TICK_MS    100
#define SHOT_LINE_BYTES 57      /* → 76 base64 karakter */
#define SHOT_PASSES     2

typedef struct {
    void (*prep)(int arg);
    int arg;
    uint32_t wait_ms;
    bool (*until)(void);      /* NULL değilse wait_ms dolunca da bunu bekler */
    const char *shot;         /* NULL: kare alma */
} step_t;

static bool s_demo_obd;
static app_mode_t s_orig_mode;

/* --- demo verisi ----------------------------------------------------------- */

static void demo_obd_tick(void)
{
    if (!s_demo_obd) {
        return;
    }
    float t = lv_tick_get() / 1000.0f;
    uint32_t now = lv_tick_get();
    vehicle_data_t *vd = vehicle_data_get();
    vehicle_data_lock();
    vd->state = OBD_STATE_READY;
    snprintf(vd->adapter_name, sizeof(vd->adapter_name), "OBDII");
    snprintf(vd->link.elm_id, sizeof(vd->link.elm_id), "ELM327 v1.5");
    snprintf(vd->link.proto, sizeof(vd->link.proto), "A5");
    snprintf(vd->link.volt_src, sizeof(vd->link.volt_src), "ATRV");
    vd->link.req_rate = 11.8f;
    vd->link.rpm_hz = 4.1f;
    vd->link.volt_raw = 14.1f;
    vd->rpm = 2650 + 180 * sinf(t * 0.9f);
    vd->speed = 78 + 3 * sinf(t * 0.4f);
    vd->coolant = 89;
    vd->voltage = 14.1f;
    vd->throttle = 21;
    vd->load = 38;
    vd->map = 52;
    vd->maf = 9.4f;
    vd->iat = 24;
    vd->timing = 14;
    vd->fuel_trim_st = 1.6f;
    vd->fuel_trim_lt = -2.3f;
    vd->fuel_level = 63;
    vd->oil_temp = 94;
    vd->o2_voltage = 0.45f + 0.35f * sinf(t * 3.1f);
    vd->o2_b1s2 = 0.68f;
    vd->rpm_ts = vd->speed_ts = vd->coolant_ts = vd->voltage_ts = now;
    vd->dash_pair_ts = now;
    vehicle_data_unlock();
}

/* --- adımlar --------------------------------------------------------------- */

static void p_begin(int arg)
{
    (void)arg;
    s_orig_mode = app_mode_get();
    if (CONFIG_UI_SHOT_EPOCH > 0) {
        clock_set_from_phone(CONFIG_UI_SHOT_EPOCH, 180);
    }
}

static void p_nav_mode(int arg)
{
    (void)arg;
    app_mode_set(APP_MODE_NAV);   /* mock rota başlar, OBD radyosu kapanır */
    s_demo_obd = true;
}

static void p_view(int view)      { ui_show_view(view); }
static void p_tab(int tab)        { ui_show_view(UI_VIEW_OBD); ui_show_tab(tab); }
static void p_clock(int on)       { if (on) screen_clock_show(); else screen_clock_hide(); }
static void p_nav_page(int n)     { while (n-- > 0) screen_nav_next_page(); }

static void p_nav_idle(int arg)
{
    (void)arg;
    nav_mock_stop();
    nav_state_t *ns = nav_state_begin();
    ns->connected = true;
    ns->active = false;
    ns->speed_kmh = 54;
    snprintf(ns->cur_road, sizeof(ns->cur_road), "Atat\xC3\xBCrk Bulvar\xC4\xB1");
    nav_state_commit();
}

static void p_end(int arg)
{
    (void)arg;
    s_demo_obd = false;
    vehicle_data_set_state(OBD_STATE_DISCONNECTED, "");
    app_mode_set(s_orig_mode);
    printf("SHOT:DONE\n");
}

static bool u_turn_near(void)
{
    nav_state_t st;
    nav_state_snapshot(&st);
    return st.active && st.maneuver == NAV_MAN_RIGHT && st.maneuver_dist_m < 240;
}

static const step_t STEPS[] = {
    { p_begin,    0,                0,     NULL,        NULL },
    { NULL,       0,                1800,  NULL,        "splash" },
    { p_nav_mode, 0,                5500,  NULL,        NULL },
    { p_view,     UI_VIEW_HOME,     1500,  NULL,        "home" },
    { p_view,     UI_VIEW_NAV,      2500,  NULL,        "nav_guide" },
    { NULL,       0,                0,     u_turn_near, "nav_turn" },
    { p_tab,      UI_TAB_DASH,      2500,  NULL,        "obd_dash" },
    { p_tab,      UI_TAB_GRID,      1500,  NULL,        "obd_grid" },
    { p_tab,      UI_TAB_DTC,       1500,  NULL,        "obd_dtc" },
    { p_tab,      UI_TAB_GYRO,      2000,  NULL,        "obd_gyro" },
    { p_tab,      UI_TAB_SETTINGS,  1500,  NULL,        "obd_settings" },
    { p_clock,    1,                2500,  NULL,        "clock" },
    { p_clock,    0,                300,   NULL,        NULL },
    { p_view,     UI_VIEW_NAV,      300,   NULL,        NULL },
    { p_nav_idle, 0,                2000,  NULL,        "nav_idle" },
    { p_end,      0,                0,     NULL,        NULL },
};
#define STEP_COUNT (sizeof(STEPS) / sizeof(STEPS[0]))

/* --- kare dökümü ----------------------------------------------------------- */

static void b64_line(uint32_t idx, const uint8_t *src, size_t n)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char out[24 + 80];
    size_t o = (size_t)snprintf(out, 24, "SHOT:D %u ", (unsigned)idx);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < n) v |= (uint32_t)src[i + 1] << 8;
        if (i + 2 < n) v |= src[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o++] = '\n';
    fwrite(out, 1, o, stdout);
}

static void shoot(const char *name)
{
    lv_obj_t *obj = screen_clock_visible() ? lv_layer_top() : lv_scr_act();
    lv_obj_update_layout(obj);
    uint32_t sz = lv_snapshot_buf_size_needed(obj, LV_IMG_CF_TRUE_COLOR);
    uint8_t *buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    lv_img_dsc_t dsc;
    if (!buf || lv_snapshot_take_to_buf(obj, LV_IMG_CF_TRUE_COLOR, &dsc, buf, sz) != LV_RES_OK) {
        printf("SHOT:FAIL %s\n", name);
        heap_caps_free(buf);
        return;
    }
    /* USB konsolu hızlı akışta satır düşürebilir: satırlar numaralı, kare iki
     * geçişte gönderilir ve araya kısa beklemeler girer; PC eksikleri ikinci
     * geçişten tamamlar. */
    uint32_t lines = (dsc.data_size + SHOT_LINE_BYTES - 1) / SHOT_LINE_BYTES;
    printf("SHOT:BEGIN %s %d %d %u\n", name, (int)dsc.header.w, (int)dsc.header.h, (unsigned)lines);
    for (int pass = 0; pass < SHOT_PASSES; pass++) {
        for (uint32_t l = 0; l < lines; l++) {
            uint32_t i = l * SHOT_LINE_BYTES;
            b64_line(l, buf + i, dsc.data_size - i < SHOT_LINE_BYTES ? dsc.data_size - i : SHOT_LINE_BYTES);
            if (l % 4 == 3) {
                fflush(stdout);
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
    }
    printf("SHOT:END %s\n", name);
    fflush(stdout);
    heap_caps_free(buf);
}

/* --- zamanlayıcı ----------------------------------------------------------- */

static void tour_cb(lv_timer_t *timer)
{
    static size_t s_i;
    static bool s_prepped;
    static uint32_t s_t0;

    demo_obd_tick();
    lv_disp_trig_activity(NULL);   /* tur sırasında koruyucu kendiliğinden gelmesin */
    if (s_i >= STEP_COUNT) {
        lv_timer_del(timer);
        return;
    }
    const step_t *st = &STEPS[s_i];
    if (!s_prepped) {
        if (st->prep) {
            st->prep(st->arg);
        }
        s_prepped = true;
        s_t0 = lv_tick_get();
    }
    if (lv_tick_elaps(s_t0) < st->wait_ms || (st->until && !st->until())) {
        return;
    }
    if (st->shot) {
        lv_refr_now(NULL);
        shoot(st->shot);
    }
    s_i++;
    s_prepped = false;
}

void ui_shots_start(void)
{
    lv_timer_create(tour_cb, TOUR_TICK_MS, NULL);
}

#else

void ui_shots_start(void)
{
}

#endif
