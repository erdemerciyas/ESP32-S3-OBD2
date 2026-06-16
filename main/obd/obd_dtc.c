#include "obd_dtc.h"
#include "elm327.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "app_log.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "obd_dtc";

static bool s_scan_busy;

static char dtc_digit(uint8_t b)
{
    static const char table[] = "PCBU";
    return table[(b >> 6) & 0x03];
}

static const char *skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static bool read_hex_pair(const char **p, int *out)
{
    const char *s = skip_ws(*p);
    if (!s[0] || !s[1]) {
        return false;
    }
    int v = 0;
    if (sscanf(s, "%2x", &v) != 1) {
        return false;
    }
    *out = v;
    *p = skip_ws(s + 2);
    return true;
}

static bool response_is_error(const char *resp)
{
    return strstr(resp, "NO DATA") != NULL ||
           strstr(resp, "UNABLE") != NULL ||
           strstr(resp, "ERROR") != NULL ||
           strstr(resp, "STOPPED") != NULL;
}

static const char *find_mode_marker(const char *resp, const char *mode)
{
    size_t mode_len = strlen(mode);
    const char *p = resp;

    while ((p = strstr(p, mode)) != NULL) {
        if (p > resp && isxdigit((unsigned char)p[-1])) {
            p += mode_len;
            continue;
        }
        return p;
    }
    return NULL;
}

static bool protocol_is_can(void)
{
    const vehicle_profile_t *p = vehicle_profile_get();
    /* ATSP6 = ISO 15765-4 CAN, ATSP7 = ISO 15765-4 CAN 29-bit */
    return strstr(p->init_protocol_cmd, "ATSP6") != NULL ||
           strstr(p->init_protocol_cmd, "ATSP7") != NULL;
}

static int count_hex_bytes(const char *p)
{
    int n = 0;
    while (p && *p) {
        p = skip_ws(p);
        if (isxdigit((unsigned char)p[0]) && p[1] && isxdigit((unsigned char)p[1])) {
            n++;
            p += 2;
        } else {
            break;
        }
    }
    return n;
}

static void parse_dtcs(const char *resp, bool pending)
{
    if (response_is_error(resp)) {
        app_log_warn(TAG, "%s DTC read error: %s", pending ? "Pending" : "Active", resp);
        return;
    }

    const char *mode = pending ? "47" : "43";
    const char *p = find_mode_marker(resp, mode);
    if (!p) {
        app_log_warn(TAG, "No %s marker in response: %s", mode, resp);
        return;
    }

    const char *data = skip_ws(p + 2);
    int count = 0;

    if (protocol_is_can()) {
        /* CAN (ISO 15765): 43 <count_byte> <DTC pairs...> */
        if (sscanf(data, "%2x", &count) != 1) {
            app_log_warn(TAG, "Invalid DTC count in: %s", resp);
            return;
        }
        data = skip_ws(data + 2);
    } else {
        /* KWP / ISO 9141 / J1850: 43 <DTC pairs...> — no count byte */
        count = count_hex_bytes(data) / 2;  /* 2 bytes per DTC */
    }

    vehicle_data_lock();
    vehicle_data_t *vd = vehicle_data_get();
    if (pending) {
        vd->dtc_pending_count = count;
    } else {
        vd->dtc_count = 0;
        for (int i = 0; i < count && vd->dtc_count < VEHICLE_DTC_MAX; i++) {
            int b1 = 0, b2 = 0;
            if (!read_hex_pair(&data, &b1) || !read_hex_pair(&data, &b2)) {
                break;
            }
            snprintf(vd->dtcs[vd->dtc_count], VEHICLE_DTC_LEN, "%c%X%X%X%X",
                     dtc_digit((uint8_t)b1),
                     (b1 >> 4) & 0x03,
                     b1 & 0x0F,
                     (b2 >> 4) & 0x0F,
                     b2 & 0x0F);
            vd->dtc_count++;
        }
    }
    int active = vd->dtc_count;
    int pending_n = vd->dtc_pending_count;
    vehicle_data_unlock();

    app_log_info(TAG, "%s DTCs: stored=%d pending=%d",
                 pending ? "Pending" : "Active", active, pending_n);
}

static void finish_scan(bool ok, const char *msg)
{
    s_scan_busy = false;
    vehicle_data_set_dtc_scan(ok ? DTC_SCAN_DONE : DTC_SCAN_ERROR, msg);
}

static void pending_cb(const char *resp, void *user_data)
{
    (void)user_data;
    parse_dtcs(resp, true);
    finish_scan(true, "Scan complete");
}

static void active_cb(const char *resp, void *user_data)
{
    (void)user_data;
    parse_dtcs(resp, false);
    if (!elm327_send_cmd_prio("07", pending_cb, NULL, 8000, true)) {
        finish_scan(false, "Pending scan failed");
    }
}

static void clear_cb(const char *resp, void *user_data)
{
    (void)resp;
    (void)user_data;
    vehicle_data_lock();
    vehicle_data_t *vd = vehicle_data_get();
    vd->dtc_count = 0;
    vd->dtc_pending_count = 0;
    memset(vd->dtcs, 0, sizeof(vd->dtcs));
    vehicle_data_unlock();
    finish_scan(true, "Codes cleared");
    app_log_info(TAG, "DTCs cleared");
}

bool obd_dtc_is_busy(void)
{
    return s_scan_busy;
}

void obd_dtc_init(void)
{
}

void obd_dtc_read_all(void)
{
    if (!elm327_is_ready()) {
        vehicle_data_set_dtc_scan(DTC_SCAN_ERROR, "OBD not ready");
        return;
    }
    if (s_scan_busy) {
        return;
    }
    if (!elm327_can_queue(true)) {
        vehicle_data_set_dtc_scan(DTC_SCAN_ERROR, "Adapter busy");
        return;
    }

    s_scan_busy = true;
    vehicle_data_lock();
    vehicle_data_t *vd = vehicle_data_get();
    vd->dtc_count = 0;
    vd->dtc_pending_count = 0;
    memset(vd->dtcs, 0, sizeof(vd->dtcs));
    vehicle_data_unlock();
    vehicle_data_set_dtc_scan(DTC_SCAN_BUSY, "Reading fault codes...");

    app_log_info(TAG, "Scan all DTCs");
    if (!elm327_send_cmd_prio("03", active_cb, NULL, 8000, true)) {
        s_scan_busy = false;
        vehicle_data_set_dtc_scan(DTC_SCAN_ERROR, "Queue full");
    }
}

void obd_dtc_read_active(void)
{
    obd_dtc_read_all();
}

void obd_dtc_read_pending(void)
{
    /* pending is chained from active_cb */
}

void obd_dtc_clear(void)
{
    if (!elm327_is_ready()) {
        return;
    }
    if (s_scan_busy) {
        return;
    }
    s_scan_busy = true;
    vehicle_data_set_dtc_scan(DTC_SCAN_BUSY, "Clearing codes...");
    app_log_info(TAG, "Clear DTCs");
    if (!elm327_send_cmd_prio("04", clear_cb, NULL, 8000, true)) {
        s_scan_busy = false;
        vehicle_data_set_dtc_scan(DTC_SCAN_ERROR, "Clear failed");
    }
}

void obd_dtc_reset(void)
{
    s_scan_busy = false;
    vehicle_data_set_dtc_scan(DTC_SCAN_IDLE, "");
}
