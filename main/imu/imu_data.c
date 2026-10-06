#include "imu_data.h"
#include "qmi8658.h"
#include "vehicle_data.h"
#include "roll_feed.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "imu";

#define G0              9.80665f
#define DT_NOM          0.01f        /* 100 Hz */
#define IMU_STALE_MS    500

/* Füzyon */
#define K_STILL         0.05f        /* dururken ivmeölçere hızlı yakınsa (τ≈0.2 s) */
#define K_MOVE          0.008f       /* hareketteyken güvenilirse (τ≈1.2 s) */
#define TRUST_DEV       0.12f        /* ||a|−g|/g bu kadar sapınca güven 0 */
#define TRUST_RATE      0.6f         /* rad/s: hızlı dönüşte ivmeölçere güvenme */
#define G_LPF           0.15f        /* ekrandaki G için alçak geçiren */

/* Durma algısı + sapma öğrenimi */
#define STILL_RATE      0.035f       /* rad/s */
#define STILL_DEV       0.03f        /* g */
#define STILL_MS        1500
#define BIAS_ALPHA      0.002f
#define BIAS_MAX        0.08f        /* rad/s: bundan büyük "sapma" gerçek dönüştür */

/* Kalibrasyon */
#define CAL_SAMPLES     300          /* 3 sn */
#define CAL_MAX_STD     0.025f       /* |a| std, g */
#define CAL_MAX_RATE    0.06f        /* rad/s */

/* OBD hız telafisi + ileri yön öğrenimi */
#define SPD_STALE_MS    1200
#define ALONG_LPF       0.3f
#define ALONG_DEADBAND  0.5f         /* m/s²: K-line hız gürültüsü */
#define FWD_MIN_ACC     1.3f         /* m/s² */
#define FWD_MAX_YAW     0.06f        /* rad/s */
#define FWD_NEED_S      4.0f
#define FWD_MIN_DEG     8.0f

typedef struct { float x, y, z; } v3;

static inline v3 v_add(v3 a, v3 b) { return (v3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline v3 v_sub(v3 a, v3 b) { return (v3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline v3 v_mul(v3 a, float k) { return (v3){ a.x * k, a.y * k, a.z * k }; }
static inline float v_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3 v_cross(v3 a, v3 b)
{
    return (v3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static inline float v_len(v3 a) { return sqrtf(v_dot(a, a)); }
static inline v3 v_norm(v3 a)
{
    float l = v_len(a);
    return l > 1e-6f ? v_mul(a, 1.0f / l) : a;
}

/* Montaj: araç eksenleri sensör koordinatlarında (R satırları) */
typedef struct {
    v3 fwd, left, up;
} mount_t;

static inline v3 to_vehicle(const mount_t *m, v3 s)
{
    return (v3){ v_dot(m->fwd, s), v_dot(m->left, s), v_dot(m->up, s) };
}
static inline v3 to_sensor(const mount_t *m, v3 v)
{
    return v_add(v_add(v_mul(m->fwd, v.x), v_mul(m->left, v.y)), v_mul(m->up, v.z));
}

/* --- kalıcı ayarlar (NVS "imu") -------------------------------------------- */

typedef struct {
    v3   up;          /* düzken ivmeölçerin gösterdiği yön (sensör) */
    v3   fwd_hint;    /* ileri yön ipucu (sensör) */
    v3   bias;        /* jiroskop sapması, rad/s */
    float g_ref;      /* bu sensörün okuduğu 1 g (m/s²) — ölçek hatası telafisi */
    bool calibrated;
    bool fwd_learned;
} imu_cfg_t;

static imu_cfg_t s_cfg;

/* Varsayılan: dikey montaj, ekran sürücüye bakar — sensör −X yukarı, +Z ileri
 * (eski ekranın varsayımı). Düz montajda +Z yukarı olur: ileri ipucu −X. */
static void cfg_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.up = (v3){ -1, 0, 0 };
    s_cfg.fwd_hint = (v3){ 0, 0, 1 };
    s_cfg.g_ref = G0;
}

static void cfg_load(void)
{
    cfg_defaults();
    nvs_handle_t h;
    if (nvs_open("imu", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    imu_cfg_t c;
    size_t n = sizeof(c);
    if (nvs_get_blob(h, "cfg2", &c, &n) == ESP_OK && n == sizeof(c)) {
        float ul = v_len(c.up), fl = v_len(c.fwd_hint);
        if (ul > 0.9f && ul < 1.1f && fl > 0.5f && v_len(c.bias) < BIAS_MAX &&
            c.g_ref > 0.85f * G0 && c.g_ref < 1.15f * G0) {
            s_cfg = c;
        } else {
            ESP_LOGW(TAG, "Stored calibration invalid (|up| %.2f) — using defaults", ul);
        }
    }
    nvs_close(h);
}

static void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("imu", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg2", &s_cfg, sizeof(s_cfg));
        nvs_commit(h);
        nvs_close(h);
    }
}

/* yukarı + ileri ipucundan ortonormal araç eksenleri */
static mount_t build_mount(v3 up, v3 hint)
{
    mount_t m;
    m.up = v_norm(up);
    v3 f = v_sub(hint, v_mul(m.up, v_dot(hint, m.up)));
    if (v_len(f) < 0.3f) {
        /* ipucu dikeye yakın (cihaz düz yatıyor): ekranın üst kenarı (−X) ileri */
        v3 alt = { -1, 0, 0 };
        f = v_sub(alt, v_mul(m.up, v_dot(alt, m.up)));
    }
    m.fwd = v_norm(f);
    m.left = v_cross(m.up, m.fwd);
    return m;
}

/* --- durum ------------------------------------------------------------------ */

typedef struct {
    float pitch, roll, g_long, g_lat;
    float peak_pitch, peak_roll, peak_g;
    float temperature;
    uint32_t ts;
    bool fresh, stationary, speed_comp;
    imu_calib_state_t cal_state;
    float cal_progress;
} imu_out_t;

static imu_out_t s_out;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static volatile bool s_running;

/* UI → görev istekleri */
static volatile bool s_req_cal, s_req_zero, s_req_peaks, s_req_clear;

extern uint32_t lv_tick_get(void);

static void imu_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    mount_t m = build_mount(s_cfg.up, s_cfg.fwd_hint);
    v3 gs = s_cfg.up;                 /* tahmini "yukarı" (sensör), birim */
    bool seeded = false;
    uint32_t last_ms = 0, still_since = 0;
    float glong = 0, glat = 0;
    float peak_p = 0, peak_r = 0, peak_g = 0;

    /* kalibrasyon toplayıcıları */
    int cal_n = -1;
    v3 cal_a = {0}, cal_w = {0};
    float cal_s1 = 0, cal_s2 = 0, cal_wmax = 0;

    /* OBD hızından boyuna ivme */
    float v_prev = 0, a_long = 0;
    uint32_t v_ts_prev = 0;

    /* ileri yön öğrenimi */
    v3 fwd_acc = {0};
    float fwd_time = 0;
    bool logged_sample = false;
    bool logged_state = false;
    bool auto_level = false;
    float an_lpf = 0;
    v3 w_lpf = {0};
    v3 a_lpf = {0};

    while (s_running) {
        qmi8658_data_t raw;
        if (!qmi8658_read_sensors(&raw)) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
            continue;
        }
        int64_t t_us = esp_timer_get_time();   /* ROLL: örnek anı */
        uint32_t now = lv_tick_get();
        float dt = last_ms ? (now - last_ms) / 1000.0f : DT_NOM;
        if (dt <= 0) dt = 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now;

        v3 a = { raw.accel_x, raw.accel_y, raw.accel_z };
        v3 w_raw = { raw.gyro_x, raw.gyro_y, raw.gyro_z };
        v3 w = v_sub(w_raw, s_cfg.bias);
        float an = v_len(a);
        float gr = s_cfg.g_ref;
        an_lpf += (an_lpf > 0 ? 0.05f : 1.0f) * (an - an_lpf);
        float jitter = fabsf(an - an_lpf) / gr;   /* ivme sabit mi (ölçekten bağımsız) */
        w_lpf = v_add(w_lpf, v_mul(v_sub(w_raw, w_lpf), 0.05f));
        a_lpf = v_len(a_lpf) > 0 ? v_add(a_lpf, v_mul(v_sub(a, a_lpf), 0.05f)) : a;
        float w_jitter = v_len(v_sub(w_raw, w_lpf));   /* jiro sabit mi (sapmadan bağımsız) */
        float rate = v_len(w);
        if (!logged_sample && now > 3000) {
            logged_sample = true;
            ESP_LOGI(TAG, "Sample a=(%.2f %.2f %.2f) |a|=%.2f m/s2  w=(%.3f %.3f %.3f) rad/s",
                     a.x, a.y, a.z, an, w_raw.x, w_raw.y, w_raw.z);
        }

        /* --- UI istekleri --- */
        if (s_req_clear) {
            s_req_clear = false;
            cfg_defaults();
            cfg_save();
            m = build_mount(s_cfg.up, s_cfg.fwd_hint);
            ESP_LOGI(TAG, "Calibration cleared");
        }
        if (s_req_cal) {
            s_req_cal = false;
            cal_n = 0;
            cal_a = cal_w = (v3){0};
            cal_s1 = cal_s2 = cal_wmax = 0;
        }
        if (s_req_zero) {
            s_req_zero = false;
            if (still_since && now - still_since > 500) {
                s_cfg.up = v_norm(a_lpf);   /* dururken: süzülmüş ivme en doğru "yukarı" */
                gs = s_cfg.up;
            } else if (fabsf(v_len(gs) - 1.0f) < 0.1f) {
                s_cfg.up = gs;
            }
            s_cfg.calibrated = true;
            cfg_save();
            m = build_mount(s_cfg.up, s_cfg.fwd_hint);
            peak_p = peak_r = peak_g = 0;
            ESP_LOGI(TAG, "Level zeroed");
        }
        if (s_req_peaks) {
            s_req_peaks = false;
            peak_p = peak_r = peak_g = 0;
        }

        /* --- 3 sn kalibrasyon örneklemesi --- */
        imu_calib_state_t cal_state_now = IMU_CAL_IDLE;
        if (cal_n >= 0) {
            cal_a = v_add(cal_a, a);
            cal_w = v_add(cal_w, w_raw);
            cal_s1 += an / gr;
            cal_s2 += (an / gr) * (an / gr);
            cal_n++;
            if (cal_n >= CAL_SAMPLES) {
                v3 wm = v_mul(cal_w, 1.0f / cal_n);
                float mean = cal_s1 / cal_n;
                float std = sqrtf(fmaxf(0, cal_s2 / cal_n - mean * mean));
                bool moved = std > CAL_MAX_STD || cal_wmax > CAL_MAX_RATE || v_len(wm) > BIAS_MAX ||
                             fabsf(mean - 1.0f) > 0.15f;   /* 1 g görünmüyorsa sensör sorunu */
                if (moved) {
                    s_out.cal_state = IMU_CAL_MOVED;
                    ESP_LOGW(TAG, "Calibration rejected: moving (std %.3f g, wmax %.3f)", std, cal_wmax);
                } else {
                    s_cfg.bias = wm;
                    s_cfg.g_ref = v_len(v_mul(cal_a, 1.0f / CAL_SAMPLES));
                    s_cfg.up = v_norm(cal_a);
                    s_cfg.calibrated = true;
                    cfg_save();
                    m = build_mount(s_cfg.up, s_cfg.fwd_hint);
                    gs = s_cfg.up;
                    peak_p = peak_r = peak_g = 0;
                    s_out.cal_state = IMU_CAL_OK;
                    ESP_LOGI(TAG, "Calibrated: up=(%.3f %.3f %.3f) bias=(%.4f %.4f %.4f)",
                             s_cfg.up.x, s_cfg.up.y, s_cfg.up.z, wm.x, wm.y, wm.z);
                }
                cal_n = -1;
            } else {
                /* sapma bilinmediği için ortalamadan sapmayı izle */
                v3 wm = v_mul(cal_w, 1.0f / cal_n);
                cal_wmax = fmaxf(cal_wmax, v_len(v_sub(w_raw, wm)));
                cal_state_now = IMU_CAL_RUNNING;
            }
        }

        /* --- OBD hızı: boyuna ivme (yalnız OBD hazırken) --- */
        const vehicle_data_t *vd = vehicle_data_get();
        bool spd_ok = vd->state == OBD_STATE_READY && vd->speed_ts && now - vd->speed_ts < SPD_STALE_MS;
        float v_ms = spd_ok ? vd->speed / 3.6f : 0;
        if (spd_ok && vd->speed_ts != v_ts_prev) {
            if (v_ts_prev && vd->speed_ts > v_ts_prev) {
                float d = (v_ms - v_prev) / ((vd->speed_ts - v_ts_prev) / 1000.0f);
                a_long += ALONG_LPF * (d - a_long);
            }
            v_prev = v_ms;
            v_ts_prev = vd->speed_ts;
        }
        if (!spd_ok) {
            a_long = 0;
            v_ts_prev = 0;
        }

        /* --- yerçekimi yönünü jiroskopla taşı --- */
        if (!seeded) {
            gs = v_norm(a);
            if (!s_cfg.calibrated) {
                /* kalibrasyon yok: açılıştaki duruş = düz (montaj ekseni tahmin edilmez);
                 * ilk durmada oturmuş yönle yeniden kurulur */
                m = build_mount(gs, s_cfg.fwd_hint);
            }
            seeded = true;
        } else {
            gs = v_norm(v_add(gs, v_mul(v_cross(gs, w), dt)));
        }

        /* --- dinamik ivme telafisi (araç → sensör) --- */
        v3 wv = to_vehicle(&m, w);
        v3 a_dyn = {0};
        if (spd_ok) {
            float along = fabsf(a_long) > ALONG_DEADBAND ? a_long : 0;
            a_dyn = to_sensor(&m, (v3){ along, v_ms * wv.z, 0 });   /* sola dönüş: +y */
        }
        v3 am = v_sub(a, a_dyn);
        float amn = v_len(am);

        /* --- durma algısı + sapma öğrenimi --- */
        /* Sapma henüz bilinmeyebilir: büyüklük değil sabitlik (+ kaba üst sınır) */
        bool still_now = w_jitter < STILL_RATE && v_len(w_raw) < BIAS_MAX && jitter < STILL_DEV &&
                         (!spd_ok || v_ms < 0.3f);
        if (!still_now) {
            still_since = 0;
        } else if (!still_since) {
            still_since = now;
        }
        bool stationary = still_since && now - still_since > STILL_MS;
        if (stationary && now - still_since > 3000 && !auto_level && !s_cfg.calibrated) {
            auto_level = true;   /* sensör oturdu: açılış duruşunu düz al (süzülmüş ivme) */
            gs = v_norm(a_lpf);
            m = build_mount(gs, s_cfg.fwd_hint);
        }
        if (stationary && cal_n < 0 && v_len(w_raw) < BIAS_MAX) {
            s_cfg.bias = v_add(s_cfg.bias, v_mul(v_sub(w_raw, s_cfg.bias), BIAS_ALPHA));
        }

        /* --- ivmeölçer düzeltmesi (güvene göre) --- */
        float trust = fmaxf(0, 1 - fabsf(amn - gr) / gr / TRUST_DEV) * fmaxf(0, 1 - rate / TRUST_RATE);
        float k = stationary ? K_STILL : K_MOVE * trust;
        if (amn > 1.0f && k > 0) {
            gs = v_norm(v_add(gs, v_mul(v_sub(v_mul(am, 1.0f / amn), gs), k)));
        }

        /* --- araç ekseninde eğim + G --- */
        v3 f = to_vehicle(&m, gs);
        float pitch = atan2f(f.x, sqrtf(f.y * f.y + f.z * f.z)) * 180.0f / M_PI;
        float roll = atan2f(f.y, f.z) * 180.0f / M_PI;
        v3 lin = to_vehicle(&m, v_sub(v_mul(a, 1.0f / gr), gs));
        glong += G_LPF * (lin.x - glong);
        glat += G_LPF * (lin.y - glat);
        if (roll_feed_active()) {
            /* ROLL: süzülmemiş araç ekseni ivmesi (ölçek telafili) + yerçekimsiz boyuna */
            v3 av = v_mul(to_vehicle(&m, a), G0 / gr);
            const float a3[3] = { av.x, av.y, av.z };
            roll_feed_imu(t_us, a3, lin.x * G0, wv.z, pitch);
        }

        if (cal_n < 0) {
            peak_p = fmaxf(peak_p, fabsf(pitch));
            peak_r = fmaxf(peak_r, fabsf(roll));
            peak_g = fmaxf(peak_g, sqrtf(glong * glong + glat * glat));
        }

        /* --- ileri yön öğrenimi: düz giderken güçlü hızlanma / fren --- */
        if (spd_ok && fabsf(a_long) > FWD_MIN_ACC && fabsf(wv.z) < FWD_MAX_YAW && v_ms > 3) {
            v3 ah = v_sub(a, v_mul(gs, v_dot(a, gs)));   /* yatay bileşen */
            if (v_len(ah) > 0.8f) {
                fwd_acc = v_add(fwd_acc, v_mul(v_norm(ah), (a_long > 0 ? 1.0f : -1.0f) * dt));
                fwd_time += dt;
            }
            if (fwd_time >= FWD_NEED_S) {
                v3 nf = v_norm(v_sub(fwd_acc, v_mul(m.up, v_dot(fwd_acc, m.up))));
                float ang = acosf(fmaxf(-1, fminf(1, v_dot(nf, m.fwd)))) * 180.0f / M_PI;
                if (ang > FWD_MIN_DEG) {
                    s_cfg.fwd_hint = nf;
                    m = build_mount(s_cfg.up, s_cfg.fwd_hint);
                    ESP_LOGI(TAG, "Forward axis learned (%.1f deg change)", ang);
                }
                s_cfg.fwd_learned = true;
                cfg_save();
                fwd_acc = (v3){0};
                fwd_time = 0;
            }
        }

        if (!logged_state && now > 9000) {
            logged_state = true;
            ESP_LOGI(TAG, "State: stationary=%d bias=(%.4f %.4f %.4f) pitch=%.1f roll=%.1f g_ref=%.2f",
                     stationary, s_cfg.bias.x, s_cfg.bias.y, s_cfg.bias.z, pitch, roll, gr);
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_out.pitch = pitch;
        s_out.roll = roll;
        s_out.g_long = glong;
        s_out.g_lat = glat;
        s_out.peak_pitch = peak_p;
        s_out.peak_roll = peak_r;
        s_out.peak_g = peak_g;
        s_out.temperature = raw.temperature;
        s_out.ts = now;
        s_out.fresh = true;
        s_out.stationary = stationary;
        s_out.speed_comp = spd_ok;
        if (cal_state_now == IMU_CAL_RUNNING) {
            s_out.cal_state = IMU_CAL_RUNNING;
            s_out.cal_progress = (float)cal_n / CAL_SAMPLES;
        }
        xSemaphoreGive(s_mutex);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

/* --- API ------------------------------------------------------------------- */

void imu_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    memset(&s_out, 0, sizeof(s_out));
    cfg_load();
    if (!qmi8658_init()) {
        ESP_LOGE(TAG, "QMI8658 init failed — IMU will be unavailable");
        return;
    }
    /* Açılışta bloklayan kalibrasyon yok: sapma NVS'den + araç dururken öğrenilir. */
    ESP_LOGI(TAG, "IMU ready (%s, bias %.4f %.4f %.4f)", s_cfg.calibrated ? "calibrated" : "default mount",
             s_cfg.bias.x, s_cfg.bias.y, s_cfg.bias.z);
}

void imu_start(void)
{
    if (s_running) {
        return;
    }
    s_running = true;
    if (xTaskCreate(imu_task, "imu_task", 4096, NULL, configMAX_PRIORITIES - 2, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create IMU task");
        s_running = false;
    }
}

void imu_stop(void)
{
    s_running = false;
}

void imu_get_snapshot(imu_snapshot_t *snap)
{
    if (!snap) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snap->pitch_deg = s_out.pitch;
    snap->roll_deg = s_out.roll;
    snap->g_long = s_out.g_long;
    snap->g_lat = s_out.g_lat;
    snap->peak_pitch = s_out.peak_pitch;
    snap->peak_roll = s_out.peak_roll;
    snap->peak_g = s_out.peak_g;
    snap->temperature = s_out.temperature;
    snap->fresh = s_out.fresh && lv_tick_get() - s_out.ts < IMU_STALE_MS;
    snap->stationary = s_out.stationary;
    snap->speed_comp = s_out.speed_comp;
    snap->calib_state = s_out.cal_state;
    snap->calib_progress = s_out.cal_progress;
    xSemaphoreGive(s_mutex);
    snap->calibrated = s_cfg.calibrated;
    snap->fwd_learned = s_cfg.fwd_learned;
}

bool imu_is_fresh(void)
{
    imu_snapshot_t s;
    imu_get_snapshot(&s);
    return s.fresh;
}

void imu_calib_start(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_out.cal_state = IMU_CAL_RUNNING;
    s_out.cal_progress = 0;
    xSemaphoreGive(s_mutex);
    s_req_cal = true;
}

void imu_level_zero(void)
{
    s_req_zero = true;
}

void imu_reset_peaks(void)
{
    s_req_peaks = true;
}

void imu_calib_clear(void)
{
    s_req_clear = true;
}
