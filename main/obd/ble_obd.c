#include "ble_obd.h"
#include "vehicle_data.h"
#include "app_log.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "ble_obd";
static const char *NVS_NS = "obd_ble";
static const char *NVS_KEY_ADDR = "addr";

#define SCAN_DURATION_SEC  8
#define MAX_NAME_LEN       32
#define RECONNECT_MS       1500
#define CONNECT_TIMEOUT_MS 15000
#define GATT_DISCOVERY_TIMEOUT_MS 6000  /* servis keşfi asılı kalırsa kurtar */
#define DIRECT_FAIL_MAX    3            /* bu kadar direkt hatadan sonra scan'e düş (adres korunur) */
#define BACKOFF_MAX_MS     10000        /* scan bulunamayınca üst sınır bekleme */

static ble_obd_rx_cb_t s_rx_cb;
static ble_obd_state_t s_state = BLE_OBD_DISCONNECTED;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_write_handle = 0;
static uint16_t s_notify_handle = 0;
static uint8_t s_saved_addr[6];
static uint8_t s_saved_addr_type;   /* BLE_ADDR_PUBLIC or BLE_ADDR_RANDOM */
static bool s_has_saved_addr;
static bool s_scan_active;
static bool s_connecting_saved;
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_connect_timer;
static int64_t s_connect_start_us; /* bağlantı denemesi başlangıcı (teşhis) */
static int s_direct_fail_count;   /* ardışık direkt bağlantı hataları */
static bool s_prefer_scan;        /* geçici olarak scan'i tercih et (adres silinmez) */
static int s_scan_fail_count;     /* ardışık "adaptör bulunamadı" — backoff için */
static bool s_gatt_phase;         /* watchdog GATT keşfi mi bağlantı mı bekliyor */

static const char *s_name_filters[] = {
    "OBD", "OBDII", "OBD2", "ELM", "VLINK", "IOS-Vlink", "Android-Vlink", "vlink", "Car",
};

static const struct {
    uint16_t svc;
    uint16_t notify;
    uint16_t write;
} s_uuid_profiles[] = {
    { 0xFFF0, 0xFFF1, 0xFFF2 },
    { 0xFFE0, 0xFFE1, 0xFFE1 },
    { 0xFFE0, 0xFFE1, 0xFFE2 },
};

static int ble_obd_gap_event(struct ble_gap_event *event, void *arg);
static int ble_obd_on_disc_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                               const struct ble_gatt_chr *chr, void *arg);
static int ble_obd_on_disc_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                               const struct ble_gatt_svc *svc, void *arg);
static void ble_obd_on_sync(void);
static void ble_obd_on_reset(int reason);
static void ble_obd_host_task(void *param);
static void schedule_reconnect(uint32_t delay_ms);
static void cancel_connect_watchdog(void);
static void start_connect_watchdog(uint32_t timeout_ms);
static bool addr_is_valid(const uint8_t *addr);
static void clear_saved_addr(void);
static void try_auto_connect(void);
static int connect_peer(const ble_addr_t *addr, const char *name);
static void start_scan(void);

static char to_lower(char c)
{
    return (char)((c >= 'A' && c <= 'Z') ? (c + 32) : c);
}

static bool str_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return false;
    }
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > hlen) {
        return nlen == 0;
    }
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (to_lower(haystack[i + j]) != to_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static bool name_matches(const char *name)
{
    if (!name || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_name_filters) / sizeof(s_name_filters[0]); i++) {
        if (str_contains_ci(name, s_name_filters[i])) {
            return true;
        }
    }
    return false;
}

static bool addr_matches_saved(const uint8_t *addr)
{
    return s_has_saved_addr && memcmp(addr, s_saved_addr, 6) == 0;
}

static void save_addr(const uint8_t *addr, uint8_t addr_type)
{
    if (!addr_is_valid(addr)) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        uint8_t blob[7];
        memcpy(blob, addr, 6);
        blob[6] = addr_type;
        nvs_set_blob(h, NVS_KEY_ADDR, blob, 7);
        nvs_commit(h);
        nvs_close(h);
        memcpy(s_saved_addr, addr, 6);
        s_saved_addr_type = addr_type;
        s_has_saved_addr = true;
    }
}

static void clear_saved_addr(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_ADDR);
        nvs_commit(h);
        nvs_close(h);
    }
    memset(s_saved_addr, 0, sizeof(s_saved_addr));
    s_saved_addr_type = BLE_ADDR_PUBLIC;
    s_has_saved_addr = false;
}

static bool addr_is_valid(const uint8_t *addr)
{
    if (!addr) {
        return false;
    }
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (addr[i] != 0x00) {
            all_zero = false;
        }
        if (addr[i] != 0xFF) {
            all_ff = false;
        }
    }
    return !all_zero && !all_ff;
}

static void schedule_reconnect(uint32_t delay_ms)
{
    if (!vehicle_data_get()->auto_connect || s_reconnect_timer == NULL) {
        return;
    }
    if (ble_obd_is_connected() || s_scan_active) {
        return;
    }
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
}

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (ble_obd_is_connected() || s_scan_active) {
        return;
    }
    try_auto_connect();
}

static void connect_timeout_cb(void *arg)
{
    (void)arg;
    if (s_state != BLE_OBD_CONNECTING) {
        return;
    }
    /* GATT keşfi de asılabilir; her iki fazı da burada kurtarıyoruz.
     * Kayıtlı adresi ASLA silmeyiz — adaptör sadece o an uyanmamış olabilir.
     * Bunun yerine ardışık hataları sayıp bir süre scan'i tercih ederiz. */
    app_log_warn(TAG, "Connect/GATT timeout (phase=%s), retrying",
                 s_gatt_phase ? "gatt" : "conn");
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    } else {
        ble_gap_conn_cancel();
    }
    s_state = BLE_OBD_DISCONNECTED;
    s_gatt_phase = false;
    s_connecting_saved = false;
    if (++s_direct_fail_count >= DIRECT_FAIL_MAX) {
        s_prefer_scan = true;   /* adres korunur; scan adaptörün gerçekten yayında olduğunu doğrular */
        s_direct_fail_count = 0;
    }
    vehicle_data_set_state(OBD_STATE_DISCONNECTED, "Reconnecting...");
    schedule_reconnect(RECONNECT_MS);
}

static void cancel_connect_watchdog(void)
{
    if (s_connect_timer) {
        esp_timer_stop(s_connect_timer);
    }
}

static void start_connect_watchdog(uint32_t timeout_ms)
{
    if (!s_connect_timer) {
        return;
    }
    esp_timer_stop(s_connect_timer);
    esp_timer_start_once(s_connect_timer, (uint64_t)timeout_ms * 1000ULL);
}

static void try_auto_connect(void)
{
    if (!vehicle_data_get()->auto_connect) {
        return;
    }
    if (ble_obd_is_connected() || s_scan_active || s_state == BLE_OBD_CONNECTING) {
        return;
    }
    /* Kayıtlı adres varsa doğrudan bağlan; ancak arka arkaya hata sonrası
     * geçici olarak scan'i tercih et (adres silinmez, scan doğrular). */
    if (s_has_saved_addr && addr_is_valid(s_saved_addr) && !s_prefer_scan) {
        ble_addr_t addr = {0};
        addr.type = s_saved_addr_type;
        memcpy(addr.val, s_saved_addr, 6);
        s_connecting_saved = true;
        if (connect_peer(&addr, "Kayitli adaptor") == 0) {
            return;
        }
        /* If random type failed, try public */
        if (addr.type == BLE_ADDR_RANDOM) {
            addr.type = BLE_ADDR_PUBLIC;
            connect_peer(&addr, "Kayitli adaptor");
            return;
        }
    } else {
        start_scan();
    }
}

static void load_saved_addr(void)
{
    nvs_handle_t h;
    size_t len = 7;
    uint8_t blob[7] = {0};
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        esp_err_t err = nvs_get_blob(h, NVS_KEY_ADDR, blob, &len);
        if (err == ESP_OK && len >= 6) {
            memcpy(s_saved_addr, blob, 6);
            s_saved_addr_type = (len >= 7) ? blob[6] : BLE_ADDR_RANDOM;
            s_has_saved_addr = true;
        }
        nvs_close(h);
    }
}

static void addr_to_str(const uint8_t *addr, char *out, size_t out_len)
{
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static uint16_t s_disc_svc_start;
static uint16_t s_disc_svc_end;
static uint16_t s_target_svc_uuid;

static bool uuid16_match(const ble_uuid_any_t *uuid, uint16_t target)
{
    return uuid && uuid->u.type == BLE_UUID_TYPE_16 && uuid->u16.value == target;
}

static int subscribe_notify(uint16_t conn_handle)
{
    if (!s_notify_handle) {
        return BLE_HS_EINVAL;
    }
    uint8_t buf[2] = {0x01, 0x00};
    return ble_gattc_write_flat(conn_handle, s_notify_handle + 1, buf, sizeof(buf), NULL, NULL);
}

static void on_gatt_ready(uint16_t conn_handle)
{
    if (s_notify_handle && s_write_handle) {
        cancel_connect_watchdog();
        s_gatt_phase = false;
        s_direct_fail_count = 0;
        s_scan_fail_count = 0;
        s_prefer_scan = false;   /* başarılı: bir sonraki sefer tekrar direkt bağlan */
        int32_t ms = s_connect_start_us ?
                     (int32_t)((esp_timer_get_time() - s_connect_start_us) / 1000) : -1;
        app_log_info(TAG, "GATT ready notify=0x%04X write=0x%04X (connect took %ld ms)",
                     s_notify_handle, s_write_handle, (long)ms);
        s_state = BLE_OBD_CONNECTED;
    } else {
        app_log_error(TAG, "Required characteristics not found");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int ble_obd_on_disc_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                               const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == BLE_HS_EDONE) {
        if (s_notify_handle) {
            subscribe_notify(conn_handle);
        }
        on_gatt_ready(conn_handle);
        return 0;
    }
    if (error->status != 0 || !chr) {
        return 0;
    }

    for (size_t p = 0; p < sizeof(s_uuid_profiles) / sizeof(s_uuid_profiles[0]); p++) {
        if (s_target_svc_uuid != s_uuid_profiles[p].svc) {
            continue;
        }
        if (uuid16_match(&chr->uuid, s_uuid_profiles[p].notify)) {
            s_notify_handle = chr->val_handle;
        }
        if (uuid16_match(&chr->uuid, s_uuid_profiles[p].write)) {
            s_write_handle = chr->val_handle;
        }
    }
    return 0;
}

static int ble_obd_on_disc_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                               const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status == BLE_HS_EDONE) {
        if (s_target_svc_uuid == 0) {
            app_log_error(TAG, "OBD service not found");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    if (error->status != 0 || !svc) {
        return 0;
    }

    for (size_t p = 0; p < sizeof(s_uuid_profiles) / sizeof(s_uuid_profiles[0]); p++) {
        if (uuid16_match(&svc->uuid, s_uuid_profiles[p].svc)) {
            s_target_svc_uuid = s_uuid_profiles[p].svc;
            s_disc_svc_start = svc->start_handle;
            s_disc_svc_end = svc->end_handle;
            s_notify_handle = 0;
            s_write_handle = 0;
            ble_gattc_disc_all_chrs(conn_handle, s_disc_svc_start, s_disc_svc_end,
                                    ble_obd_on_disc_chr, NULL);
            return 0;
        }
    }
    return 0;
}

static void start_gatt_discovery(uint16_t conn_handle)
{
    s_notify_handle = 0;
    s_write_handle = 0;
    s_target_svc_uuid = 0;
    ble_gattc_disc_all_svcs(conn_handle, ble_obd_on_disc_svc, NULL);
}

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .passive = 0,
        .filter_duplicates = 1,
        .itvl = 0x0010,               /* 10ms */
        .window = 0x0010,             /* 10ms */
    };

    s_state = BLE_OBD_SCANNING;
    s_scan_active = true;
    vehicle_data_set_state(OBD_STATE_SCANNING, "Scanning for adapter...");
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, SCAN_DURATION_SEC * 1000, &params,
                 ble_obd_gap_event, NULL);
}

static int connect_peer(const ble_addr_t *addr, const char *name)
{
    struct ble_gap_conn_params conn_params = {
        .scan_itvl = 0x0010,           /* 10ms */
        .scan_window = 0x0010,         /* 10ms */
        .itvl_min = 6,                 /* 7.5ms — min allowed by BLE spec */
        .itvl_max = 12,                /* 15ms — tight for max throughput */
        .latency = 0,
        .supervision_timeout = 300,    /* 3s — faster disconnect detection */
        .min_ce_len = 0,
        .max_ce_len = 0,
    };

    s_state = BLE_OBD_CONNECTING;
    s_connect_start_us = esp_timer_get_time();
    vehicle_data_set_state(OBD_STATE_CONNECTING, "Connecting...");

    char addr_str[18];
    addr_to_str(addr->val, addr_str, sizeof(addr_str));
    vehicle_data_set_adapter(name ? name : "OBD", addr_str);

    s_gatt_phase = false;
    start_connect_watchdog(CONNECT_TIMEOUT_MS);
    return ble_gap_connect(BLE_OWN_ADDR_PUBLIC, addr, 30000, &conn_params,
                           ble_obd_gap_event, NULL);
}

static int ble_obd_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }

        char name[MAX_NAME_LEN] = {0};
        if (fields.name && fields.name_len > 0) {
            size_t n = fields.name_len < MAX_NAME_LEN - 1 ? fields.name_len : MAX_NAME_LEN - 1;
            memcpy(name, fields.name, n);
        }

        bool match = name_matches(name) || addr_matches_saved(event->disc.addr.val);
        if (!match && fields.uuids16) {
            for (int i = 0; i < fields.num_uuids16; i++) {
                uint16_t u = fields.uuids16[i].value;
                if (u == 0xFFF0 || u == 0xFFE0) {
                    match = true;
                    break;
                }
            }
        }

        if (match) {
            app_log_info(TAG, "Found OBD device: %s", name[0] ? name : "(no name)");
            s_scan_fail_count = 0;
            s_prefer_scan = false;   /* adaptör yayında; sonraki sefer direkt bağlanılabilir */
            s_connecting_saved = false;
            ble_gap_disc_cancel();
            s_scan_active = false;
            connect_peer(&event->disc.addr, name);
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scan_active = false;
        if (s_state == BLE_OBD_SCANNING) {
            s_state = BLE_OBD_DISCONNECTED;
            vehicle_data_set_state(OBD_STATE_DISCONNECTED, "Adapter not found");
            /* Bulunamadıkça artan (exponential-ish) backoff, üst sınır 10 sn. */
            uint32_t delay = RECONNECT_MS * (1u << (s_scan_fail_count < 3 ? s_scan_fail_count : 3));
            if (delay > BACKOFF_MAX_MS) {
                delay = BACKOFF_MAX_MS;
            }
            if (s_scan_fail_count < 8) {
                s_scan_fail_count++;
            }
            schedule_reconnect(delay);
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_state = BLE_OBD_CONNECTING;
            s_connecting_saved = false;
            ble_gattc_exchange_mtu(s_conn_handle, NULL, NULL);

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(s_conn_handle, &desc) == 0) {
                save_addr(desc.peer_ota_addr.val, desc.peer_ota_addr.type);
            }
            /* Watchdog'u GATT keşfi fazı için yeniden başlat; keşif asılırsa
             * connect_timeout_cb terminate + reconnect yapar. */
            s_gatt_phase = true;
            start_connect_watchdog(GATT_DISCOVERY_TIMEOUT_MS);
            start_gatt_discovery(s_conn_handle);
            app_log_info(TAG, "Connected, discovering GATT");
        } else {
            cancel_connect_watchdog();
            s_state = BLE_OBD_DISCONNECTED;
            vehicle_data_set_state(OBD_STATE_ERROR, "Connection failed");
            app_log_error(TAG, "Connect failed status=%d", event->connect.status);
            s_connecting_saved = false;
            if (++s_direct_fail_count >= DIRECT_FAIL_MAX) {
                s_prefer_scan = true;   /* adres korunur */
                s_direct_fail_count = 0;
            }
            schedule_reconnect(RECONNECT_MS);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        cancel_connect_watchdog();
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_write_handle = 0;
        s_notify_handle = 0;
        s_state = BLE_OBD_DISCONNECTED;
        s_connecting_saved = false;
        vehicle_data_set_state(OBD_STATE_DISCONNECTED, "Disconnected");
        app_log_warn(TAG, "BLE disconnected");
        schedule_reconnect(RECONNECT_MS);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (s_rx_cb) {
            s_rx_cb(event->notify_rx.om->om_data, event->notify_rx.om->om_len);
        }
        break;
    default:
        break;
    }
    return 0;
}

static void ble_obd_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

static void ble_obd_on_sync(void)
{
    ble_svc_gap_device_name_set("OBD-Dashboard");
    load_saved_addr();

    if (s_has_saved_addr && !addr_is_valid(s_saved_addr)) {
        app_log_warn(TAG, "Invalid saved BLE address, clearing");
        clear_saved_addr();
    }

    try_auto_connect();
}

static void ble_obd_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_obd_init(void)
{
    const esp_timer_create_args_t reconnect_args = {
        .callback = reconnect_timer_cb,
        .name = "ble_reconn",
    };
    const esp_timer_create_args_t connect_args = {
        .callback = connect_timeout_cb,
        .name = "ble_conn_to",
    };
    esp_timer_create(&reconnect_args, &s_reconnect_timer);
    esp_timer_create(&connect_args, &s_connect_timer);

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed");
        return;
    }

    ble_hs_cfg.sync_cb = ble_obd_on_sync;
    ble_hs_cfg.reset_cb = ble_obd_on_reset;

    nimble_port_freertos_init(ble_obd_host_task);
}

void ble_obd_start(void)
{
    /* sync_cb triggers scan/connect */
}

void ble_obd_stop(void)
{
    if (s_scan_active) {
        ble_gap_disc_cancel();
    }
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void ble_obd_scan(void)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    start_scan();
}

void ble_obd_connect_saved(void)
{
    if (s_has_saved_addr) {
        ble_addr_t addr = {0};
        addr.type = BLE_ADDR_PUBLIC;
        memcpy(addr.val, s_saved_addr, 6);
        connect_peer(&addr, "Kayitli adaptor");
    }
}

bool ble_obd_send(const uint8_t *data, size_t len)
{
    if (!ble_obd_is_connected() || !s_write_handle) {
        return false;
    }
    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, s_write_handle, data, len);
    return rc == 0;
}

bool ble_obd_is_connected(void)
{
    return s_state == BLE_OBD_CONNECTED && s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

ble_obd_state_t ble_obd_get_state(void)
{
    return s_state;
}

void ble_obd_set_rx_callback(ble_obd_rx_cb_t cb)
{
    s_rx_cb = cb;
}
