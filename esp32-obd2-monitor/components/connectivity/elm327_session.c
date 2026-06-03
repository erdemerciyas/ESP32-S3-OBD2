#include "elm327_session.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "elm327";

bool elm327_response_looks_valid(const char *resp, size_t len)
{
    if (resp == NULL || len == 0) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = resp[i];
        if (c == '>') {
            return true;
        }
    }

    const char *markers[] = {"ELM327", "elm327", "OBDII", "STN", "ICAR", "OBDLink", "VLINK", "OK", "41 "};
    for (size_t m = 0; m < sizeof(markers) / sizeof(markers[0]); m++) {
        if (strstr(resp, markers[m]) != NULL) {
            return true;
        }
    }

    return false;
}

bool elm327_probe_adapter(elm327_send_fn send, elm327_recv_fn recv)
{
    if (send == NULL || recv == NULL) {
        return false;
    }

    if (send("ATI\r", 4) < 0) {
        return false;
    }

    char resp[128] = {0};
    int total = 0;

    for (int attempt = 0; attempt < 4; attempt++) {
        int n = recv(resp + total, sizeof(resp) - (size_t)total - 1, 200);
        if (n > 0) {
            total += n;
            resp[total] = '\0';
            if (elm327_response_looks_valid(resp, (size_t)total)) {
                ESP_LOGI(TAG, "Adapter probe OK");
                return true;
            }
        } else if (n == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    ESP_LOGW(TAG, "Adapter probe failed");
    return false;
}

bool elm327_init_session(elm327_send_fn send, elm327_recv_fn recv)
{
    if (send == NULL || recv == NULL) {
        return false;
    }

    const char *init_cmds[] = {
        "ATZ\r", "ATE0\r", "ATL0\r", "ATS0\r", "ATH0\r",
        "ATAT1\r", "ATST32\r", "ATSP0\r",
    };
    const int delays_ms[] = {800, 100, 100, 100, 100, 100, 100, 400};

    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        if (send(init_cmds[i], strlen(init_cmds[i])) < 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(delays_ms[i]));
    }

    char drain[128];
    for (int i = 0; i < 4; i++) {
        int n = recv(drain, sizeof(drain) - 1, 50);
        if (n <= 0) {
            break;
        }
    }

    ESP_LOGI(TAG, "ELM327 session initialized");
    return true;
}

static bool response_has_rpm_pid(const char *resp)
{
    return strstr(resp, "410C") != NULL || strstr(resp, "41 0C") != NULL ||
           strstr(resp, "41 0c") != NULL;
}

bool elm327_probe_obd_ready(elm327_send_fn send, elm327_recv_fn recv)
{
    if (send == NULL || recv == NULL) {
        return false;
    }

    if (send("010C\r", 5) < 0) {
        return false;
    }

    char resp[128] = {0};
    int total = 0;

    for (int attempt = 0; attempt < 6; attempt++) {
        int n = recv(resp + total, sizeof(resp) - (size_t)total - 1, 300);
        if (n > 0) {
            total += n;
            resp[total] = '\0';
            if (response_has_rpm_pid(resp)) {
                ESP_LOGI(TAG, "OBD bus ready (RPM PID OK)");
                return true;
            }
            if (strchr(resp, '>') != NULL && attempt >= 2) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    ESP_LOGW(TAG, "OBD RPM probe inconclusive");
    return false;
}
