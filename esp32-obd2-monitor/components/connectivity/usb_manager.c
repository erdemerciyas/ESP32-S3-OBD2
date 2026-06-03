#include "usb_manager.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>
#include "app.h"

static const char *TAG = "usb";

static bool connected = false;
static uart_port_t uart_num = UART_NUM_0;

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
    QueueHandle_t uart_queue;
    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size * 2, uart_buffer_size * 2, 10, &uart_queue, 0));

    connected = true;
    ESP_LOGI(TAG, "USB UART initialized at %d baud", UART_OBD2_BAUD);

    return true;
}

void usb_cdc_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting USB");

    uart_driver_delete(uart_num);
    connected = false;
}

esp_err_t usb_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (resp_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*resp_len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (cmd == NULL || resp == NULL || len < 2) {
        *resp_len = 0;
        return ESP_ERR_INVALID_ARG;
    }

    if (!connected) {
        *resp_len = 0;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd_str[32];
    int cmd_len = snprintf((char *)cmd_str, sizeof(cmd_str), "%02X%02X\r\n", cmd[0], cmd[1]);

    int written = uart_write_bytes(uart_num, (const char *)cmd_str, cmd_len);
    if (written != cmd_len) {
        *resp_len = 0;
        return ESP_FAIL;
    }

    int received = uart_read_bytes(uart_num, (char *)resp, *resp_len - 1, pdMS_TO_TICKS(500));
    if (received <= 0) {
        *resp_len = 0;
        return ESP_ERR_TIMEOUT;
    }

    resp[received] = 0;
    *resp_len = received;

    return ESP_OK;
}

bool usb_is_connected(void)
{
    return connected;
}
