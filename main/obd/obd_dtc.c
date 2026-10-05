#include "obd_dtc.h"
#include "elm327.h"
#include "vehicle_data.h"
#include "app_log.h"

#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "obd_dtc";

static const char *NVS_NS  = "obd_dtc";
static const char *NVS_KEY = "hist";
#define HIST_VERSION 1

/* Otomatik tarama: bağlantı oturduktan bu kadar sonra (PID keşfi + birkaç
 * saniye canlı veri). Arka plan MIL kontrolü (yalnız 0101) periyodu. */
#define AUTO_SCAN_DELAY_MS  8000
#define MONITOR_PERIOD_MS   60000
/* K-line'da 03 birden çok çerçeve döndürebilir; ELM her çerçeveden sonra
 * ATST kadar bekler. Silme (04) ECU tarafında yavaş olabilir. */
#define TMO_MONITOR_MS      1500
#define TMO_STORED_MS       4000
#define TMO_PENDING_MS      2500
#define TMO_FF_MS           1200
#define TMO_CLEAR_MS        5000
#define CLEAR_SETTLE_MS     1500
/* ELM görevi kendi timeout'unu uygular; bu pay yalnız emniyet içindir. */
#define WATCHDOG_EXTRA_MS   1000

typedef enum {
    ST_MONITOR = 0,
    ST_STORED,
    ST_PENDING,
    ST_FF_CODE,
    ST_FF_PID,
    ST_KM_MIL,
    ST_KM_CLEAR,
    ST_CLEAR,
    ST_SETTLE,
} step_t;

typedef enum {
    JOB_SCAN = 1,
    JOB_CLEAR,
    JOB_MONITOR,
} job_t;

typedef struct {
    uint8_t step;
    uint8_t arg;
} plan_item_t;

static const uint8_t s_ff_pids[] = { 0x0C, 0x0D, 0x05, 0x04, 0x06, 0x07, 0x0B };

static SemaphoreHandle_t s_mtx;
static dtc_report_t s_rep;          /* yayınlanan (mutex) */
static dtc_hist_t   s_hist;         /* kalıcı geçmiş (mutex) */
static dtc_report_t s_work;         /* obd_poll görevinde kurulur */
static dtc_hist_t   s_hist_io;      /* NVS yazımı için kopya */

static volatile uint8_t s_req;      /* UI isteği: JOB_SCAN / JOB_CLEAR */
static job_t    s_job;
static bool     s_active;
static plan_item_t s_plan[20];
static uint8_t  s_plan_len;
static uint8_t  s_plan_idx;

static bool     s_inflight;
static uint32_t s_sent_at;
static uint32_t s_timeout;
static uint32_t s_tx_seq;
static volatile uint32_t s_cb_seq;
static char     s_resp[256];
static uint32_t s_settle_until;

static bool     s_link_ready;
static bool     s_auto_done;
static uint32_t s_ready_at;
static uint32_t s_monitor_last;
static bool     s_pending_unsupported; /* 07 bu bağlantıda timeout verdi */
static bool     s_stored_ok;           /* 03 yanıtlandı */
static bool     s_clear_acked;         /* 04 → 44 */

static void lock(void)   { xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mtx); }

static uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void obd_dtc_format(uint16_t code, char *buf)
{
    static const char sys[4] = { 'P', 'C', 'B', 'U' };
    snprintf(buf, 6, "%c%04X", sys[code >> 14], code & 0x3FFF);
}

/* ---------- NVS ---------- */

static void hist_load(void)
{
    nvs_handle_t h;
    memset(&s_hist, 0, sizeof(s_hist));
    s_hist.version = HIST_VERSION;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    dtc_hist_t tmp;
    size_t len = sizeof(tmp);
    if (nvs_get_blob(h, NVS_KEY, &tmp, &len) == ESP_OK && len == sizeof(tmp) &&
        tmp.version == HIST_VERSION && tmp.count <= DTC_HIST_MAX) {
        s_hist = tmp;
    }
    nvs_close(h);
}

/* Yalnız obd_poll görevinden: flash yazımı kısa ama mutex dışında yapılır. */
static void hist_persist(void)
{
    lock();
    s_hist_io = s_hist;
    unlock();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        app_log_warn(TAG, "NVS open failed, history not saved");
        return;
    }
    if (nvs_set_blob(h, NVS_KEY, &s_hist_io, sizeof(s_hist_io)) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        app_log_warn(TAG, "NVS write failed, history not saved");
    }
    nvs_close(h);
}

/* Açılışta: son taramada görülen (silinmemiş) kodlar rapora yüklenir ki
 * bağlantı yokken de son durum görünsün. */
static void report_from_history(void)
{
    s_rep.count = 0;
    for (int i = 0; i < s_hist.count && s_rep.count < DTC_MAX; i++) {
        const dtc_hist_item_t *it = &s_hist.items[i];
        if (it->last_scan == s_hist.scan_no && !(it->kind & DTC_KIND_CLEARED)) {
            s_rep.codes[s_rep.count].code = it->code;
            s_rep.codes[s_rep.count].kind = it->kind & (DTC_KIND_STORED | DTC_KIND_PENDING);
            s_rep.count++;
        }
    }
    s_rep.scan_no = s_hist.scan_no;
    s_rep.from_flash = s_hist.scan_no > 0;
    s_rep.status = s_rep.from_flash ? DTC_ST_DONE : DTC_ST_IDLE;
    s_rep.km_mil = -1;
    s_rep.km_clear = -1;
}

/* ---------- Yanıt ayrıştırma ---------- */

/* Boşlukları atıp büyük harf hex dizisi üretir. */
static void compact_hex(const char *resp, char *out, size_t out_len)
{
    size_t n = 0;
    for (const char *p = resp; *p && n < out_len - 1; p++) {
        if (isxdigit((unsigned char)*p)) {
            out[n++] = (char)toupper((unsigned char)*p);
        }
    }
    out[n] = '\0';
}

static bool resp_is_error(const char *resp)
{
    return strstr(resp, "NO DATA") || strstr(resp, "UNABLE") ||
           strstr(resp, "ERROR") || strstr(resp, "STOPPED");
}

/* tok (örn. "4101") sonrasındaki n baytı okur. */
static bool read_bytes_after(const char *resp, const char *tok, uint8_t *out, int n)
{
    char hex[160];
    compact_hex(resp, hex, sizeof(hex));
    const char *p = strstr(hex, tok);
    if (!p) {
        return false;
    }
    p += strlen(tok);
    for (int i = 0; i < n; i++) {
        unsigned v = 0;
        if (strlen(p) < 2 || sscanf(p, "%2x", &v) != 1) {
            return false;
        }
        out[i] = (uint8_t)v;
        p += 2;
    }
    return true;
}

static void add_code(dtc_report_t *r, uint16_t code, uint8_t kind)
{
    if (code == 0) {
        return;
    }
    for (int i = 0; i < r->count; i++) {
        if (r->codes[i].code == code) {
            r->codes[i].kind |= kind;
            return;
        }
    }
    if (r->count < DTC_MAX) {
        r->codes[r->count].code = code;
        r->codes[r->count].kind = kind;
        r->count++;
    }
}

static int hex_frame_bytes(const char *tok, size_t len, uint8_t *out, int max)
{
    int n = 0;
    for (size_t i = 0; i + 1 < len && n < max; i += 2) {
        unsigned v = 0;
        char pair[3] = { tok[i], tok[i + 1], '\0' };
        if (sscanf(pair, "%2x", &v) != 1) {
            break;
        }
        out[n++] = (uint8_t)v;
    }
    return n;
}

/* Mode 03/07 yanıtı. ELM her ECU çerçevesini ayrı satırda verir; elm327.c
 * satırları tek boşlukla birleştirir (ATS0 → satır içinde boşluk yok).
 *  K-line (ISO 9141/14230): "43 AABB CCDD EEFF" sabit 7 bayt, 3 kod/çerçeve.
 *  CAN: "43 NN AABB ..." — NN kod sayısı.
 * ATS1 kullanan klonlarda her bayt ayrı token olur; o zaman tek akış kabul
 * edilip K-line için 7'lik çerçevelere bölünür. */
static void parse_dtc_resp(const char *resp, uint8_t reply, uint8_t kind, dtc_report_t *r)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", resp);
    const bool can = elm327_protocol_is_can();

    bool all_pairs = true;
    int tokens = 0;
    for (const char *p = buf; *p;) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != ' ') p++;
        tokens++;
        if (p - s != 2) {
            all_pairs = false;
        }
    }

    uint8_t b[96];
    if (all_pairs && tokens > 1) {
        char hex[200];
        compact_hex(buf, hex, sizeof(hex));
        int n = hex_frame_bytes(hex, strlen(hex), b, sizeof(b));
        int frame = can ? n : 7;
        for (int off = 0; off < n; off += frame) {
            int fl = (n - off < frame) ? n - off : frame;
            if (b[off] != reply) continue;
            int start = can ? 2 : 1;
            int max_codes = can && fl > 1 ? b[off + 1] : 255;
            for (int i = off + start; i + 1 < off + fl && max_codes-- > 0; i += 2) {
                add_code(r, (uint16_t)((b[i] << 8) | b[i + 1]), kind);
            }
        }
        return;
    }

    char *save = NULL;
    for (char *tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        int n = hex_frame_bytes(tok, strlen(tok), b, sizeof(b));
        if (n < 1 || b[0] != reply) {
            continue;
        }
        int start = can ? 2 : 1;
        int max_codes = (can && n > 1) ? b[1] : 255;
        for (int i = start; i + 1 < n && max_codes-- > 0; i += 2) {
            add_code(r, (uint16_t)((b[i] << 8) | b[i + 1]), kind);
        }
    }
}

static void parse_monitor(const char *resp, dtc_report_t *r)
{
    uint8_t b[4];
    if (!read_bytes_after(resp, "4101", b, 4)) {
        return;
    }
    r->monitor_valid = true;
    r->mil_on = (b[0] & 0x80) != 0;
    r->ecu_dtc_count = b[0] & 0x7F;

    /* Hazırlık monitörleri (benzinli: B bit3 = 0). Desteklenen bit=1,
     * tamamlanmamış bit=1. */
    uint8_t sup = 0, ok = 0;
    for (int i = 0; i < 3; i++) {
        if ((b[1] >> i) & 1) {
            sup++;
            if (!((b[1] >> (i + 4)) & 1)) ok++;
        }
    }
    if (!(b[1] & 0x08)) {
        for (int i = 0; i < 8; i++) {
            if ((b[2] >> i) & 1) {
                sup++;
                if (!((b[3] >> i) & 1)) ok++;
            }
        }
    }
    r->ready_total = sup;
    r->ready_ok = ok;
}

static void parse_ff_pid(const char *resp, uint8_t pid, dtc_freeze_t *ff)
{
    char tok[8];
    uint8_t b[3];
    snprintf(tok, sizeof(tok), "42%02X", pid);
    /* "42 PID FRAME A [B]" */
    int need = (pid == 0x0C) ? 3 : 2;
    if (!read_bytes_after(resp, tok, b, need)) {
        return;
    }
    uint8_t a = b[1];
    switch (pid) {
    case 0x0C: ff->rpm = ((a * 256.0f) + b[2]) / 4.0f; break;
    case 0x0D: ff->speed = a; break;
    case 0x05: ff->coolant = a - 40.0f; break;
    case 0x04: ff->load = a * 100.0f / 255.0f; break;
    case 0x06: ff->stft = (a - 128.0f) * 100.0f / 128.0f; break;
    case 0x07: ff->ltft = (a - 128.0f) * 100.0f / 128.0f; break;
    case 0x0B: ff->map = a; break;
    default: break;
    }
}

/* ---------- Plan ---------- */

static void plan_add(uint8_t step, uint8_t arg)
{
    if (s_plan_len < sizeof(s_plan) / sizeof(s_plan[0])) {
        s_plan[s_plan_len].step = step;
        s_plan[s_plan_len].arg = arg;
        s_plan_len++;
    }
}

static void plan_scan_steps(bool with_ff)
{
    plan_add(ST_MONITOR, 0);
    plan_add(ST_STORED, 0);
    plan_add(ST_PENDING, 0);
    if (with_ff) {
        plan_add(ST_FF_CODE, 0);
        for (size_t i = 0; i < sizeof(s_ff_pids); i++) {
            plan_add(ST_FF_PID, s_ff_pids[i]);
        }
        plan_add(ST_KM_MIL, 0);
    }
    plan_add(ST_KM_CLEAR, 0);
}

static void publish_progress(dtc_status_t st)
{
    lock();
    s_rep.status = st;
    s_rep.progress = s_plan_len ? (uint8_t)(s_plan_idx * 100 / s_plan_len) : 0;
    s_rep.seq++;
    unlock();
}

static void begin_job(job_t job, uint32_t now)
{
    s_job = job;
    s_active = true;
    s_inflight = false;
    s_plan_len = 0;
    s_plan_idx = 0;
    s_stored_ok = false;
    s_clear_acked = false;
    s_settle_until = 0;

    memset(&s_work, 0, sizeof(s_work));
    s_work.km_mil = -1;
    s_work.km_clear = -1;
    s_work.ff.rpm = s_work.ff.speed = s_work.ff.coolant = NAN;
    s_work.ff.load = s_work.ff.stft = s_work.ff.ltft = s_work.ff.map = NAN;

    switch (job) {
    case JOB_CLEAR:
        plan_add(ST_CLEAR, 0);
        plan_add(ST_SETTLE, 0);
        plan_scan_steps(false);
        publish_progress(DTC_ST_CLEARING);
        app_log_info(TAG, "Clearing DTCs (mode 04)");
        break;
    case JOB_MONITOR:
        plan_add(ST_MONITOR, 0);
        break;
    default:
        plan_scan_steps(true);
        publish_progress(DTC_ST_SCANNING);
        app_log_info(TAG, "DTC scan started");
        break;
    }
    (void)now;
}

static bool pid_known_unsupported(uint8_t pid)
{
    const vehicle_data_t *vd = vehicle_data_get();
    bool any = vd->supported_pids[0] || vd->supported_pids[1];
    return any && !vehicle_data_is_pid_supported(pid);
}

static bool step_skipped(const plan_item_t *it)
{
    switch (it->step) {
    case ST_PENDING:  return s_pending_unsupported;
    case ST_FF_CODE:  return !(s_work.mil_on || s_work.count > 0);
    case ST_FF_PID:   return !s_work.ff.valid || pid_known_unsupported(it->arg);
    case ST_KM_MIL:   return !vehicle_data_is_pid_supported(0x21);
    case ST_KM_CLEAR: return !vehicle_data_is_pid_supported(0x31);
    default:          return false;
    }
}

static void step_cb(const char *resp, void *user_data)
{
    if ((uint32_t)(uintptr_t)user_data != s_tx_seq) {
        return;   /* geç gelen eski yanıt */
    }
    snprintf(s_resp, sizeof(s_resp), "%s", resp ? resp : "");
    s_cb_seq = s_tx_seq;
}

static bool send_step(const plan_item_t *it, uint32_t now)
{
    char cmd[12];
    uint32_t tmo = TMO_FF_MS;
    switch (it->step) {
    case ST_MONITOR:  snprintf(cmd, sizeof(cmd), "0101"); tmo = TMO_MONITOR_MS; break;
    case ST_STORED:   snprintf(cmd, sizeof(cmd), "03");   tmo = TMO_STORED_MS;  break;
    case ST_PENDING:  snprintf(cmd, sizeof(cmd), "07");   tmo = TMO_PENDING_MS; break;
    case ST_FF_CODE:  snprintf(cmd, sizeof(cmd), "020200"); break;
    case ST_FF_PID:   snprintf(cmd, sizeof(cmd), "02%02X00", it->arg); break;
    case ST_KM_MIL:   snprintf(cmd, sizeof(cmd), "0121"); break;
    case ST_KM_CLEAR: snprintf(cmd, sizeof(cmd), "0131"); break;
    case ST_CLEAR:    snprintf(cmd, sizeof(cmd), "04");   tmo = TMO_CLEAR_MS;   break;
    default: return false;
    }
    s_tx_seq++;
    s_resp[0] = '\0';
    if (!elm327_send_cmd_prio(cmd, step_cb, (void *)(uintptr_t)s_tx_seq, tmo, true)) {
        return false;
    }
    s_inflight = true;
    s_sent_at = now;
    s_timeout = tmo;
    return true;
}

/* resp == NULL: timeout. */
static void handle_step(const plan_item_t *it, const char *resp)
{
    bool err = !resp || resp_is_error(resp);
    switch (it->step) {
    case ST_MONITOR:
        if (!err) parse_monitor(resp, &s_work);
        break;
    case ST_STORED:
        /* K-line ECU kod yokken "43 00.." ya da NO DATA döner; ikisi de geçerli. */
        if (resp) {
            s_stored_ok = true;
            if (!err) parse_dtc_resp(resp, 0x43, DTC_KIND_STORED, &s_work);
        }
        break;
    case ST_PENDING:
        if (!resp) {
            s_pending_unsupported = true;
            app_log_info(TAG, "Mode 07 not answered — skipped for this link");
        } else if (!err) {
            parse_dtc_resp(resp, 0x47, DTC_KIND_PENDING, &s_work);
        }
        break;
    case ST_FF_CODE: {
        uint8_t b[3];
        if (!err && read_bytes_after(resp, "4202", b, 3)) {
            uint16_t code = (uint16_t)((b[1] << 8) | b[2]);
            s_work.ff.valid = code != 0;
            s_work.ff.code = code;
        }
        break;
    }
    case ST_FF_PID:
        if (!err) parse_ff_pid(resp, it->arg, &s_work.ff);
        break;
    case ST_KM_MIL:
    case ST_KM_CLEAR: {
        uint8_t b[2];
        const char *tok = it->step == ST_KM_MIL ? "4121" : "4131";
        if (!err && read_bytes_after(resp, tok, b, 2)) {
            int32_t km = (b[0] << 8) | b[1];
            if (it->step == ST_KM_MIL) s_work.km_mil = km; else s_work.km_clear = km;
        }
        break;
    }
    case ST_CLEAR:
        s_clear_acked = resp && strstr(resp, "44") != NULL;
        app_log_info(TAG, "Mode 04 response: %s", resp ? resp : "(timeout)");
        break;
    default:
        break;
    }
}

/* ---------- Bitiş ---------- */

static int hist_find(uint16_t code)
{
    for (int i = 0; i < s_hist.count; i++) {
        if (s_hist.items[i].code == code) return i;
    }
    return -1;
}

static int hist_alloc(void)
{
    if (s_hist.count < DTC_HIST_MAX) {
        return s_hist.count++;
    }
    int oldest = 0;
    for (int i = 1; i < s_hist.count; i++) {
        if (s_hist.items[i].last_scan < s_hist.items[oldest].last_scan) oldest = i;
    }
    return oldest;
}

/* Mutex altında çağrılır. true: flash'a yazılmalı. */
static bool hist_merge(const dtc_report_t *r, bool cleared)
{
    bool prev_had_codes = false;
    for (int i = 0; i < s_hist.count; i++) {
        const dtc_hist_item_t *it = &s_hist.items[i];
        if (it->last_scan == s_hist.scan_no && !(it->kind & DTC_KIND_CLEARED)) {
            prev_had_codes = true;
        }
    }
    if (r->count == 0 && !cleared && !prev_had_codes) {
        return false;   /* temiz → temiz: yazmaya gerek yok (flash ömrü) */
    }

    if (cleared) {
        for (int i = 0; i < s_hist.count; i++) {
            s_hist.items[i].kind |= DTC_KIND_CLEARED;
        }
        s_hist.clear_no++;
        s_hist.last_clear_scan = s_hist.scan_no + 1;
    }
    s_hist.scan_no++;
    for (int i = 0; i < r->count; i++) {
        int idx = hist_find(r->codes[i].code);
        if (idx < 0) {
            idx = hist_alloc();
            s_hist.items[idx].code = r->codes[i].code;
            s_hist.items[idx].hits = 0;
            s_hist.items[idx].first_scan = s_hist.scan_no;
        }
        dtc_hist_item_t *it = &s_hist.items[idx];
        it->kind = r->codes[i].kind;
        if (it->hits < 255) it->hits++;
        it->last_scan = s_hist.scan_no;
    }
    return true;
}

static void log_report(const dtc_report_t *r)
{
    char line[96];
    int n = 0;
    for (int i = 0; i < r->count && n < (int)sizeof(line) - 10; i++) {
        char c[6];
        obd_dtc_format(r->codes[i].code, c);
        n += snprintf(line + n, sizeof(line) - n,
                      (r->codes[i].kind & DTC_KIND_STORED) ? " %s" : " (%s)", c);
    }
    line[n] = '\0';
    app_log_info(TAG, "DTC #%u: MIL %s, ecu=%u, ready %u/%u, codes:%s",
                 r->scan_no, r->mil_on ? "ON" : "off", r->ecu_dtc_count,
                 r->ready_ok, r->ready_total, n ? line : " none");
}

static void finish_job(uint32_t now)
{
    s_active = false;

    if (s_job == JOB_MONITOR) {
        s_monitor_last = now;
        if (!s_work.monitor_valid) {
            return;
        }
        lock();
        bool changed = s_work.mil_on != s_rep.mil_on ||
                       s_work.ecu_dtc_count != s_rep.ecu_dtc_count;
        unlock();
        if (changed && !s_req) {
            app_log_info(TAG, "MIL/DTC count changed — full scan");
            s_req = JOB_SCAN;
        }
        return;
    }

    /* 03 yanıtı yoksa kod listesi bilinmiyor: "temiz" deme, hata göster. */
    bool ok = s_stored_ok;
    bool cleared = s_job == JOB_CLEAR && ok && s_work.count == 0 && !s_work.mil_on;
    bool persist = false;

    lock();
    if (ok) {
        persist = hist_merge(&s_work, cleared);
        s_work.scan_no = s_hist.scan_no;
        s_work.seq = s_rep.seq + 1;
        if (s_job == JOB_CLEAR) {
            s_work.status = cleared ? DTC_ST_CLEARED : DTC_ST_CLEAR_FAIL;
        } else {
            s_work.status = DTC_ST_DONE;
        }
        s_work.progress = 100;
        s_rep = s_work;
    } else {
        s_rep.status = DTC_ST_ERROR;
        s_rep.progress = 0;
        s_rep.seq++;
    }
    unlock();

    s_monitor_last = now;
    if (!ok) {
        app_log_warn(TAG, "DTC %s failed: ECU did not answer",
                     s_job == JOB_CLEAR ? "clear" : "scan");
        return;
    }
    if (s_job == JOB_CLEAR) {
        app_log_info(TAG, "DTC clear %s (ack=%d)", cleared ? "verified" : "NOT verified",
                     s_clear_acked);
    }
    log_report(&s_work);
    if (persist) {
        hist_persist();
    }
}

/* ---------- Genel API ---------- */

void obd_dtc_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    hist_load();
    report_from_history();
    app_log_info(TAG, "History: %u codes, %u scans, %u clears",
                 s_hist.count, s_hist.scan_no, s_hist.clear_no);
}

bool obd_dtc_request_scan(void)
{
    if (!s_link_ready || s_active || s_req) {
        return false;
    }
    s_req = JOB_SCAN;
    return true;
}

bool obd_dtc_request_clear(void)
{
    if (!s_link_ready || s_req || (s_active && s_job != JOB_MONITOR)) {
        return false;
    }
    s_req = JOB_CLEAR;
    return true;
}

void obd_dtc_erase_history(void)
{
    lock();
    memset(&s_hist, 0, sizeof(s_hist));
    s_hist.version = HIST_VERSION;
    if (s_rep.from_flash) {
        s_rep.count = 0;
        s_rep.from_flash = false;
        s_rep.status = DTC_ST_IDLE;
    }
    s_rep.scan_no = 0;
    s_rep.seq++;
    unlock();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
    app_log_info(TAG, "DTC history erased");
}

void obd_dtc_get_report(dtc_report_t *out)
{
    lock();
    *out = s_rep;
    unlock();
}

void obd_dtc_get_history(dtc_hist_t *out)
{
    lock();
    *out = s_hist;
    unlock();
}

uint32_t obd_dtc_seq(void)
{
    return s_rep.seq;
}

uint8_t obd_dtc_active_count(bool *mil_on)
{
    lock();
    uint8_t n = 0;
    for (int i = 0; i < s_rep.count; i++) {
        if (s_rep.codes[i].kind & DTC_KIND_STORED) n++;
    }
    bool mil = s_rep.monitor_valid && s_rep.mil_on;
    unlock();
    if (mil_on) *mil_on = mil;
    return (mil && n == 0) ? 1 : n;
}

void obd_dtc_on_link_ready(void)
{
    if (s_link_ready) {
        return;
    }
    s_link_ready = true;
    s_ready_at = now_ms();
    s_monitor_last = s_ready_at;
}

void obd_dtc_on_disconnect(void)
{
    if (!s_link_ready && !s_active) {
        return;
    }
    bool was_active = s_active && s_job != JOB_MONITOR;
    s_link_ready = false;
    s_auto_done = false;
    s_active = false;
    s_inflight = false;
    s_req = 0;
    s_pending_unsupported = false;
    s_tx_seq++;    /* uçuştaki yanıtı geçersiz kıl */
    lock();
    s_rep.monitor_valid = false;
    if (was_active) {
        s_rep.status = DTC_ST_ERROR;
        s_rep.progress = 0;
    }
    s_rep.seq++;
    unlock();
}

bool obd_dtc_service(uint32_t now)
{
    if (!s_link_ready) {
        return false;
    }

    if (!s_active) {
        uint8_t req = s_req;
        if (req) {
            s_req = 0;
            s_auto_done = true;
            begin_job((job_t)req, now);
        } else if (!s_auto_done && now - s_ready_at >= AUTO_SCAN_DELAY_MS) {
            s_auto_done = true;
            begin_job(JOB_SCAN, now);
        } else if (s_auto_done && now - s_monitor_last >= MONITOR_PERIOD_MS) {
            begin_job(JOB_MONITOR, now);
        } else {
            return false;
        }
    }

    if (s_inflight) {
        const plan_item_t *it = &s_plan[s_plan_idx];
        if (s_cb_seq == s_tx_seq) {
            s_inflight = false;
            handle_step(it, s_resp);
        } else if (now - s_sent_at > s_timeout + WATCHDOG_EXTRA_MS) {
            s_inflight = false;
            handle_step(it, NULL);
        } else {
            return true;
        }
        if (it->step == ST_CLEAR) {
            s_settle_until = now + CLEAR_SETTLE_MS;
        }
        s_plan_idx++;
        if (s_job != JOB_MONITOR) {
            publish_progress(s_job == JOB_CLEAR ? DTC_ST_CLEARING : DTC_ST_SCANNING);
        }
    }

    while (s_plan_idx < s_plan_len &&
           (s_plan[s_plan_idx].step == ST_SETTLE || step_skipped(&s_plan[s_plan_idx]))) {
        if (s_plan[s_plan_idx].step == ST_SETTLE && (int32_t)(s_settle_until - now) > 0) {
            return true;
        }
        s_plan_idx++;
    }

    if (s_plan_idx >= s_plan_len) {
        finish_job(now);
        return false;
    }

    /* Önceki PID komutları bitsin: K-line'da tek komut uçuşta olmalı. */
    if (elm327_queue_depth() > 0 || elm327_is_busy()) {
        return true;
    }
    send_step(&s_plan[s_plan_idx], now);
    return true;
}
