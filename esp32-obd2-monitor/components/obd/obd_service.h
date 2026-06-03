#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

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

    bool dtc_present;
    size_t dtc_count;
} obd_data_t;

void obd_service_init(void);
void obd_service_poll_all(void);
void obd_service_get_data(obd_data_t *data);
esp_err_t obd_service_send_command(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len);
bool obd_service_is_connected(void);
esp_err_t obd_service_detect_protocol(char *protocol_str, size_t str_len);
esp_err_t obd_service_test_connection(void);
bool obd_service_get_dtc_codes(uint8_t *dtc_codes, size_t max_codes, size_t *dtc_count);
