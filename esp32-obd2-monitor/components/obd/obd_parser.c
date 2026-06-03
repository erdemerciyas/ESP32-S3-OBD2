#include "obd_parser.h"
#include "pid_table.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

esp_err_t obd_parse_response_ascii(const uint8_t *resp, size_t resp_len,
                                   uint8_t *pid, uint8_t *data_bytes, size_t *data_len)
{
    if (resp == NULL || pid == NULL || data_bytes == NULL || data_len == NULL || resp_len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *ptr = resp;
    const uint8_t *end = resp + resp_len;

    while (ptr < end - 2) {
        if (ptr[0] == '4' && ptr[1] == '1') {
            ptr += 2;
            if (ptr < end && *ptr == ' ') {
                ptr++;
            }
            if (ptr + 2 > end) {
                return ESP_ERR_INVALID_RESPONSE;
            }

            char pid_str[3] = {(char)ptr[0], (char)ptr[1], '\0'};
            *pid = (uint8_t)strtol(pid_str, NULL, 16);
            ptr += 2;

            if (ptr < end && *ptr == ' ') {
                ptr++;
            }

            *data_len = 0;
            while (ptr + 1 < end && *data_len < 8) {
                if (!isxdigit(ptr[0]) || !isxdigit(ptr[1])) {
                    break;
                }
                char byte_str[3] = {(char)ptr[0], (char)ptr[1], '\0'};
                data_bytes[*data_len] = (uint8_t)strtol(byte_str, NULL, 16);
                (*data_len)++;
                ptr += 2;
                if (ptr < end && *ptr == ' ') {
                    ptr++;
                }
            }
            return ESP_OK;
        }
        ptr++;
    }

    return ESP_ERR_INVALID_RESPONSE;
}

float obd_decode_pid_value(uint8_t pid, const uint8_t *bytes, size_t byte_len)
{
    const pid_info_t *info = pid_get_info((pid_type_t)pid);
    if (info == NULL || byte_len < info->bytes) {
        return 0.0f;
    }

    float raw = 0.0f;
    if (info->bytes == 1) {
        raw = (float)bytes[0];
    } else if (info->bytes == 2) {
        raw = (float)((uint16_t)bytes[0] * 256U + bytes[1]);
    }

    return raw * info->multiplier + info->offset;
}
