#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nav_state.h"
#include "nav_map.h"

/* Telefon ↔ ESP32 navigasyon protokolü (v1) — taşıma ve kodlamadan bağımsız
 * mesaj modeli. Kodlayıcı (codec) bayt ↔ nav_msg_t çevirir: bugün JSON,
 * ileride ikili paket + CRC aynı arayüzle eklenir.
 *
 * Telefon → ESP:  hello, start, upd, stop, ping, loc, alert
 * ESP → telefon:  status (hello yanıtı), pong, mapack, mapreq
 *
 * upd'de yalnızca gelen alanlar uygulanır (fields bitleri); kaynak bir alanı
 * bilmiyorsa (ör. Yandex sokak adı) göndermez, ekrandaki değer korunur. */

#define NAV_PROTO_VERSION 1

typedef enum {
    NAV_MSG_NONE = 0,
    NAV_MSG_HELLO,
    NAV_MSG_START,
    NAV_MSG_UPDATE,
    NAV_MSG_STOP,
    NAV_MSG_PING,
    NAV_MSG_STATUS,
    NAV_MSG_PONG,
    NAV_MSG_LOC,       /* telefon GPS: lat, lon, spd, hdg */
    NAV_MSG_ALERT,     /* radar / trafik / tehlike / bilgi */
    NAV_MSG_MAP,       /* harita resmi başlığı; baytlar MAP kanalından */
    NAV_MSG_MAP_ACK,   /* ESP → telefon: harita sonucu (map.id, ok, src = neden) */
    NAV_MSG_MAP_REQ,   /* ESP → telefon: harita iste (src = "fit" | "follow", map.zoom) */
} nav_msg_type_t;

enum {
    NAV_F_MAN     = 1 << 0,
    NAV_F_MAN_D   = 1 << 1,
    NAV_F_REMAIN  = 1 << 2,
    NAV_F_ETA     = 1 << 3,
    NAV_F_SPEED   = 1 << 4,
    NAV_F_HEADING = 1 << 5,
    NAV_F_ROAD    = 1 << 6,
    NAV_F_NEXT    = 1 << 7,
    NAV_F_DEST    = 1 << 8,
    NAV_F_POS     = 1 << 9,
    NAV_F_TIME    = 1 << 10,   /* hello / ping: telefon saati (ts, tz) */
};

typedef struct {
    nav_msg_type_t type;
    uint8_t        version;
    uint32_t       seq;
    uint32_t       fields;            /* NAV_F_* — hangi alanlar geldi */
    nav_maneuver_t maneuver;
    uint32_t       maneuver_dist_m;
    uint32_t       remain_m;
    int16_t        eta_min;
    float          speed_kmh;
    float          heading;
    char           road[NAV_TEXT_LEN];
    char           next_road[NAV_TEXT_LEN];
    char           destination[NAV_TEXT_LEN];
    char           src[16];           /* hello: veri kaynağı ("yandex", "gmaps") */
    double         lat;
    double         lon;
    int64_t        ts;                /* NAV_F_TIME: UTC epoch saniye */
    int16_t        tz_min;            /* NAV_F_TIME: yerel ofset, dakika */
    nav_alert_t    alert;             /* NAV_MSG_ALERT */
    nav_map_hdr_t  map;               /* NAV_MSG_MAP */
} nav_msg_t;

typedef struct {
    const char *name;
    /* false: bozuk / tanınmayan mesaj */
    bool (*decode)(const uint8_t *buf, size_t len, nav_msg_t *out);
    /* Yazılan bayt sayısı; 0 = sığmadı */
    size_t (*encode)(const nav_msg_t *msg, uint8_t *buf, size_t cap);
} nav_codec_t;

extern const nav_codec_t nav_codec_json;
