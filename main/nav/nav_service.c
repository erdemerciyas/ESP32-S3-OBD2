#include "nav_service.h"
#include "nav_state.h"
#include "nav_protocol.h"
#include "nav_transport.h"
#include "nav_mock.h"
#include "nav_track.h"
#include "app_log.h"
#include "clock.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

/* Taşıma → kodlayıcı → nav_state. Hangi radyonun ve hangi kodlamanın
 * kullanıldığı yalnızca bu iki işaretçide. */

static const char *TAG = "nav";

#ifdef CONFIG_NAV_MOCK
#define NAV_USE_MOCK 1
#else
#define NAV_USE_MOCK 0
#endif

static const nav_transport_t *s_transport = &nav_transport_ble;
static const nav_codec_t     *s_codec = &nav_codec_json;
static bool s_trip_open;   /* start geldi, stop gelmedi */

static void reply(nav_msg_type_t type, uint32_t seq)
{
    nav_msg_t msg = { .type = type, .seq = seq };
    uint8_t buf[64];
    size_t n = s_codec->encode(&msg, buf, sizeof(buf));
    if (n) {
        s_transport->send(buf, n);
    }
}

static void apply_fields(nav_state_t *st, const nav_msg_t *m)
{
    if (m->fields & NAV_F_MAN)     st->maneuver = m->maneuver;
    if (m->fields & NAV_F_MAN_D)   st->maneuver_dist_m = m->maneuver_dist_m;
    if (m->fields & NAV_F_REMAIN)  st->remain_m = m->remain_m;
    if (m->fields & NAV_F_ETA)     st->eta_min = m->eta_min;
    if (m->fields & NAV_F_SPEED)   st->speed_kmh = m->speed_kmh;
    if (m->fields & NAV_F_HEADING) st->heading = m->heading;
    if (m->fields & NAV_F_ROAD)    memcpy(st->cur_road, m->road, sizeof(st->cur_road));
    if (m->fields & NAV_F_NEXT)    memcpy(st->next_road, m->next_road, sizeof(st->next_road));
    if (m->fields & NAV_F_DEST)    memcpy(st->destination, m->destination, sizeof(st->destination));
    if (m->fields & NAV_F_POS) {
        st->has_fix = true;
        st->lat = m->lat;
        st->lon = m->lon;
    }
}

static void on_rx(int ch, const uint8_t *data, size_t len)
{
    if (ch == NAV_CH_BULK) {
        nav_map_chunk(data, len);
        return;
    }

    nav_msg_t msg;
    if (!s_codec->decode(data, len, &msg)) {
        app_log_warn(TAG, "Bad message (%u bytes)", (unsigned)len);
        return;
    }

    /* Yeni sürüş yalnızca açık sürüş yokken başlar: BLE kopup yeniden
     * bağlanınca gelen start kaydı silmesin. */
    if (msg.type == NAV_MSG_START && !s_trip_open) {
        s_trip_open = true;
        nav_track_reset();
    } else if (msg.type == NAV_MSG_STOP) {
        s_trip_open = false;
    }

    nav_state_t *st = nav_state_begin();
    st->connected = true;
    switch (msg.type) {
    case NAV_MSG_START:
    case NAV_MSG_UPDATE:
        st->active = true;
        apply_fields(st, &msg);
        break;
    case NAV_MSG_STOP:
        st->active = false;
        st->maneuver = NAV_MAN_UNKNOWN;
        break;
    case NAV_MSG_LOC:
        apply_fields(st, &msg);
        break;
    case NAV_MSG_ALERT:
        st->alert = msg.alert;
        st->alert.ts_ms = nav_state_now_ms();
        break;
    default:
        break;
    }
    nav_state_commit();

    if (s_trip_open && (msg.fields & NAV_F_POS)) {
        nav_track_add(msg.lat, msg.lon);
    }
    if (s_trip_open && (msg.fields & NAV_F_NEXT)) {
        nav_track_add_road(msg.next_road);
    }
    if (s_trip_open && (msg.fields & NAV_F_ROAD)) {
        nav_track_add_road(msg.road);
    }

    if (msg.fields & NAV_F_TIME) {
        clock_set_from_phone(msg.ts, msg.tz_min);
    }

    if (msg.type == NAV_MSG_MAP) {
        nav_map_begin(&msg.map);
    }

    if (msg.type == NAV_MSG_HELLO) {
        app_log_info(TAG, "Hello from %s (v%u)", msg.src[0] ? msg.src : "?", msg.version);
        reply(NAV_MSG_STATUS, msg.seq);
    } else if (msg.type == NAV_MSG_PING) {
        reply(NAV_MSG_PONG, msg.seq);
    } else if (msg.type == NAV_MSG_START || msg.type == NAV_MSG_STOP) {
        app_log_info(TAG, "Route %s", msg.type == NAV_MSG_START ? "started" : "stopped");
    }
}

/* ESP → telefon: harita sonucu (telefon günlüğünde görünür). */
static void on_map_result(uint8_t id, const char *why)
{
    nav_msg_t msg = { .type = NAV_MSG_MAP_ACK };
    msg.map.id = id;
    if (why) {
        snprintf(msg.src, sizeof(msg.src), "%s", why);
    }
    uint8_t buf[80];
    size_t n = s_codec->encode(&msg, buf, sizeof(buf));
    if (n) {
        s_transport->send(buf, n);
    }
}

static void on_link(bool up)
{
    if (up) {
        nav_state_t *st = nav_state_begin();
        st->connected = true;
        nav_state_commit();
    } else {
        nav_state_reset();
    }
}

void nav_service_init(void)
{
    nav_state_init();
    nav_track_init();
    nav_map_init();
    nav_map_set_result_cb(on_map_result);
}

void nav_service_start(void)
{
    nav_state_reset();
    if (NAV_USE_MOCK) {
        nav_mock_start();
        app_log_info(TAG, "Nav service started (mock)");
    } else if (s_transport->start(on_rx, on_link)) {
        app_log_info(TAG, "Nav service started (%s/%s)", s_transport->name, s_codec->name);
    }
}

void nav_service_stop(void)
{
    if (NAV_USE_MOCK) {
        nav_mock_stop();
    } else {
        s_transport->stop();
    }
    nav_state_reset();
    app_log_info(TAG, "Nav service stopped");
}

void nav_service_request_map(bool fit, int zoom)
{
    nav_msg_t msg = { .type = NAV_MSG_MAP_REQ };
    snprintf(msg.src, sizeof(msg.src), "%s", fit ? "fit" : "follow");
    msg.map.zoom = (uint8_t)zoom;
    uint8_t buf[64];
    size_t n = s_codec->encode(&msg, buf, sizeof(buf));
    if (n) {
        s_transport->send(buf, n);
    }
}
