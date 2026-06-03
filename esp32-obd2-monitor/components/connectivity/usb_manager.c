#include "usb_manager.h"
#include "elm327_session.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include "app.h"

static const char *TAG = "usb";

static bool uart_installed = false;
static bool obd_ready = false;
static uart_port_t uart_num = UART_NUM_0;

static int usb_elm_send(const char *data, size_t len)
{
    if (!uart_installed || data == NULL || len == 0) {
        return -1;
    }
    int written = uart_write_bytes(uart_num, data, (int)len);
    return (written == (int)len) ? written : -1;
}

static int usb_elm_recv(char *buf, size_t max_len, int timeout_ms)
{
    if (!uart_installed || buf == NULL || max_len == 0) {
        return 0;
    }

    int n = uart_read_bytes(uart_num, buf, (int)max_len - 1, pdMS_TO_TICKS(timeout_ms));
    if (n > 0) {
        buf[n] = '\0';
    }
    return n;
}

static void usb_flush_rx(void)
{
    uint8_t dump[64];
    while (uart_read_bytes(uart_num, dump, sizeof(dump), pdMS_TO_TICKS(20)) > 0) {
    }
}

bool usb_cdc_connect(void)
{
    ESP_LOGI(TAG, "Initializing USB UART connection...");

    uart_config_t uart_config = {
        .baud_rate = UART_OBD2_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };

    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num,
                                  UART_OBD2_TX_PIN,
                                  UART_OBD2_RX_PIN,
                                  UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE));

    const int uart_buffer_size = 1024;
    if (!uart_installed) {
        QueueHandle_t uart_queue;
        ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size * 2, uart_buffer_size * 2, 10, &uart_queue, 0));
        uart_installed = true;
    }

    usb_flush_rx();
    obd_ready = false;
    ESP_LOGI(TAG, "USB UART ready at %d baud", UART_OBD2_BAUD);
    return true;
}

void usb_cdc_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting USB");

    if (uart_installed) {
        uart_driver_delete(uart_num);
        uart_installed = false;
    }
    obd_ready = false;
}

bool usb_link_up(void)
{
    return usb_cdc_connect();
}

bool usb_obd_session_ready(void)
{
    if (!uart_installed) {
        return false;
    }

    usb_flush_rx();

    if (!elm327_probe_adapter(usb_elm_send, usb_elm_recv)) {
        ESP_LOGW(TAG, "USB ELM327 probe failed");
        return false;
    }

    if (!elm327_init_session(usb_elm_send, usb_elm_recv)) {
        return false;
    }

    obd_ready = elm327_probe_obd_ready(usb_elm_send, usb_elm_recv);
    if (obd_ready) {
        ESP_LOGI(TAG, "USB OBD session ready");
    }
    return obd_ready;
}

esp_err_t usb_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (resp_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*resp_len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (cmd == NULL || resp == NULL || len < 1) {
        *resp_len = 0;
        return ESP_ERR_INVALID_ARG;
    }

    if (!uart_installed || !obd_ready) {
        *resp_len = 0;
        return ESP_ERR_INVALID_STATE;
    }

    char cmd_str[64];
    int cmd_len;
    if (len == 1) {
        cmd_len = snprintf(cmd_str, sizeof(cmd_str), "%02X\r", cmd[0]);
    } else {
        cmd_len = snprintf(cmd_str, sizeof(cmd_str), "%02X%02X\r", cmd[0], cmd[1]);
    }

    usb_flush_rx();
    if (usb_elm_send(cmd_str, (size_t)cmd_len) < 0) {
        *resp_len = 0;
        return ESP_FAIL;
    }

    int total = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        int n = usb_elm_recv((char *)resp + total, *resp_len - (size_t)total - 1, 400);
        if (n > 0) {
            total += n;
            resp[total] = '\0';
            if (resp[total - 1] == '>' || (len >= 2 && strstr((char *)resp, "41") != NULL)) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    if (total == 0) {
        *resp_len = 0;
        return ESP_ERR_TIMEOUT;
    }

    resp[total] = '\0';
    *resp_len = (size_t)total;
    return ESP_OK;
}

bool usb_is_connected(void)
{
    return uart_installed && obd_ready;
}
