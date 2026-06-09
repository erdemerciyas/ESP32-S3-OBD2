#include "obd_service.h"
#include "obd_parser.h"
#include "pid_table.h"
#include "pid_support.h"
#include "connectivity.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char *TAG = "obd_service";

static obd_data_t obd_data;
static SemaphoreHandle_t obd_data_mutex;
static uint8_t pid_fail_streak[PID_TABLE_SIZE];
static bool discover_done;

static const uint8_t cmd_dtc[] = {0x03};

static const struct {
    uint8_t mode;
    uint8_t pid;
} discovery_queries[] = {
    {0x01, 0x00},
    {0x01, 0x20},
    {0x01, 0x40},
};

static uint32_t obd_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void obd_touch_timestamp(void)
{
    obd_data.last_update_ms = obd_now_ms();
}

static void calculate_fuel_consumption(void)
{
    if (!obd_data.maf_valid || !obd_data.rpm_valid) {
        obd_data.fuel_consumption_valid = false;
        return;
    }

    float maf_grams_per_sec = (float)obd_data.maf_rate / 100.0f;
    float fuel_liters_per_hour = (maf_grams_per_sec / 14.7f) * 3600.0f / 750.0f;
    obd_data.fuel_consumption = fuel_liters_per_hour;
    obd_data.fuel_consumption_valid = true;
}

static bool extract_pid_bytes(const uint8_t *data, size_t len, uint8_t expected_pid,
                              uint8_t *bytes, size_t *byte_len)
{
    uint8_t pid = 0;
    size_t parsed_len = 0;

    if (obd_parse_response_ascii(data, len, &pid, bytes, &parsed_len) == ESP_OK &&
        pid == expected_pid && parsed_len > 0) {
        *byte_len = parsed_len;
        return true;
    }

    if (len >= 3 && data[0] == 0x41 && data[1] == expected_pid) {
        *byte_len = len - 2;
        memcpy(bytes, &data[2], *byte_len);
        return *byte_len > 0;
    }

    return false;
}

static void obd_invalidate_pid(uint8_t pid)
{
    switch (pid) {
        case PID_RPM:
            obd_data.rpm_valid = false;
            break;
        case PID_SPEED:
            obd_data.speed_valid = false;
            break;
        case PID_COOLANT_TEMP:
            obd_data.coolant_valid = false;
            break;
        case PID_THROTTLE_POS:
            obd_data.throttle_valid = false;
            break;
        case PID_FUEL_LEVEL:
            obd_data.fuel_valid = false;
            break;
        case PID_ENGINE_LOAD:
            obd_data.load_valid = false;
            break;
        case PID_INTAKE_TEMP:
            obd_data.intake_valid = false;
            break;
        case PID_MAF_RATE:
            obd_data.maf_valid = false;
            obd_data.fuel_consumption_valid = false;
            break;
        case PID_CONTROL_MODULE_VOLTAGE:
            obd_data.battery_valid = false;
            break;
        default:
            break;
    }
}

static esp_err_t apply_pid(uint8_t expected_pid, const uint8_t *resp, size_t len)
{
    uint8_t bytes[8];
    size_t byte_len = 0;

    if (!extract_pid_bytes(resp, len, expected_pid, bytes, &byte_len)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const float value = obd_decode_pid_value(expected_pid, bytes, byte_len);
    obd_touch_timestamp();
    obd_data.session_valid = true;
    pid_support_mark_supported(expected_pid);

    switch (expected_pid) {
        case PID_RPM:
            obd_data.rpm = (uint16_t)value;
            obd_data.rpm_valid = true;
            calculate_fuel_consumption();
            break;
        case PID_SPEED:
            obd_data.speed = (uint8_t)value;
            obd_data.speed_valid = true;
            break;
        case PID_COOLANT_TEMP:
            obd_data.coolant_temp = (int16_t)value;
            obd_data.coolant_valid = true;
            break;
        case PID_THROTTLE_POS:
            obd_data.throttle_pos = (uint8_t)value;
            obd_data.throttle_valid = true;
            break;
        case PID_FUEL_LEVEL:
            obd_data.fuel_level = (uint8_t)value;
            obd_data.fuel_valid = true;
            break;
        case PID_ENGINE_LOAD:
            obd_data.engine_load = (uint8_t)value;
            obd_data.load_valid = true;
            break;
        case PID_INTAKE_TEMP:
            obd_data.intake_temp = (int16_t)value;
            obd_data.intake_valid = true;
            break;
        case PID_MAF_RATE:
            if (byte_len >= 2) {
                obd_data.maf_rate = (uint16_t)bytes[0] * 256U + bytes[1];
                obd_data.maf_valid = true;
                calculate_fuel_consumption();
            }
            break;
        case PID_CONTROL_MODULE_VOLTAGE: {
            float volts = value;
            if (volts > 0.0f && volts < 30.0f) {
                obd_data.battery_voltage = volts;
                obd_data.battery_valid = true;
            }
            break;
        }
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

static unsigned pid_table_index(uint8_t pid)
{
    for (unsigned i = 0; i < PID_TABLE_SIZE; i++) {
        const pid_info_t *info = pid_get_by_index(i);
        if (info != NULL && (uint8_t)info->pid == pid) {
            return i;
        }
    }
    return PID_TABLE_SIZE;
}

static bool obd_poll_pid(uint8_t pid)
{
    if (!pid_support_should_poll(pid)) {
        return false;
    }

    const uint8_t cmd[] = {0x01, pid};
    uint8_t resp[128];
    size_t resp_len = sizeof(resp);

    if (!connectivity_is_connected()) {
        return false;
    }

    if (connectivity_send_cmd(cmd, sizeof(cmd), resp, &resp_len) != ESP_OK) {
        return false;
    }

    const unsigned idx = pid_table_index(pid);

    if (obd_data_mutex != NULL) {
        xSemaphoreTake(obd_data_mutex, portMAX_DELAY);
    }
    esp_err_t parsed = apply_pid(pid, resp, resp_len);

    if (parsed != ESP_OK && idx < PID_TABLE_SIZE) {
        if (pid_fail_streak[idx] < 255) {
            pid_fail_streak[idx]++;
        }
        if (pid_fail_streak[idx] >= OBD_PID_FAIL_STREAK_MAX) {
            obd_invalidate_pid(pid);
            pid_support_mark_unsupported(pid);
        }
    } else if (idx < PID_TABLE_SIZE) {
        pid_fail_streak[idx] = 0;
    }

    if (obd_data_mutex != NULL) {
        xSemaphoreGive(obd_data_mutex);
    }

    return parsed == ESP_OK;
}

static void obd_poll_tier(pid_poll_tier_t tier)
{
    for (unsigned i = 0; i < PID_TABLE_SIZE; i++) {
        const pid_info_t *info = pid_get_by_index(i);
        if (info == NULL || info->tier != tier) {
            continue;
        }
        obd_poll_pid((uint8_t)info->pid);
    }
}

static bool obd_query_supported_block(uint8_t query_pid)
{
    const uint8_t cmd[] = {0x01, query_pid};
    uint8_t resp[128];
    size_t resp_len = sizeof(resp);

    if (connectivity_send_cmd(cmd, sizeof(cmd), resp, &resp_len) != ESP_OK) {
        return false;
    }

    uint8_t bytes[8];
    size_t byte_len = 0;
    if (!extract_pid_bytes(resp, resp_len, query_pid, bytes, &byte_len)) {
        return false;
    }

    pid_support_merge_block(query_pid, bytes, byte_len);
    return true;
}

static void obd_probe_table_fallback(void)
{
    ESP_LOGW(TAG, "Supported-PID bitmap unavailable; probing table PIDs");

    for (unsigned i = 0; i < PID_TABLE_SIZE; i++) {
        const pid_info_t *info = pid_get_by_index(i);
        if (info == NULL) {
            continue;
        }
        const uint8_t pid = (uint8_t)info->pid;
        if (obd_poll_pid(pid)) {
            pid_support_mark_supported(pid);
        } else {
            pid_support_mark_unsupported(pid);
            obd_invalidate_pid(pid);
        }
    }
}

void obd_service_format_dtc_string(uint8_t b1, uint8_t b2, char *out, size_t out_len)
{
    if (out == NULL || out_len < 6) {
        return;
    }

    const char type_chars[] = {'P', 'C', 'B', 'U'};
    uint8_t type_index = (b1 >> 6) & 0x03;
    if (type_index > 3) {
        type_index = 0;
    }

    snprintf(out, out_len, "%c%01X%02X%02X",
             type_chars[type_index],
             (b1 >> 4) & 0x03,
             b1 & 0x0F,
             b2);
}

static void obd_clear_all(void)
{
    memset(&obd_data, 0, sizeof(obd_data));
    memset(pid_fail_streak, 0, sizeof(pid_fail_streak));
    obd_data.session_valid = false;
    pid_support_reset();
    discover_done = false;
}

void obd_service_init(void)
{
    ESP_LOGI(TAG, "Initializing OBD2 service...");

    if (obd_data_mutex == NULL) {
        obd_data_mutex = xSemaphoreCreateMutex();
    }

    obd_clear_all();
    ESP_LOGI(TAG, "OBD2 service initialized");
}

void obd_service_on_disconnect(void)
{
    if (obd_data_mutex != NULL) {
        xSemaphoreTake(obd_data_mutex, portMAX_DELAY);
    }
    obd_clear_all();
    if (obd_data_mutex != NULL) {
        xSemaphoreGive(obd_data_mutex);
    }
    ESP_LOGI(TAG, "OBD session cleared (disconnect)");
}

esp_err_t obd_service_discover_supported_pids(void)
{
    if (!connectivity_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (discover_done) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Discovering supported Mode 01 PIDs...");
    pid_support_reset();

    unsigned blocks_ok = 0;
    for (size_t q = 0; q < sizeof(discovery_queries) / sizeof(discovery_queries[0]); q++) {
        if (obd_query_supported_block(discovery_queries[q].pid)) {
            blocks_ok++;
        }
    }

    if (blocks_ok == 0) {
        obd_probe_table_fallback();
    } else {
        ESP_LOGI(TAG, "Supported-PID bitmap: %u block(s)", blocks_ok);
    }

    discover_done = true;
    return ESP_OK;
}

static void obd_ensure_discovered(void)
{
    if (!discover_done && connectivity_is_connected()) {
        obd_service_discover_supported_pids();
    }
}

void obd_service_poll_fast(void)
{
    if (!connectivity_is_connected()) {
        return;
    }

    obd_ensure_discovered();
    obd_poll_tier(PID_POLL_FAST);
}

void obd_service_poll_slow(void)
{
    if (!connectivity_is_connected()) {
        return;
    }

    obd_ensure_discovered();
    obd_poll_tier(PID_POLL_SLOW);
}

void obd_service_poll_all(void)
{
    obd_service_poll_fast();
    obd_service_poll_slow();
}

static bool obd_gauge_pid_available(int8_t gauge_id)
{
    if (gauge_id == OBD_GAUGE_FUEL_CONSUMPTION) {
        return pid_support_should_poll(PID_MAF_RATE) &&
               pid_support_should_poll(PID_RPM);
    }

    const pid_info_t *info = pid_find_by_gauge(gauge_id);
    if (info == NULL) {
        return false;
    }

    return pid_support_should_poll((uint8_t)info->pid);
}

bool obd_service_is_gauge_available(uint8_t gauge_id)
{
    if (gauge_id >= OBD_GAUGE_COUNT) {
        return false;
    }

    if (!connectivity_is_connected()) {
        return true;
    }

    if (!discover_done) {
        return true;
    }

    if (gauge_id == (uint8_t)OBD_GAUGE_DTC_WARNING) {
        return true;
    }

    return obd_gauge_pid_available((int8_t)gauge_id);
}

unsigned obd_service_available_gauge_count(void)
{
    unsigned count = 0;
    for (uint8_t g = 0; g < OBD_GAUGE_COUNT; g++) {
        if (obd_service_is_gauge_available(g)) {
            count++;
        }
    }
    return count;
}

int obd_service_available_gauge_index(uint8_t gauge_id)
{
    if (!obd_service_is_gauge_available(gauge_id)) {
        return -1;
    }

    int index = 0;
    for (uint8_t g = 0; g < gauge_id; g++) {
        if (obd_service_is_gauge_available(g)) {
            index++;
        }
    }
    return index;
}

void obd_service_get_data(obd_data_t *data)
{
    if (data == NULL) {
        return;
    }

    if (obd_data_mutex != NULL) {
        xSemaphoreTake(obd_data_mutex, portMAX_DELAY);
    }
    *data = obd_data;
    if (obd_data_mutex != NULL) {
        xSemaphoreGive(obd_data_mutex);
    }
}

bool obd_service_data_is_live(const obd_data_t *data)
{
    if (data == NULL || !data->session_valid) {
        return false;
    }

    const uint32_t age_ms = obd_now_ms() - data->last_update_ms;
    if (age_ms > 5000U) {
        return false;
    }

    return data->rpm_valid || data->speed_valid || data->coolant_valid ||
           data->throttle_valid || data->fuel_valid || data->load_valid ||
           data->battery_valid || data->intake_valid;
}

esp_err_t obd_service_send_command(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    return connectivity_send_cmd(cmd, len, resp, resp_len);
}

bool obd_service_is_connected(void)
{
    return connectivity_is_connected() && obd_data.session_valid;
}

esp_err_t obd_service_detect_protocol(char *protocol_str, size_t str_len)
{
    if (!connectivity_is_connected() || protocol_str == NULL || str_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(protocol_str, str_len, "Auto (ATSP0) — Kalos ISO9141/KWP");
    return ESP_OK;
}

esp_err_t obd_service_test_connection(void)
{
    if (!connectivity_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    obd_service_discover_supported_pids();
    obd_service_poll_fast();

    if (obd_data_mutex != NULL) {
        xSemaphoreTake(obd_data_mutex, portMAX_DELAY);
    }
    const bool ok = obd_data.rpm_valid || obd_data.speed_valid;
    if (obd_data_mutex != NULL) {
        xSemaphoreGive(obd_data_mutex);
    }

    return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static void obd_store_dtc_strings(const uint8_t *raw, size_t count)
{
    obd_data.dtc_count = count > OBD_DTC_MAX_CODES ? OBD_DTC_MAX_CODES : count;
    obd_data.dtc_present = obd_data.dtc_count > 0;

    for (size_t i = 0; i < obd_data.dtc_count; i++) {
        obd_service_format_dtc_string(raw[i * 2], raw[i * 2 + 1],
                                      obd_data.dtc_codes[i], OBD_DTC_CODE_LEN);
    }
}

bool obd_service_get_dtc_codes(uint8_t *dtc_codes, size_t max_codes, size_t *dtc_count)
{
    if (!connectivity_is_connected() || dtc_count == NULL) {
        return false;
    }

    uint8_t resp[256];
    size_t resp_len = sizeof(resp);

    if (connectivity_send_cmd(cmd_dtc, sizeof(cmd_dtc), resp, &resp_len) != ESP_OK) {
        return false;
    }

    *dtc_count = 0;
    uint8_t raw[OBD_DTC_MAX_CODES * 2];

    if (resp_len >= 4 && resp[0] == 0x43) {
        size_t dtc_bytes = resp_len - 2;
        *dtc_count = dtc_bytes / 2;
        if (*dtc_count > max_codes) {
            *dtc_count = max_codes;
        }
        memcpy(dtc_codes, &resp[2], *dtc_count * 2);
        memcpy(raw, &resp[2], *dtc_count * 2);
    } else {
        const uint8_t *ptr = resp;
        while (ptr < resp + resp_len - 1) {
            if (ptr[0] == '4' && ptr[1] == '3') {
                ptr += 2;
                if (ptr < resp + resp_len && *ptr == ' ') {
                    ptr++;
                }
                while (ptr + 1 < resp + resp_len && *dtc_count < max_codes) {
                    if (!isxdigit(ptr[0]) || !isxdigit(ptr[1])) {
                        break;
                    }
                    char byte_str[3] = {(char)ptr[0], (char)ptr[1], '\0'};
                    raw[*dtc_count * 2] = (uint8_t)strtol(byte_str, NULL, 16);
                    ptr += 2;
                    if (ptr < resp + resp_len && *ptr == ' ') {
                        ptr++;
                    }
                    if (ptr + 1 >= resp + resp_len || !isxdigit(ptr[0]) || !isxdigit(ptr[1])) {
                        break;
                    }
                    byte_str[0] = (char)ptr[0];
                    byte_str[1] = (char)ptr[1];
                    raw[*dtc_count * 2 + 1] = (uint8_t)strtol(byte_str, NULL, 16);
                    (*dtc_count)++;
                    ptr += 2;
                    if (ptr < resp + resp_len && *ptr == ' ') {
                        ptr++;
                    }
                }
                memcpy(dtc_codes, raw, *dtc_count * 2);
                break;
            }
            ptr++;
        }
    }

    if (obd_data_mutex != NULL) {
        xSemaphoreTake(obd_data_mutex, portMAX_DELAY);
    }
    obd_store_dtc_strings(raw, *dtc_count);
    obd_touch_timestamp();
    if (obd_data_mutex != NULL) {
        xSemaphoreGive(obd_data_mutex);
    }

    return true;
}
