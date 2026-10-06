#include "roll_feed.h"
#include "roll_cfg.h"
#include "nav_service.h"
#include "app_log.h"
#include "sdkconfig.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "roll";

#define IMU_REC        14
#define OBD_REC        9
#define IMU_RING       128           /* ~1.3 sn @100 Hz */
#define OBD_RING       32
#define IMU_PER_FRAME  10            /* 142 B: MTU 247'ye rahat sığar */
#define OBD_PER_FRAME  16            /* 146 B */
#define TEL_PERIOD_MS  50

#define GPS_STALE_US   2000000       /* 1 Hz telefonda bir fix kaçabilir */
#define OBD_STALE_US   1200000
#define DR_MAX_US      1500000       /* IMU ile en çok bu kadar taşı */
#define RATE_WIN_US    1000000

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_active;
static volatile bool s_phone;

/* telemetri halkaları */
static uint8_t  s_imu_ring[IMU_RING][IMU_REC];
static uint16_t s_imu_head, s_imu_count;
static uint8_t  s_obd_ring[OBD_RING][OBD_REC];
static uint16_t s_obd_head, s_obd_count;
static uint32_t s_tel_drop;
static int64_t  s_tel_last_ok;

/* son değerler */
static roll_gnss_t s_gps;
static int64_t s_gps_rx;             /* varış (yaş/IMU taşıması bunun üzerinden) */
static float   s_gps_dr;             /* son fix'ten beri IMU hız değişimi, m/s */
static int     s_obd_kmh;
static int64_t s_obd_rx;
static float   s_obd_dr;
static float   s_a_long, s_pitch;
static int64_t s_imu_t;

/* oran ölçümü (olay / sn) */
typedef struct {
    uint32_t n;
    int64_t  t0;
    float    hz;
} rate_t;
static rate_t s_gps_rate, s_obd_rate;
static const roll_snapshot_t *s_demo;

static void rate_tick(rate_t *r, int64_t now)
{
    if (!r->t0) {
        r->t0 = now;
        return;
    }
    int64_t span = now - r->t0;
    if (span >= RATE_WIN_US) {
        r->hz = r->n * 1e6f / (float)span;
        r->n = 0;
        r->t0 = now;
    }
}

static inline void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static inline void put_i16(uint8_t *p, float v)
{
    int32_t i = lrintf(v);
    if (i > INT16_MAX) i = INT16_MAX;
    if (i < INT16_MIN) i = INT16_MIN;
    p[0] = (uint8_t)i;
    p[1] = (uint8_t)(i >> 8);
}

/* --- girişler ---------------------------------------------------------- */

void roll_feed_gnss(const roll_gnss_t *fix)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_mux);
    s_gps = *fix;
    s_gps_rx = now;
    s_gps_dr = 0;
    s_gps_rate.n++;
    taskEXIT_CRITICAL(&s_mux);
}

void roll_feed_obd_speed(int64_t t_tx_us, int64_t t_rx_us, int kmh)
{
    if (!s_active) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    s_obd_kmh = kmh;
    s_obd_rx = t_rx_us;
    s_obd_dr = 0;
    s_obd_rate.n++;
    uint8_t *r = s_obd_ring[(s_obd_head + s_obd_count) % OBD_RING];
    put_u32(r, (uint32_t)t_tx_us);
    put_u32(r + 4, (uint32_t)t_rx_us);
    r[8] = (uint8_t)(kmh < 0 ? 0 : kmh > 255 ? 255 : kmh);
    if (s_obd_count < OBD_RING) {
        s_obd_count++;
    } else {
        s_obd_head = (s_obd_head + 1) % OBD_RING;   /* taşma: en eskiyi at */
        s_tel_drop++;
    }
    taskEXIT_CRITICAL(&s_mux);
}

void roll_feed_imu(int64_t t_us, const float a[3], float a_long, float gz, float pitch_deg)
{
    if (!s_active) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    if (s_imu_t) {
        float dt = (t_us - s_imu_t) * 1e-6f;
        if (dt > 0 && dt < 0.1f) {
            s_gps_dr += a_long * dt;
            s_obd_dr += a_long * dt;
        }
    }
    s_imu_t = t_us;
    s_a_long = a_long;
    s_pitch = pitch_deg;
    uint8_t *r = s_imu_ring[(s_imu_head + s_imu_count) % IMU_RING];
    put_u32(r, (uint32_t)t_us);
    put_i16(r + 4, a[0] * 1000.0f);
    put_i16(r + 6, a[1] * 1000.0f);
    put_i16(r + 8, a[2] * 1000.0f);
    put_i16(r + 10, gz * (18000.0f / (float)M_PI));
    put_i16(r + 12, pitch_deg * 100.0f);
    if (s_imu_count < IMU_RING) {
        s_imu_count++;
    } else {
        s_imu_head = (s_imu_head + 1) % IMU_RING;
        s_tel_drop++;
    }
    taskEXIT_CRITICAL(&s_mux);
}

/* --- telemetri görevi ---------------------------------------------------- */

/* Halkanın başından bir çerçeve gönderir; başarısızsa kayıtlar halkada
 * kalır, sonraki turda yeniden denenir. */
static bool flush_imu(void)
{
    uint8_t buf[2 + IMU_PER_FRAME * IMU_REC];
    taskENTER_CRITICAL(&s_mux);
    int n = s_imu_count < IMU_PER_FRAME ? s_imu_count : IMU_PER_FRAME;
    for (int i = 0; i < n; i++) {
        memcpy(buf + 2 + i * IMU_REC, s_imu_ring[(s_imu_head + i) % IMU_RING], IMU_REC);
    }
    taskEXIT_CRITICAL(&s_mux);
    if (n == 0) {
        return false;
    }
    buf[0] = ROLL_TEL_IMU;
    buf[1] = (uint8_t)n;
    if (!nav_service_send_tel(buf, 2 + n * IMU_REC)) {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    /* gönderim sırasında halka taştıysa birkaç kayıt kaybolur (s_tel_drop) */
    int drop = n < s_imu_count ? n : s_imu_count;
    s_imu_head = (s_imu_head + drop) % IMU_RING;
    s_imu_count -= drop;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

static bool flush_obd(void)
{
    uint8_t buf[2 + OBD_PER_FRAME * OBD_REC];
    taskENTER_CRITICAL(&s_mux);
    int n = s_obd_count < OBD_PER_FRAME ? s_obd_count : OBD_PER_FRAME;
    for (int i = 0; i < n; i++) {
        memcpy(buf + 2 + i * OBD_REC, s_obd_ring[(s_obd_head + i) % OBD_RING], OBD_REC);
    }
    taskEXIT_CRITICAL(&s_mux);
    if (n == 0) {
        return false;
    }
    buf[0] = ROLL_TEL_OBD;
    buf[1] = (uint8_t)n;
    if (!nav_service_send_tel(buf, 2 + n * OBD_REC)) {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    int drop = n < s_obd_count ? n : s_obd_count;
    s_obd_head = (s_obd_head + drop) % OBD_RING;
    s_obd_count -= drop;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

static void clear_rings(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_imu_head = s_imu_count = 0;
    s_obd_head = s_obd_count = 0;
    taskEXIT_CRITICAL(&s_mux);
}

static void tel_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    uint32_t last_drop_log = 0;
    int64_t last_drop_t = 0;
    while (1) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TEL_PERIOD_MS));
        int64_t now = esp_timer_get_time();
        taskENTER_CRITICAL(&s_mux);
        rate_tick(&s_gps_rate, now);
        rate_tick(&s_obd_rate, now);
        taskEXIT_CRITICAL(&s_mux);
        if (!s_active) {
            continue;
        }
        if (!s_phone) {
            clear_rings();   /* telefon yok: kayıt yok, eski veriyi biriktirme */
            continue;
        }
        bool ok = false;
        for (int i = 0; i < 4 && flush_imu(); i++) {
            ok = true;
        }
        if (flush_obd()) {
            ok = true;
        }
        if (ok) {
            s_tel_last_ok = now;
        }
        if (s_tel_drop - last_drop_log >= 100 && now - last_drop_t > 5000000) {
            app_log_warn(TAG, "Telemetry dropped %lu records", (unsigned long)s_tel_drop);
            last_drop_log = s_tel_drop;
            last_drop_t = now;
        }
    }
}

/* --- API ----------------------------------------------------------------- */

void roll_feed_init(void)
{
    if (xTaskCreate(tel_task, "roll_tel", 3072, NULL, 4, NULL) != pdPASS) {
        app_log_error(TAG, "Telemetry task create failed");
    }
}

void roll_feed_set_active(bool on)
{
    if (on == s_active) {
        return;
    }
    if (on) {
        clear_rings();
        taskENTER_CRITICAL(&s_mux);
        s_imu_t = 0;
        s_obd_rx = 0;
        taskEXIT_CRITICAL(&s_mux);
    }
    s_active = on;
    app_log_info(TAG, "ROLL feed %s", on ? "on" : "off");
}

bool roll_feed_active(void)
{
    return s_active;
}

void roll_feed_set_phone(bool up)
{
    s_phone = up;
    if (!up) {
        taskENTER_CRITICAL(&s_mux);
        s_gps_rx = 0;
        taskEXIT_CRITICAL(&s_mux);
    }
}

void roll_feed_demo(const roll_snapshot_t *demo)
{
#ifdef CONFIG_UI_SHOT_TOUR
    s_demo = demo;
#else
    (void)demo;
#endif
}

void roll_feed_snapshot(roll_snapshot_t *o)
{
    if (s_demo) {
        *o = *s_demo;
        return;
    }
    /* ayarlar yalnız değişince kopyalanır (UI bunu 60 Hz çağırır) */
    static roll_cfg_t cfg;
    static uint32_t cfg_rev = UINT32_MAX;
    if (cfg_rev != roll_cfg_rev()) {
        cfg_rev = roll_cfg_rev();
        roll_cfg_get(&cfg);
    }
    int64_t now = esp_timer_get_time();
    memset(o, 0, sizeof(*o));
    taskENTER_CRITICAL(&s_mux);
    roll_gnss_t g = s_gps;
    int64_t gps_rx = s_gps_rx, obd_rx = s_obd_rx;
    float gps_dr = s_gps_dr, obd_dr = s_obd_dr;
    int obd_kmh = s_obd_kmh;
    o->gps_hz = s_gps_rate.hz;
    o->obd_hz = s_obd_rate.hz;
    o->a_long = s_a_long;
    o->pitch_deg = s_pitch;
    taskEXIT_CRITICAL(&s_mux);

    o->phone = s_phone;
    o->tel = s_tel_last_ok && now - s_tel_last_ok < 1000000;

    int64_t gps_age = gps_rx ? now - gps_rx : INT64_MAX;
    o->gps_age_ms = gps_rx ? (uint32_t)(gps_age / 1000) : UINT32_MAX;
    /* kalite eşikleri kullanıcı ayarından (min hız doğruluğu, min uydu) */
    o->gps_ok = s_phone && gps_age < GPS_STALE_US && g.speed_ms >= 0 &&
                (g.speed_acc < 0 || g.speed_acc <= cfg.gps_acc_max) &&
                (g.sats < 0 || g.sats >= cfg.sats_min);
    o->gps_synced = gps_rx && g.synced;
    o->gps_kmh = g.speed_ms > 0 ? g.speed_ms * 3.6f : 0;
    o->gps_acc_kmh = g.speed_acc < 0 ? -1.0f : g.speed_acc * 3.6f;
    o->gps_sats = gps_rx ? g.sats : -1;
    o->gps_alt = g.alt_m;
    if (!gps_rx) {
        o->gps_hz = 0;
    }

    int64_t obd_age = obd_rx ? now - obd_rx : INT64_MAX;
    o->obd_age_ms = obd_rx ? (uint32_t)(obd_age / 1000) : UINT32_MAX;
    o->obd_ok = obd_age < OBD_STALE_US;
    o->obd_kmh = obd_kmh;
    if (!o->obd_ok) {
        o->obd_hz = 0;
    }

    float v = 0;
    if (o->gps_ok && cfg.src != ROLL_SRC_MODE_OBD) {
        o->src = ROLL_SRC_GPS;
        v = g.speed_ms + (gps_age < DR_MAX_US ? gps_dr : 0);
    } else if (o->obd_ok && cfg.src != ROLL_SRC_MODE_GPS) {
        o->src = ROLL_SRC_OBD;
        v = obd_kmh / 3.6f + (obd_age < DR_MAX_US ? obd_dr : 0);
    }
    o->speed_kmh = v > 0 ? v * 3.6f : 0;
}
