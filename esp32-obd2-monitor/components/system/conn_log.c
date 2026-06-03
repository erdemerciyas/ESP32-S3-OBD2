#include "conn_log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "conn_log";
static const char *NVS_NAMESPACE = "conn_log";
static const char *NVS_KEY_BLOB = "entries";

typedef struct {
    uint32_t seq;
    uint32_t uptime_s;
    char msg[CONN_LOG_MSG_LEN];
} conn_log_entry_t;

typedef struct {
    uint32_t seq_next;
    int32_t count;
    int32_t head; /* index of oldest entry */
    conn_log_entry_t entries[CONN_LOG_MAX_ENTRIES];
} conn_log_store_t;

static conn_log_store_t s_store;
static SemaphoreHandle_t s_mutex;

static void conn_log_lock(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void conn_log_unlock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static void conn_log_persist_locked(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_blob(nvs, NVS_KEY_BLOB, &s_store, sizeof(s_store));
    nvs_commit(nvs);
    nvs_close(nvs);
}

void conn_log_init(void)
{
    conn_log_lock();

    memset(&s_store, 0, sizeof(s_store));

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_store);
        esp_err_t err = nvs_get_blob(nvs, NVS_KEY_BLOB, &s_store, &len);
        if (err != ESP_OK || len != sizeof(s_store)) {
            memset(&s_store, 0, sizeof(s_store));
        }
        nvs_close(nvs);
    }

    if (s_store.count < 0 || s_store.count > CONN_LOG_MAX_ENTRIES) {
        s_store.count = 0;
        s_store.head = 0;
        s_store.seq_next = 0;
    }

    conn_log_unlock();

    ESP_LOGI(TAG, "Connection log ready (%d stored entries)", (int)s_store.count);
}

void conn_log_add(const char *fmt, ...)
{
    char buf[CONN_LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* Always echo to serial for live debugging */
    ESP_LOGW(TAG, "%s", buf);

    conn_log_lock();

    int write_idx;
    if (s_store.count < CONN_LOG_MAX_ENTRIES) {
        write_idx = (s_store.head + s_store.count) % CONN_LOG_MAX_ENTRIES;
        s_store.count++;
    } else {
        write_idx = s_store.head;
        s_store.head = (s_store.head + 1) % CONN_LOG_MAX_ENTRIES;
    }

    conn_log_entry_t *e = &s_store.entries[write_idx];
    e->seq = s_store.seq_next++;
    e->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    strncpy(e->msg, buf, sizeof(e->msg) - 1);
    e->msg[sizeof(e->msg) - 1] = '\0';

    conn_log_persist_locked();

    conn_log_unlock();
}

void conn_log_dump(void)
{
    conn_log_lock();

    ESP_LOGW(TAG, "==== Connection log (%d entries) ====", (int)s_store.count);
    for (int i = 0; i < s_store.count; i++) {
        int idx = (s_store.head + i) % CONN_LOG_MAX_ENTRIES;
        const conn_log_entry_t *e = &s_store.entries[idx];
        ESP_LOGW(TAG, "#%lu [t+%lus] %s",
                 (unsigned long)e->seq, (unsigned long)e->uptime_s, e->msg);
    }
    ESP_LOGW(TAG, "==== end of connection log ====");

    conn_log_unlock();
}

int conn_log_count(void)
{
    return (int)s_store.count;
}

const char *conn_log_entry(int idx, uint32_t *uptime_s_out, uint32_t *seq_out)
{
    if (idx < 0 || idx >= s_store.count) {
        return NULL;
    }
    int real = (s_store.head + idx) % CONN_LOG_MAX_ENTRIES;
    const conn_log_entry_t *e = &s_store.entries[real];
    if (uptime_s_out != NULL) {
        *uptime_s_out = e->uptime_s;
    }
    if (seq_out != NULL) {
        *seq_out = e->seq;
    }
    return e->msg;
}

void conn_log_clear(void)
{
    conn_log_lock();
    memset(&s_store, 0, sizeof(s_store));
    conn_log_persist_locked();
    conn_log_unlock();
    ESP_LOGI(TAG, "Connection log cleared");
}
