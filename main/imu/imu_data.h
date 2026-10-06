#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Araç eğim ölçer + G-metre (QMI8658, 100 Hz).
 *
 * Sensör → araç dönüşümü montaj kalibrasyonundan (yukarı yön + ileri yön)
 * gelir; cihaz hangi açıyla takılırsa takılsın pitch/roll araç eksenlerinde
 * çıkar. Yerçekimi yönü jiroskopla izlenir, ivmeölçer yalnız güvenilir
 * anlarda (|a| ≈ 1 g, düşük dönüş hızı) düzeltir; OBD hızı varsa viraj
 * (v·ω) ve hızlanma ivmesi çıkarılır. Araç dururken jiroskop sapması
 * kendiliğinden öğrenilir. Araç ekseni: x ileri, y sol, z yukarı. */

typedef enum {
    IMU_CAL_IDLE = 0,
    IMU_CAL_RUNNING,     /* 3 sn hareketsiz örnekleme */
    IMU_CAL_OK,
    IMU_CAL_MOVED,       /* örnekleme sırasında hareket: tekrar dene */
} imu_calib_state_t;

typedef struct {
    float pitch_deg;        /* + burun yukarı */
    float roll_deg;         /* + sağ taraf aşağı */
    float g_long;           /* boyuna ivme, g (+ hızlanma, − fren) */
    float g_lat;            /* yanal ivme, g (+ sola doğru ivme = sağa dönüşte −) */
    float peak_pitch;       /* sıfırlamadan beri en büyük |pitch| */
    float peak_roll;
    float peak_g;           /* en büyük yatay ivme, g */
    float temperature;
    bool  fresh;            /* son 500 ms içinde örnek var */
    bool  stationary;       /* araç duruyor (sapma öğrenimi açık) */
    bool  calibrated;       /* montaj kalibrasyonu yapıldı (yoksa dikey montaj varsayımı) */
    bool  fwd_learned;      /* ileri yön sürüşten öğrenildi */
    bool  speed_comp;       /* OBD hız telafisi etkin */
    imu_calib_state_t calib_state;
    float calib_progress;   /* 0..1 */
} imu_snapshot_t;

void imu_init(void);
void imu_start(void);
void imu_stop(void);
void imu_get_snapshot(imu_snapshot_t *snap);
bool imu_is_fresh(void);

/* Düz zeminde, araç dururken: 3 sn örnekleme → jiroskop sapması + yukarı
 * yön. Sonuç snapshot.calib_state ile izlenir. Asenkron. */
void imu_calib_start(void);
/* Şu anki duruşu "düz" kabul et (ileri yön korunur) + tepe değerleri sıfırla. */
void imu_level_zero(void);
void imu_reset_peaks(void);
/* Montaj kalibrasyonunu ve sapmayı unut (dikey montaj varsayımına dön). */
void imu_calib_clear(void);
