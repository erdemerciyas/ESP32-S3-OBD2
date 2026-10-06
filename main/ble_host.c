#include "ble_host.h"
#include "app_log.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_host";

#define MAX_CLIENTS 2

static const struct ble_gatt_svc_def *s_svcs;
static const char *s_name = "AURA";
static const ble_host_client_t *s_clients[MAX_CLIENTS];
static SemaphoreHandle_t s_lock;
static volatile bool s_running;
static volatile bool s_synced;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        app_log_error(TAG, "No BLE address");
        return;
    }
    s_synced = true;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        const ble_host_client_t *c = s_clients[i];
        if (c && c->on_sync) {
            c->on_sync();
        }
    }
}

static void on_reset(int reason)
{
    s_synced = false;
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        const ble_host_client_t *c = s_clients[i];
        if (c && c->on_reset) {
            c->on_reset(reason);
        }
    }
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool stack_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        app_log_error(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (s_svcs) {
        int rc = ble_gatts_count_cfg(s_svcs);
        if (rc == 0) {
            rc = ble_gatts_add_svcs(s_svcs);
        }
        if (rc != 0) {
            app_log_error(TAG, "GATT register rc=%d", rc);
            nimble_port_deinit();
            return false;
        }
    }
    ble_svc_gap_device_name_set(s_name);

    /* NimBLE her GATT işlemini INFO'da yazar: ROLL telemetrisinde (10+ bildirim/sn)
     * konsol host görevini yavaşlatıp bildirimleri düşürüyordu. */
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    s_running = true;
    nimble_port_freertos_init(host_task);
    app_log_info(TAG, "BLE stack started");
    return true;
}

static void stack_stop(void)
{
    s_running = false;
    s_synced = false;
    nimble_port_stop();  /* bağlantıları/reklamı/taramayı kapatır, host görevi biter */
    nimble_port_deinit();
    app_log_info(TAG, "BLE stack stopped");
}

void ble_host_init(const struct ble_gatt_svc_def *svcs, const char *name)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    s_svcs = svcs;
    if (name) {
        s_name = name;
    }
}

bool ble_host_acquire(const ble_host_client_t *client)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] == client) {
            xSemaphoreGive(s_lock);
            return true;
        }
        if (!s_clients[i] && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        xSemaphoreGive(s_lock);
        app_log_error(TAG, "No free client slot");
        return false;
    }
    /* Slot yığından önce dolsun: ilk sync bu kullanıcıyı da görsün. */
    s_clients[slot] = client;
    bool was_running = s_running;
    if (!was_running && !stack_start()) {
        s_clients[slot] = NULL;
        xSemaphoreGive(s_lock);
        return false;
    }
    bool synced = was_running && s_synced;
    xSemaphoreGive(s_lock);

    /* Yığın önceden açıktı: bu kullanıcının sync'i kaçırdığı yerden başlat. */
    if (synced && client->on_sync) {
        client->on_sync();
    }
    return true;
}

void ble_host_release(const ble_host_client_t *client)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool any = false;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] == client) {
            s_clients[i] = NULL;
        }
        any |= s_clients[i] != NULL;
    }
    if (!any && s_running) {
        stack_stop();
    }
    xSemaphoreGive(s_lock);
}

int ble_host_client_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        n += s_clients[i] != NULL;
    }
    return n;
}

uint8_t ble_host_own_addr_type(void)
{
    return s_own_addr_type;
}
