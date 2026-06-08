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

    const char *markers[] = {
        "ELM327", "elm327", "OBDII", "STN", "ICAR", "OBDLink", "VLINK",
        "OK", "41 ", "SEARCHING", "UNABLE", "NO DATA", "CAN", "ISO", "KWP",
    };
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

    const char *wake_cmds[] = { "ATWS\r", "ATI\r" };
    for (size_t c = 0; c < sizeof(wake_cmds) / sizeof(wake_cmds[0]); c++) {
        const char *cmd = wake_cmds[c];
        if (send(cmd, strlen(cmd)) < 0) {
            continue;
        }

        char resp[160] = {0};
        int total = 0;

        for (int attempt = 0; attempt < 6; attempt++) {
            int n = recv(resp + total, sizeof(resp) - (size_t)total - 1, 250);
            if (n > 0) {
                total += n;
                resp[total] = '\0';
                if (elm327_response_looks_valid(resp, (size_t)total)) {
                    ESP_LOGI(TAG, "Adapter probe OK (%s)", cmd);
                    return true;
                }
            } else if (n == 0) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }

    ESP_LOGW(TAG, "Adapter probe failed");
    return false;
}

static bool elm327_drain(elm327_recv_fn recv)
{
    char drain[128];
    for (int i = 0; i < 6; i++) {
        int n = recv(drain, sizeof(drain) - 1, 80);
        if (n <= 0) {
            break;
        }
    }
    return true;
}

static bool elm327_send_and_wait(elm327_send_fn send, elm327_recv_fn recv,
                                 const char *cmd, int delay_ms)
{
    if (send(cmd, strlen(cmd)) < 0) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    elm327_drain(recv);
    return true;
}

bool elm327_init_session(elm327_send_fn send, elm327_recv_fn recv)
{
    if (send == NULL || recv == NULL) {
        return false;
    }

    const char *init_cmds[] = {
        "ATZ\r", "ATE0\r", "ATL0\r", "ATS0\r", "ATH0\r",
        "ATAT1\r", "ATST64\r", "ATSP0\r",
    };
    const int delays_ms[] = {1200, 100, 100, 100, 100, 100, 100, 600};

    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        if (!elm327_send_and_wait(send, recv, init_cmds[i], delays_ms[i])) {
            return false;
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

static bool response_has_obd_data(const char *resp)
{
    if (response_has_rpm_pid(resp)) {
        return true;
    }
    return strstr(resp, "41 00") != NULL || strstr(resp, "4100") != NULL ||
           strstr(resp, "41 05") != NULL || strstr(resp, "4105") != NULL;
}

static bool elm327_try_pid_probe(elm327_send_fn send, elm327_recv_fn recv, const char *pid_cmd)
{
    if (send(pid_cmd, strlen(pid_cmd)) < 0) {
        return false;
    }

    char resp[192] = {0};
    int total = 0;

    for (int attempt = 0; attempt < 8; attempt++) {
        int n = recv(resp + total, sizeof(resp) - (size_t)total - 1, 350);
        if (n > 0) {
            total += n;
            resp[total] = '\0';
            if (response_has_obd_data(resp)) {
                return true;
            }
            if (strchr(resp, '>') != NULL && attempt >= 2) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return false;
}

static bool elm327_try_protocol(elm327_send_fn send, elm327_recv_fn recv, const char *atsp_cmd)
{
    if (!elm327_send_and_wait(send, recv, atsp_cmd, atsp_cmd[3] == '0' ? 600 : 1200)) {
        return false;
    }

    if (elm327_try_pid_probe(send, recv, "0100\r")) {
        ESP_LOGI(TAG, "OBD protocol OK after %s", atsp_cmd);
        return true;
    }

    return elm327_try_pid_probe(send, recv, "010C\r");
}

bool elm327_probe_obd_ready(elm327_send_fn send, elm327_recv_fn recv)
{
    if (send == NULL || recv == NULL) {
        return false;
    }

    if (elm327_try_pid_probe(send, recv, "010C\r")) {
        ESP_LOGI(TAG, "OBD bus ready (RPM PID OK)");
        return true;
    }

    const char *protocol_cmds[] = {
        "ATSP0\r",
        "ATSP6\r",
        "ATSP7\r",
        "ATSP4\r",
        "ATSP5\r",
        "ATSP3\r",
        "ATSP0\r",
    };

    for (size_t i = 0; i < sizeof(protocol_cmds) / sizeof(protocol_cmds[0]); i++) {
        if (elm327_try_protocol(send, recv, protocol_cmds[i])) {
            ESP_LOGI(TAG, "OBD bus ready (protocol fallback)");
            return true;
        }
    }

    ESP_LOGW(TAG, "OBD RPM probe inconclusive - ignition may be OFF");
    return false;
}
