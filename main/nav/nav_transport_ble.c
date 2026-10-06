#include "nav_transport.h"
#include "app_log.h"

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>

/* NAV modunda ESP32 BLE çevre birimidir (peripheral): "AURA" adıyla
 * reklam yapar, telefondaki köprü uygulaması bağlanır.
 *
 *   Servis  7c6a0001-2f4b-4b8e-9d3a-5e1f0c2a9b10
 *   RX      7c6a0002-…  telefon → ESP (write / write without response)
 *   TX      7c6a0003-…  ESP → telefon (notify)
 *   MAP     7c6a0004-…  telefon → ESP harita JPEG parçaları (onaylı write: kayıpsız)
 *
 * Her yazma tek bir mesajdır (uzun yazmalar NimBLE'da birleştirilir).
 * NimBLE yığını yalnızca NAV modunda açıktır; OBD'nin ble_obd'si o sırada
 * kapalıdır (app_mode sırayı garanti eder). */

static const char *TAG = "nav_ble";

#define NAV_DEVICE_NAME "AURA"
#define NAV_RX_MAX      512

#define NAV_UUID(n) BLE_UUID128_INIT(0x10, 0x9b, 0x2a, 0x0c, 0x1f, 0x5e, 0x3a, 0x9d, \
                                     0x8e, 0x4b, 0x4b, 0x2f, (n), 0x00, 0x6a, 0x7c)

static const ble_uuid128_t s_svc_uuid = NAV_UUID(0x01);
static const ble_uuid128_t s_rx_uuid  = NAV_UUID(0x02);
static const ble_uuid128_t s_tx_uuid  = NAV_UUID(0x03);
static const ble_uuid128_t s_map_uuid = NAV_UUID(0x04);

static nav_transport_rx_cb_t   s_rx_cb;
static nav_transport_link_cb_t s_link_cb;
static volatile bool s_running;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_handle;
static bool     s_tx_subscribed;
static uint8_t  s_own_addr_type;
static uint8_t  s_rx_buf[NAV_RX_MAX + 1];

static void start_advertising(void);

static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, NAV_RX_MAX, &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    s_rx_buf[len] = '\0';
    if (s_rx_cb && len) {
        s_rx_cb((int)(intptr_t)arg, s_rx_buf, len);
    }
    return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = chr_access_cb,
                .arg = (void *)(intptr_t)NAV_CH_MSG,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_map_uuid.u,
                .access_cb = chr_access_cb,
                .arg = (void *)(intptr_t)NAV_CH_BULK,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = chr_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void set_link(bool up)
{
    if (s_link_cb) {
        s_link_cb(up);
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        /* IDF 5.3 NimBLE çevre biriminde status, uzak özellik okumasının
         * sonucudur; bağlantı açıkken de sıfır olmayabilir (ör. Windows PC).
         * Belirleyici olan bağlantının gerçekten var olması. */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
            s_conn = event->connect.conn_handle;
            s_tx_subscribed = false;
            app_log_info(TAG, "Phone connected (status=%d)", event->connect.status);
            set_link(true);
        } else if (s_running) {
            start_advertising();
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        app_log_warn(TAG, "Phone disconnected, reason=%d", event->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_tx_subscribed = false;
        set_link(false);
        if (s_running) {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_running && s_conn == BLE_HS_CONN_HANDLE_NONE) {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            s_tx_subscribed = event->subscribe.cur_notify;
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", event->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)NAV_DEVICE_NAME;
    fields.name_len = strlen(NAV_DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.uuids128 = &s_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        app_log_error(TAG, "adv fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        app_log_error(TAG, "adv start rc=%d", rc);
    }
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        app_log_error(TAG, "No BLE address");
        return;
    }
    app_log_info(TAG, "Advertising as %s", NAV_DEVICE_NAME);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool ble_start(nav_transport_rx_cb_t rx, nav_transport_link_cb_t link)
{
    if (s_running) {
        return true;
    }
    s_rx_cb = rx;
    s_link_cb = link;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        app_log_error(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return false;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_svcs);
    }
    if (rc != 0) {
        app_log_error(TAG, "GATT register rc=%d", rc);
        nimble_port_deinit();
        return false;
    }
    ble_svc_gap_device_name_set(NAV_DEVICE_NAME);

    s_running = true;
    nimble_port_freertos_init(host_task);
    return true;
}

static void ble_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;   /* olay yolları artık reklamı yeniden başlatmaz */
    nimble_port_stop();  /* bağlantıyı/reklamı kapatır, host görevi biter */
    nimble_port_deinit();
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_tx_subscribed = false;
    set_link(false);
    app_log_info(TAG, "BLE stack stopped");
}

static bool ble_send(const uint8_t *data, size_t len)
{
    if (!s_running || s_conn == BLE_HS_CONN_HANDLE_NONE || !s_tx_subscribed) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return false;
    }
    return ble_gatts_notify_custom(s_conn, s_tx_handle, om) == 0;
}

const nav_transport_t nav_transport_ble = {
    .name = "ble",
    .start = ble_start,
    .stop = ble_stop,
    .send = ble_send,
};
