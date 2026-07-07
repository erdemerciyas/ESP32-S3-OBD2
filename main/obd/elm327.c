#include "elm327.h"
#include "ble_obd.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "app_log.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "elm327";

#define RX_BUF_SIZE      512
#define CMD_QUEUE_LEN    16
#define ELM_QUEUE_MAX    8
#define ELM_TASK_STACK   4096
#define ELM_TASK_PRIO    5
#define TIMEOUT_FLUSH_MS 60

typedef struct {
    char cmd[64];
    char expect[8];
    elm327_response_cb_t cb;
    void *user_data;
    uint32_t timeout_ms;
} elm_cmd_t;

static elm327_state_t s_state = ELM_STATE_IDLE;
static char s_rx_buf[RX_BUF_SIZE];
static size_t s_rx_len;
static QueueHandle_t s_cmd_queue;
static SemaphoreHandle_t s_sync_sem;
static SemaphoreHandle_t s_elm_mutex;
static char s_sync_response[256];
static char s_pending_response[256];
static char s_inflight_expect[8];
static TaskHandle_t s_task_handle;

static void elm_lock(void) { xSemaphoreTake(s_elm_mutex, portMAX_DELAY); }
static void elm_unlock(void) { xSemaphoreGive(s_elm_mutex); }

static bool s_elm_configured;

static void elm327_run_init(void);
static bool send_raw(const char *cmd);
static bool wait_response(uint32_t timeout_ms);
static void drain_stale_sync(void);
static void flush_rx_after_timeout(void);
static void derive_expect_token(const char *cmd, char *expect, size_t expect_len);
static void set_inflight_expect(const char *cmd);

static void trim_response(char *s)
{
    char *start = s;
    while (*start && (isspace((unsigned char)*start) || *start == '>' || *start == '\r' || *start == '\n')) {
        start++;
    }
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == '\r' || start[len - 1] == '\n' || start[len - 1] == '>' ||
                       isspace((unsigned char)start[len - 1]))) {
        start[--len] = '\0';
    }
    if (start != s) {
        memmove(s, start, len + 1);
    }
}

static bool is_intermediate_line(const char *line)
{
    return strstr(line, "SEARCHING") != NULL ||
           strstr(line, "BUS INIT") != NULL ||
           strstr(line, "CONNECTING") != NULL ||
           strstr(line, "BUS LOAD") != NULL;
}

static bool looks_like_voltage(const char *line)
{
    float v = 0.0f;
    if (sscanf(line, "%f", &v) != 1) {
        return false;
    }
    return v >= 9.0f && v <= 17.0f;
}

static bool is_hex_data_line(const char *line)
{
    if (!line || line[0] == '\0') {
        return false;
    }
    for (const char *p = line; *p; p++) {
        if (!isspace((unsigned char)*p) && !isxdigit((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

static bool pending_has_obd_mode(void)
{
    return strstr(s_pending_response, "41") != NULL ||
           strstr(s_pending_response, "43") != NULL ||
           strstr(s_pending_response, "44") != NULL ||
           strstr(s_pending_response, "47") != NULL;
}

static int count_hex_digits(const char *line)
{
    int n = 0;
    for (const char *p = line; *p; p++) {
        if (isxdigit((unsigned char)*p)) {
            n++;
        }
    }
    return n;
}

static bool obd_line_complete(const char *line)
{
    if (!line || line[0] == '\0') {
        return false;
    }
    if (strstr(line, "41") == NULL &&
        strstr(line, "43") == NULL &&
        strstr(line, "44") == NULL &&
        strstr(line, "47") == NULL) {
        return false;
    }
    return count_hex_digits(line) >= 4;
}

static void append_hex_line(const char *line)
{
    if (!line || line[0] == '\0') {
        return;
    }
    size_t len = strlen(s_pending_response);
    if (len > 0) {
        snprintf(s_pending_response + len, sizeof(s_pending_response) - len, " %s", line);
    } else {
        strncpy(s_pending_response, line, sizeof(s_pending_response) - 1);
        s_pending_response[sizeof(s_pending_response) - 1] = '\0';
    }
}

static bool is_final_response(const char *line)
{
    if (line[0] == '\0') {
        return false;
    }
    if (strcmp(line, "OK") == 0) {
        return true;
    }
    if (strstr(line, "ERROR") != NULL ||
        strstr(line, "UNABLE") != NULL ||
        strstr(line, "NO DATA") != NULL ||
        strstr(line, "STOPPED") != NULL) {
        return true;
    }
    if (strstr(line, "41") != NULL ||
        strstr(line, "43") != NULL ||
        strstr(line, "44") != NULL ||
        strstr(line, "47") != NULL) {
        return true;
    }
    if (strchr(line, '.') != NULL && strchr(line, 'V') != NULL) {
        return true;
    }
    if (strncmp(line, "ELM", 3) == 0) {
        return true;
    }
    if (looks_like_voltage(line)) {
        return true;
    }
    return false;
}

static void consume_rx_bytes(size_t count)
{
    if (count >= s_rx_len) {
        s_rx_len = 0;
        s_rx_buf[0] = '\0';
        return;
    }
    memmove(s_rx_buf, s_rx_buf + count, s_rx_len - count);
    s_rx_len -= count;
    s_rx_buf[s_rx_len] = '\0';
}

static void clear_pending_response(void)
{
    s_pending_response[0] = '\0';
}

static void drain_stale_sync(void)
{
    if (!s_sync_sem) {
        return;
    }
    while (xSemaphoreTake(s_sync_sem, 0) == pdTRUE) {
    }
}

static void flush_rx_after_timeout(void)
{
    vTaskDelay(pdMS_TO_TICKS(TIMEOUT_FLUSH_MS));
    elm_lock();
    s_rx_len = 0;
    s_rx_buf[0] = '\0';
    s_pending_response[0] = '\0';
    s_sync_response[0] = '\0';
    elm_unlock();
    drain_stale_sync();
}

static void derive_expect_token(const char *cmd, char *expect, size_t expect_len)
{
    if (!expect || expect_len == 0) {
        return;
    }
    expect[0] = '\0';
    if (!cmd || cmd[0] == '\0') {
        return;
    }

    /* AT commands: accept OK/ELM/voltage/error via empty expect filter. */
    if ((cmd[0] == 'A' || cmd[0] == 'a') && (cmd[1] == 'T' || cmd[1] == 't')) {
        return;
    }

    if (!isxdigit((unsigned char)cmd[0]) || !isxdigit((unsigned char)cmd[1])) {
        return;
    }

    int mode = 0;
    if (sscanf(cmd, "%2x", &mode) != 1) {
        return;
    }

    size_t pos = 0;
    pos += (size_t)snprintf(expect, expect_len, "%02X", mode + 0x40);
    /* Yalnızca İLK PID'i eşleştir (mode + ilk 2 hex). Çoklu-PID isteklerinde
     * (örn. "010C0D") yanıt "410C..0D.." biçiminde döner; tam "410C0D" dizisi
     * yanıtta ardışık bulunmaz. İlk PID'e göre eşleştirmek tek-PID davranışını
     * değiştirmez, çoklu-PID yanıtının geçerli sayılmasını sağlar. */
    int hex_taken = 0;
    for (const char *p = cmd + 2; *p && pos < expect_len - 1 && hex_taken < 2; p++) {
        if (isxdigit((unsigned char)*p)) {
            expect[pos++] = (char)toupper((unsigned char)*p);
            hex_taken++;
        }
    }
    expect[pos] = '\0';
}

static void set_inflight_expect(const char *cmd)
{
    derive_expect_token(cmd, s_inflight_expect, sizeof(s_inflight_expect));
}

static bool pending_matches_inflight(void)
{
    if (s_inflight_expect[0] == '\0') {
        return true;
    }
    if (strstr(s_pending_response, s_inflight_expect) != NULL) {
        return true;
    }
    return strstr(s_pending_response, "NO DATA") != NULL ||
           strstr(s_pending_response, "UNABLE") != NULL ||
           strstr(s_pending_response, "ERROR") != NULL ||
           strstr(s_pending_response, "STOPPED") != NULL;
}

static int response_priority(const char *line)
{
    if (strstr(line, "41") != NULL ||
        strstr(line, "43") != NULL ||
        strstr(line, "44") != NULL ||
        strstr(line, "47") != NULL) {
        return 3;
    }
    if (strstr(line, "ERROR") != NULL ||
        strstr(line, "UNABLE") != NULL ||
        strstr(line, "NO DATA") != NULL ||
        strstr(line, "STOPPED") != NULL) {
        return 2;
    }
    if (strchr(line, '.') != NULL) {
        return 2;
    }
    if (looks_like_voltage(line)) {
        return 2;
    }
    if (strcmp(line, "OK") == 0) {
        return 1;
    }
    if (strncmp(line, "ELM", 3) == 0) {
        return 1;
    }
    return 1;
}

static void consider_response_line(const char *line)
{
    if (line[0] == '\0') {
        return;
    }
    if (s_pending_response[0] == '\0') {
        strncpy(s_pending_response, line, sizeof(s_pending_response) - 1);
        s_pending_response[sizeof(s_pending_response) - 1] = '\0';
        return;
    }
    /* Multi-frame OBD response: accumulate hex continuation data */
    if (pending_has_obd_mode() && is_hex_data_line(line)) {
        append_hex_line(line);
        return;
    }

    if (response_priority(line) >= response_priority(s_pending_response)) {
        strncpy(s_pending_response, line, sizeof(s_pending_response) - 1);
        s_pending_response[sizeof(s_pending_response) - 1] = '\0';
    }
}

static void deliver_pending_response(void)
{
    if (s_pending_response[0] == '\0') {
        return;
    }
    if (!pending_matches_inflight()) {
        ESP_LOGW(TAG, "Discarding stale response (expected %s): %.48s",
                 s_inflight_expect, s_pending_response);
        clear_pending_response();
        return;
    }
    if (s_sync_sem) {
        strncpy(s_sync_response, s_pending_response, sizeof(s_sync_response) - 1);
        s_sync_response[sizeof(s_sync_response) - 1] = '\0';
        xSemaphoreGive(s_sync_sem);
    }
    clear_pending_response();
}

static void process_rx_buffer(void)
{
    while (s_rx_len > 0) {
        char *cr = (char *)memchr(s_rx_buf, '\r', s_rx_len);
        char *lf = (char *)memchr(s_rx_buf, '\n', s_rx_len);
        char *gt = (char *)memchr(s_rx_buf, '>', s_rx_len);

        char *end = NULL;
        size_t consume = 0;

        if (cr) {
            end = cr;
            consume = 1;
        } else if (lf) {
            end = lf;
            consume = 1;
        } else if (gt) {
            end = gt;
            consume = 1;
        } else {
            break;
        }

        char line[256];
        size_t line_len = (size_t)(end - s_rx_buf);
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, s_rx_buf, line_len);
        line[line_len] = '\0';
        bool was_prompt = (gt != NULL && end == gt);
        trim_response(line);
        consume_rx_bytes((size_t)(end - s_rx_buf) + consume);

        if (was_prompt && line[0] == '\0') {
            deliver_pending_response();
            continue;
        }
        if (line[0] == '\0') {
            continue;
        }
        if (is_intermediate_line(line)) {
            continue;
        }
        if (!is_final_response(line)) {
            if (is_hex_data_line(line) && pending_has_obd_mode()) {
                append_hex_line(line);
            }
            continue;
        }
        consider_response_line(line);
        if (strstr(line, "NO DATA") != NULL ||
            strstr(line, "UNABLE") != NULL ||
            strstr(line, "ERROR") != NULL ||
            looks_like_voltage(line) ||
            (strchr(line, 'V') != NULL && strchr(line, '.') != NULL)) {
            deliver_pending_response();
        } else if (!pending_has_obd_mode() && obd_line_complete(s_pending_response)) {
            deliver_pending_response();
        }
    }
}

static void elm327_on_rx(const uint8_t *data, size_t len)
{
    elm_lock();
    for (size_t i = 0; i < len; i++) {
        if (s_rx_len < RX_BUF_SIZE - 1) {
            s_rx_buf[s_rx_len++] = (char)data[i];
            s_rx_buf[s_rx_len] = '\0';
        }
    }
    process_rx_buffer();
    elm_unlock();
}

static bool send_raw(const char *cmd)
{
    char frame[80];
    int n = snprintf(frame, sizeof(frame), "%s\r", cmd);
    ESP_LOGD(TAG, "TX: %s", cmd);
    return ble_obd_send((const uint8_t *)frame, n);
}

static bool wait_response(uint32_t timeout_ms)
{
    if (!s_sync_sem) {
        return false;
    }
    s_sync_response[0] = '\0';
    return xSemaphoreTake(s_sync_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void elm327_run_init(void)
{
    const vehicle_profile_t *profile = vehicle_profile_get();
    const char *full_cmds[] = {
        "ATZ",
        "ATE0",
        "ATL0",
        "ATS0",
        "ATH0",
        profile->init_protocol_cmd,
        profile->init_timeout_cmd,
        "ATAT1",
    };
    const char *quick_cmds[] = {
        "ATE0",
        "ATL0",
        "ATS0",
        "ATH0",
        profile->init_protocol_cmd,
        profile->init_timeout_cmd,
        "ATAT1",
    };
    const char **cmds;
    size_t cmd_count;
    uint32_t atz_delay_ms;

    s_state = ELM_STATE_INIT;
    vehicle_data_clear_supported_pids();

    if (s_elm_configured) {
        cmds = quick_cmds;
        cmd_count = sizeof(quick_cmds) / sizeof(quick_cmds[0]);
        atz_delay_ms = 0;
        vehicle_data_set_state(OBD_STATE_ELM_INIT, "Initializing adapter...");
    } else {
        cmds = full_cmds;
        cmd_count = sizeof(full_cmds) / sizeof(full_cmds[0]);
        atz_delay_ms = 500;
        vehicle_data_set_state(OBD_STATE_ELM_INIT, "Starting ELM327...");
    }

    for (size_t i = 0; i < cmd_count; i++) {
        drain_stale_sync();
        elm_lock();
        clear_pending_response();
        set_inflight_expect(cmds[i]);
        elm_unlock();

        if (!send_raw(cmds[i])) {
            s_inflight_expect[0] = '\0';
            s_state = ELM_STATE_ERROR;
            vehicle_data_set_state(OBD_STATE_ERROR, "ELM327 send failed");
            app_log_error(TAG, "Send failed during init: %s", cmds[i]);
            return;
        }
        if (i == 0 && atz_delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(atz_delay_ms));
        }
        if (!wait_response(1500)) {
            ESP_LOGW(TAG, "Timeout on %s", cmds[i]);
            flush_rx_after_timeout();
        }
        s_inflight_expect[0] = '\0';
    }

    s_elm_configured = true;
    s_state = ELM_STATE_READY;
    vehicle_data_set_state(OBD_STATE_PID_DISCOVERY, "Discovering PIDs...");
    app_log_info(TAG, "Init complete (%s / %s)", profile->display_name, profile->protocol);
}

static void elm327_task(void *arg)
{
    elm_cmd_t cmd;
    s_sync_sem = xSemaphoreCreateBinary();

    while (1) {
        if (!ble_obd_is_connected()) {
            s_state = ELM_STATE_IDLE;
            s_elm_configured = false;
            s_rx_len = 0;
            s_rx_buf[0] = '\0';
            s_inflight_expect[0] = '\0';
            clear_pending_response();
            vehicle_data_clear_supported_pids();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (s_state == ELM_STATE_ERROR) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (s_state == ELM_STATE_IDLE) {
            elm327_run_init();
            continue;
        }

        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }

        s_state = ELM_STATE_BUSY;
        app_log_info(TAG, "TX %s", cmd.cmd);

        drain_stale_sync();
        elm_lock();
        clear_pending_response();
        set_inflight_expect(cmd.cmd);
        elm_unlock();

        if (!send_raw(cmd.cmd)) {
            s_inflight_expect[0] = '\0';
            app_log_warn(TAG, "Send failed: %s", cmd.cmd);
            s_state = ELM_STATE_READY;
            continue;
        }

        if (wait_response(cmd.timeout_ms)) {
            if (cmd.cb) {
                cmd.cb(s_sync_response, cmd.user_data);
            }
        } else {
            ESP_LOGW(TAG, "Timeout: %s", cmd.cmd);
            flush_rx_after_timeout();
        }

        s_inflight_expect[0] = '\0';
        s_state = ELM_STATE_READY;
    }
}

void elm327_init(void)
{
    s_elm_mutex = xSemaphoreCreateMutex();
    s_cmd_queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(elm_cmd_t));
    ble_obd_set_rx_callback(elm327_on_rx_data);
}

void elm327_start(void)
{
    xTaskCreatePinnedToCore(elm327_task, "elm327", ELM_TASK_STACK, NULL, ELM_TASK_PRIO,
                            &s_task_handle, 0);
}

void elm327_stop(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
}

bool elm327_send_cmd(const char *cmd, elm327_response_cb_t cb, void *user_data, uint32_t timeout_ms)
{
    return elm327_send_cmd_prio(cmd, cb, user_data, timeout_ms, false);
}

bool elm327_send_cmd_prio(const char *cmd, elm327_response_cb_t cb, void *user_data,
                          uint32_t timeout_ms, bool high_priority)
{
    if (!elm327_is_ready() && s_state != ELM_STATE_INIT) {
        return false;
    }
    elm_cmd_t item = {0};
    strncpy(item.cmd, cmd, sizeof(item.cmd) - 1);
    item.cb = cb;
    item.user_data = user_data;
    item.timeout_ms = timeout_ms;
    if (high_priority) {
        return xQueueSendToFront(s_cmd_queue, &item, pdMS_TO_TICKS(50)) == pdTRUE;
    }
    return xQueueSend(s_cmd_queue, &item, pdMS_TO_TICKS(50)) == pdTRUE;
}

bool elm327_send_cmd_sync(const char *cmd, char *response, size_t response_len, uint32_t timeout_ms)
{
    drain_stale_sync();
    elm_lock();
    clear_pending_response();
    set_inflight_expect(cmd);
    elm_unlock();

    if (!send_raw(cmd)) {
        s_inflight_expect[0] = '\0';
        return false;
    }
    if (!wait_response(timeout_ms)) {
        flush_rx_after_timeout();
        s_inflight_expect[0] = '\0';
        return false;
    }
    strncpy(response, s_sync_response, response_len - 1);
    response[response_len - 1] = '\0';
    s_inflight_expect[0] = '\0';
    return true;
}

elm327_state_t elm327_get_state(void)
{
    return s_state;
}

bool elm327_is_ready(void)
{
    return s_state == ELM_STATE_READY || s_state == ELM_STATE_BUSY;
}

bool elm327_is_busy(void)
{
    return s_state == ELM_STATE_BUSY;
}

uint32_t elm327_queue_depth(void)
{
    if (!s_cmd_queue) {
        return 0;
    }
    return (uint32_t)uxQueueMessagesWaiting(s_cmd_queue);
}

bool elm327_can_queue(bool high_priority)
{
    if (!elm327_is_ready()) {
        return false;
    }
    if (high_priority) {
        return elm327_queue_depth() < ELM_QUEUE_MAX;
    }

    if (elm327_is_busy() || elm327_queue_depth() >= ELM_QUEUE_MAX - 1) {
        return false;
    }
    return true;
}

void elm327_on_rx_data(const uint8_t *data, size_t len)
{
    elm327_on_rx(data, len);
}
