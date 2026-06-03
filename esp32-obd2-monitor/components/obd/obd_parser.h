#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t obd_parse_response_ascii(const uint8_t *resp, size_t resp_len,
                                   uint8_t *pid, uint8_t *data_bytes, size_t *data_len);

float obd_decode_pid_value(uint8_t pid, const uint8_t *bytes, size_t byte_len);

#ifdef __cplusplus
}
#endif
