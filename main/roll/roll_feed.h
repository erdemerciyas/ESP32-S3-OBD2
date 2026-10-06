#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ROLL (performans ölçümü) veri girişi — F0: ölçüm altyapısı.
 *
 * Tüm örnekler alındıkları anda ESP zaman tabanıyla (esp_timer, µs)
 * damgalanır. Kaynaklar:
 *   - telefon GNSS (BLE "gnss" mesajı; Doppler hızı, saat senkronlu zaman)
 *   - OBD hız PID 0x0D (ham, filtresiz; istek gönderim + yanıt zamanı)
 *   - IMU 100 Hz (araç ekseninde ivme, yaw hızı, pitch)
 *
 * Hız kaynağı önceliği: telefon bağlı ve GNSS taze/yeterli ise GPS, değilse
 * OBD (ayarlardan yalnız GPS / yalnız OBD seçilebilir; bkz. roll_cfg.h).
 * Kaynaklar arası anlık hız IMU ile taşınır (en çok 1.5 sn).
 *
 * ROLL modunda ham akış telefona TEL karakteristiğinden ikili gönderilir
 * (telefon CSV'ye yazar; analiz: scripts/roll_analyze.py). Çerçeve, LE:
 *   [tip u8][adet u8][kayıtlar]
 *   tip 0x01 IMU, 14 B: u32 t_us, i16 ax, ay, az (mm/s², yerçekimi dahil;
 *                       x ileri, y sol, z yukarı), i16 gz (0.01 °/s), i16 pitch (0.01 °)
 *   tip 0x02 OBD,  9 B: u32 t_tx_us, u32 t_rx_us, u8 km/h
 * t_us: esp_timer'ın alt 32 biti; telefon pong'daki 64 bit "e" ile açar. */

#define ROLL_TEL_IMU 0x01
#define ROLL_TEL_OBD 0x02

typedef enum {
    ROLL_SRC_NONE = 0,
    ROLL_SRC_GPS,
    ROLL_SRC_OBD,
} roll_src_t;

typedef struct {
    int64_t t_us;        /* fix zamanı (ESP µs); senkron yoksa varış zamanı */
    bool    synced;      /* t_us telefon saat senkronundan geldi */
    float   speed_ms;    /* Doppler hızı; <0 fix'te hız yok */
    float   speed_acc;   /* m/s, 1σ; <0 bilinmiyor */
    float   alt_m;       /* MSL (yoksa elipsoit) */
    float   alt_acc;     /* m; <0 bilinmiyor */
    float   h_acc;       /* m */
    float   heading;     /* °; <0 bilinmiyor */
    int16_t sats;        /* fix'te kullanılan uydu; <0 bilinmiyor */
    double  lat, lon;
} roll_gnss_t;

typedef struct {
    roll_src_t src;
    float    speed_kmh;      /* seçili kaynak + IMU taşıması */
    bool     phone;          /* telefon bağlı */
    bool     tel;            /* telemetri telefona akıyor */
    /* GPS */
    bool     gps_ok;
    bool     gps_synced;
    uint32_t gps_age_ms;
    float    gps_hz;
    float    gps_kmh;
    float    gps_acc_kmh;    /* <0 bilinmiyor */
    int      gps_sats;       /* <0 bilinmiyor */
    float    gps_alt;
    /* OBD */
    bool     obd_ok;
    uint32_t obd_age_ms;
    float    obd_hz;
    int      obd_kmh;
    /* IMU */
    float    a_long;         /* m/s², yerçekimsiz */
    float    pitch_deg;
} roll_snapshot_t;

void roll_feed_init(void);
/* ROLL modu: telemetri görevi + kaynak kancaları açılır/kapanır. */
void roll_feed_set_active(bool on);
bool roll_feed_active(void);
void roll_feed_set_phone(bool up);

void roll_feed_gnss(const roll_gnss_t *fix);
void roll_feed_obd_speed(int64_t t_tx_us, int64_t t_rx_us, int kmh);
/* a: araç ekseninde özgül kuvvet (m/s², yerçekimi dahil); a_long: yerçekimsiz
 * boyuna ivme (m/s²); gz: yaw hızı (rad/s); pitch: ° */
void roll_feed_imu(int64_t t_us, const float a[3], float a_long, float gz, float pitch_deg);

void roll_feed_snapshot(roll_snapshot_t *out);

/* CONFIG_UI_SHOT_TOUR: ekran görüntüsü için sabit demo anlık görüntüsü (NULL: kapalı) */
void roll_feed_demo(const roll_snapshot_t *demo);
