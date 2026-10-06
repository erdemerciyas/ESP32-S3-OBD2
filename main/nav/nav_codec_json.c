#include "nav_protocol.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"

/* JSON kodlayıcı (prototip). Kısa anahtarlar — tek BLE yazmasına sığsın:
 *
 *   {"t":"hello","v":1,"src":"yandex","ts":1791316800,"tz":180}
 *   {"t":"start","dst":"Ulus"}
 *   {"t":"upd","seq":42,"m":"R","md":800,"rd":12400,"eta":1122,
 *    "spd":54,"hdg":90,"road":"Ankara Cd.","next":"Atatürk Blv.","dst":"Ulus"}
 *   {"t":"stop"}   {"t":"ping","seq":7,"ts":1791316800,"tz":180}
 *   {"t":"loc","lat":39.92,"lon":32.85,"spd":54,"hdg":90}
 *   {"t":"alert","k":"cam","d":350,"lim":50,"txt":"Radar"}   k: cam traffic hazard info none
 *   {"t":"map","id":7,"len":34567,"w":460,"h":460,"clat":39.92,"clon":32.85,"z":15}
 *
 * m: S düz, L sol, R sağ, SL hafif sol, SR hafif sağ, U u-dönüşü,
 *    RB döner kavşak, A varış; diğer her şey bilinmeyen.
 * eta: gece yarısından dakika (18:42 → 1122). Mesafeler metre.
 * ts: UTC epoch saniye, tz: yerel ofset dakika (saat senkronu). */

static const struct {
    const char    *code;
    nav_maneuver_t man;
} s_man_codes[] = {
    { "S",  NAV_MAN_STRAIGHT },
    { "L",  NAV_MAN_LEFT },
    { "R",  NAV_MAN_RIGHT },
    { "SL", NAV_MAN_SLIGHT_LEFT },
    { "SR", NAV_MAN_SLIGHT_RIGHT },
    { "U",  NAV_MAN_UTURN },
    { "RB", NAV_MAN_ROUNDABOUT },
    { "A",  NAV_MAN_ARRIVAL },
};

static const struct {
    const char    *name;
    nav_msg_type_t type;
} s_types[] = {
    { "hello",  NAV_MSG_HELLO },
    { "start",  NAV_MSG_START },
    { "upd",    NAV_MSG_UPDATE },
    { "stop",   NAV_MSG_STOP },
    { "ping",   NAV_MSG_PING },
    { "status", NAV_MSG_STATUS },
    { "pong",   NAV_MSG_PONG },
    { "loc",    NAV_MSG_LOC },
    { "alert",  NAV_MSG_ALERT },
    { "map",    NAV_MSG_MAP },
};

/* UTF-8 karakterini ortadan bölmeden kopyala (ekranda bozuk glif olmasın). */
static void copy_utf8(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) {
            n--;   /* devam baytında kesme: karakterin başına geri çekil */
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool get_num(const cJSON *root, const char *key, double *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(it)) {
        return false;
    }
    *out = it->valuedouble;
    return true;
}

static bool get_str(const cJSON *root, const char *key, char *dst, size_t cap)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(it) || !it->valuestring) {
        return false;
    }
    copy_utf8(dst, cap, it->valuestring);
    return true;
}

static uint32_t clamp_u32(double v)
{
    if (v <= 0) {
        return 0;
    }
    return v > 10000000.0 ? 10000000u : (uint32_t)(v + 0.5);
}

static bool json_decode(const uint8_t *buf, size_t len, nav_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->eta_min = -1;
    out->speed_kmh = -1.0f;
    out->heading = -1.0f;

    cJSON *root = cJSON_ParseWithLength((const char *)buf, len);
    if (!root) {
        return false;
    }

    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (cJSON_IsString(t) && t->valuestring) {
        for (size_t i = 0; i < sizeof(s_types) / sizeof(s_types[0]); i++) {
            if (strcmp(t->valuestring, s_types[i].name) == 0) {
                out->type = s_types[i].type;
                break;
            }
        }
    }

    double v;
    char code[4];
    out->version = get_num(root, "v", &v) ? (uint8_t)v : NAV_PROTO_VERSION;
    if (get_num(root, "seq", &v)) {
        out->seq = clamp_u32(v);
    }
    if (get_str(root, "m", code, sizeof(code))) {
        out->fields |= NAV_F_MAN;
        out->maneuver = NAV_MAN_UNKNOWN;
        for (size_t i = 0; i < sizeof(s_man_codes) / sizeof(s_man_codes[0]); i++) {
            if (strcmp(code, s_man_codes[i].code) == 0) {
                out->maneuver = s_man_codes[i].man;
                break;
            }
        }
    }
    if (get_num(root, "md", &v)) {
        out->fields |= NAV_F_MAN_D;
        out->maneuver_dist_m = clamp_u32(v);
    }
    if (get_num(root, "rd", &v)) {
        out->fields |= NAV_F_REMAIN;
        out->remain_m = clamp_u32(v);
    }
    if (get_num(root, "eta", &v)) {
        out->fields |= NAV_F_ETA;
        out->eta_min = (v >= 0 && v < 24 * 60) ? (int16_t)v : -1;
    }
    if (get_num(root, "spd", &v)) {
        out->fields |= NAV_F_SPEED;
        out->speed_kmh = (float)v;
    }
    if (get_num(root, "hdg", &v)) {
        out->fields |= NAV_F_HEADING;
        out->heading = (float)v;
    }
    if (get_str(root, "road", out->road, sizeof(out->road))) {
        out->fields |= NAV_F_ROAD;
    }
    if (get_str(root, "next", out->next_road, sizeof(out->next_road))) {
        out->fields |= NAV_F_NEXT;
    }
    if (get_str(root, "dst", out->destination, sizeof(out->destination))) {
        out->fields |= NAV_F_DEST;
    }
    double lat, lon;
    if (get_num(root, "lat", &lat) && get_num(root, "lon", &lon) &&
        lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
        out->fields |= NAV_F_POS;
        out->lat = lat;
        out->lon = lon;
    }
    if (get_num(root, "ts", &v) && v > 0) {
        out->fields |= NAV_F_TIME;
        out->ts = (int64_t)v;
        out->tz_min = get_num(root, "tz", &v) ? (int16_t)v : 0;
    }
    if (out->type == NAV_MSG_ALERT) {
        char kind[8] = "";
        get_str(root, "k", kind, sizeof(kind));
        out->alert.kind = strcmp(kind, "cam") == 0     ? NAV_ALERT_CAMERA
                        : strcmp(kind, "traffic") == 0 ? NAV_ALERT_TRAFFIC
                        : strcmp(kind, "hazard") == 0  ? NAV_ALERT_HAZARD
                        : strcmp(kind, "info") == 0    ? NAV_ALERT_INFO
                                                       : NAV_ALERT_NONE;
        if (get_num(root, "d", &v)) {
            out->alert.dist_m = clamp_u32(v);
        }
        if (get_num(root, "lim", &v) && v > 0 && v < 400) {
            out->alert.limit_kmh = (uint16_t)v;
        }
        get_str(root, "txt", out->alert.text, sizeof(out->alert.text));
    }
    if (out->type == NAV_MSG_MAP) {
        if (get_num(root, "id", &v))   out->map.id = (uint8_t)v;
        if (get_num(root, "len", &v))  out->map.len = clamp_u32(v);
        if (get_num(root, "w", &v))    out->map.w = (uint16_t)v;
        if (get_num(root, "h", &v))    out->map.h = (uint16_t)v;
        if (get_num(root, "z", &v))    out->map.zoom = (uint8_t)v;
        if (get_num(root, "clat", &v)) out->map.lat = v;
        if (get_num(root, "clon", &v)) out->map.lon = v;
    }
    get_str(root, "src", out->src, sizeof(out->src));

    cJSON_Delete(root);
    return out->type != NAV_MSG_NONE;
}

static size_t json_encode(const nav_msg_t *msg, uint8_t *buf, size_t cap)
{
    int n;
    switch (msg->type) {
    case NAV_MSG_STATUS:
        n = snprintf((char *)buf, cap, "{\"t\":\"status\",\"v\":%d,\"dev\":\"AURA\"}",
                     NAV_PROTO_VERSION);
        break;
    case NAV_MSG_PONG:
        n = snprintf((char *)buf, cap, "{\"t\":\"pong\",\"seq\":%u}", (unsigned)msg->seq);
        break;
    case NAV_MSG_MAP_REQ:
        n = snprintf((char *)buf, cap, "{\"t\":\"mapreq\",\"mode\":\"%s\",\"z\":%u}",
                     msg->src, msg->map.zoom);
        break;
    case NAV_MSG_MAP_ACK:
        n = snprintf((char *)buf, cap, "{\"t\":\"mapack\",\"id\":%u,\"ok\":%s,\"why\":\"%s\"}",
                     msg->map.id, msg->src[0] ? "false" : "true", msg->src);
        break;
    default:
        return 0;   /* ESP yalnızca status / pong / mapack / mapreq gönderir */
    }
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

const nav_codec_t nav_codec_json = {
    .name = "json",
    .decode = json_decode,
    .encode = json_encode,
};
