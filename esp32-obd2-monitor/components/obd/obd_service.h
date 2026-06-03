#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "pid_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBD_DTC_MAX_CODES 8
#define OBD_DTC_CODE_LEN 6

typedef struct {
    uint16_t rpm;
    bool rpm_valid;

    uint8_t speed;
    bool speed_valid;

    int16_t coolant_temp;
    bool coolant_valid;

    uint8_t throttle_pos;
    bool throttle_valid;

    uint8_t fuel_level;
    bool fuel_valid;

    uint8_t engine_load;
    bool load_valid;

    int16_t intake_temp;
    bool intake_valid;

    uint16_t maf_rate;
    bool maf_valid;

    float fuel_consumption;
    bool fuel_consumption_valid;

    float battery_voltage;
    bool battery_valid;

    bool dtc_present;
    size_t dtc_count;
    char dtc_codes[OBD_DTC_MAX_CODES][OBD_DTC_CODE_LEN];

    bool session_valid;
    uint32_t last_update_ms;
} obd_data_t;

void obd_service_init(void);
void obd_service_on_disconnect(void);

/** Query ECU supported-PID bitmap (0100/0120/0140) or probe table PIDs. */
esp_err_t obd_service_discover_supported_pids(void);

void obd_service_poll_fast(void);
void obd_service_poll_slow(void);
void obd_service_poll_all(void);
void obd_service_get_data(obd_data_t *data);

/** UI: gauge_type_t values match obd_gauge_id_t. */
bool obd_service_is_gauge_available(uint8_t gauge_id);
unsigned obd_service_available_gauge_count(void);
int obd_service_available_gauge_index(uint8_t gauge_id);

esp_err_t obd_service_send_command(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
bool obd_service_is_connected(void);
esp_err_t obd_service_detect_protocol(char *protocol_str, size_t str_len);
esp_err_t obd_service_test_connection(void);
bool obd_service_get_dtc_codes(uint8_t *dtc_codes, size_t max_codes, size_t *dtc_count);
void obd_service_format_dtc_string(uint8_t b1, uint8_t b2, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
