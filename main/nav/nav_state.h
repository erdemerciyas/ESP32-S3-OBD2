#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Navigasyon durumu — OBD verisinden (vehicle_data) tamamen bağımsız.
 * Yazanlar (mock / telefon servisi) nav_state_begin()/commit() arasında alanları
 * değiştirir; UI nav_state_rev() değişince snapshot alır. */

typedef enum {
    NAV_MAN_UNKNOWN = 0,
    NAV_MAN_STRAIGHT,
    NAV_MAN_LEFT,
    NAV_MAN_RIGHT,
    NAV_MAN_SLIGHT_LEFT,
    NAV_MAN_SLIGHT_RIGHT,
    NAV_MAN_UTURN,
    NAV_MAN_ROUNDABOUT,
    NAV_MAN_ARRIVAL,
    NAV_MAN_COUNT,
} nav_maneuver_t;

#define NAV_TEXT_LEN 48

typedef enum {
    NAV_ALERT_NONE = 0,
    NAV_ALERT_CAMERA,       /* radar / hız kamerası */
    NAV_ALERT_TRAFFIC,      /* yoğun trafik */
    NAV_ALERT_HAZARD,       /* kaza, yol çalışması, tehlike */
    NAV_ALERT_INFO,         /* öneri / bilgi */
} nav_alert_kind_t;

typedef struct {
    nav_alert_kind_t kind;
    uint32_t         dist_m;           /* 0 = bilinmiyor */
    uint16_t         limit_kmh;        /* 0 = bilinmiyor */
    char             text[NAV_TEXT_LEN];
    uint32_t         ts_ms;            /* geldiği an; UI süre aşımında gizler */
} nav_alert_t;

typedef struct {
    bool           connected;          /* telefon bağlı (handshake tamam) */
    bool           active;             /* rota rehberliği sürüyor */
    nav_maneuver_t maneuver;
    uint32_t       maneuver_dist_m;
    uint32_t       remain_m;
    int16_t        eta_min;            /* gece yarısından dakika; -1 = bilinmiyor */
    float          speed_kmh;          /* telefon GPS; <0 = bilinmiyor */
    float          heading;            /* derece; <0 = bilinmiyor */
    char           cur_road[NAV_TEXT_LEN];
    char           next_road[NAV_TEXT_LEN];
    char           destination[NAV_TEXT_LEN];
    bool           has_fix;            /* telefon GPS konumu geldi */
    double         lat;
    double         lon;
    nav_alert_t    alert;
    uint32_t       last_rx_ms;         /* son güncelleme (esp_timer ms) */
    uint32_t       rev;                /* her commit'te artar */
} nav_state_t;

void nav_state_init(void);
/* Bağlantı yok, rota yok — tüm alanlar varsayılana döner. */
void nav_state_reset(void);

/* Kilitler ve yazılabilir durumu döndürür; commit kilidi açar, rev/last_rx_ms
 * günceller. */
nav_state_t *nav_state_begin(void);
void nav_state_commit(void);

void nav_state_snapshot(nav_state_t *out);
uint32_t nav_state_rev(void);
uint32_t nav_state_now_ms(void);
