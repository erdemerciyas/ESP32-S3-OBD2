#include "bt_manager.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#include "app.h"

#define BT_DEVICE_NAME "ESP32-S3_OBD2"
#define SPP_RX_BUF_SIZE 256
#define SPP_TX_BUF_SIZE 256
#define BT_CONNECT_WAIT_MS (BT_CONNECT_TIMEOUT_MS)

static const char *TAG = "bt";

static bool spp_connected = false;
static uint32_t spp_handle = 0;
static bool bt_initialized = false;

/* SPP receive buffer - holds the most recent response until bt_send_cmd copies it out */
static uint8_t spp_rx_buf[SPP_RX_BUF_SIZE];
static size_t spp_rx_len = 0;
static SemaphoreHandle_t spp_rx_mutex = NULL;
static SemaphoreHandle_t spp_tx_done_sem = NULL;
static volatile bool spp_write_done = false;

static void spp_event_handler(uint8_t event, void *param);
static void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

static void spp_event_handler(uint8_t event, void *param)
{
    esp_spp_cb_event_t evt = (esp_spp_cb_event_t)event;
    esp_spp_cb_param_t *p = (esp_spp_cb_param_t *)param;

    switch (evt) {
    case ESP_SPP_INIT_EVT:
        if (p->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "SPP initialized");
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "SPP_SERVICE", false, NULL);
        } else {
            ESP_LOGE(TAG, "SPP init failed: %d", p->init.status);
        }
        break;

    case ESP_SPP_START_EVT:
        if (p->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "SPP server started, scn=%d", p->start.scn);
            esp_bt_gap_set_device_name(BT_DEVICE_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        } else {
            ESP_LOGE(TAG, "SPP start failed: %d", p->start.status);
        }
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        spp_connected = true;
        spp_handle = p->srv_open.handle;
        spp_rx_len = 0;
        ESP_LOGI(TAG, "SPP client connected, handle=%" PRIu32, spp_handle);
        break;

    case ESP_SPP_CLOSE_EVT:
        spp_connected = false;
        spp_handle = 0;
        spp_rx_len = 0;
        ESP_LOGI(TAG, "SPP connection closed");
        break;

    case ESP_SPP_DATA_IND_EVT: {
        if (p->data_ind.len == 0) {
            break;
        }
        if (xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            size_t to_copy = (p->data_ind.len < SPP_RX_BUF_SIZE) ? p->data_ind.len : SPP_RX_BUF_SIZE;
            memcpy(spp_rx_buf, p->data_ind.data, to_copy);
            spp_rx_len = to_copy;
            xSemaphoreGive(spp_rx_mutex);
            ESP_LOGD(TAG, "SPP RX %u bytes", (unsigned)to_copy);
        }
        break;
    }

    case ESP_SPP_WRITE_EVT:
        if (p->write.status == ESP_SPP_SUCCESS) {
            spp_write_done = true;
            if (spp_tx_done_sem) {
                xSemaphoreGive(spp_tx_done_sem);
            }
        } else {
            ESP_LOGE(TAG, "SPP write failed: %d", p->write.status);
        }
        break;

    default:
        break;
    }
}

static void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "GAP auth success, dev [%s]", param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(TAG, "GAP auth failed: %d", param->auth_cmpl.stat);
        }
        break;

    default:
        break;
    }
}

bool bt_connect(void)
{
    ESP_LOGI(TAG, "Initializing Bluetooth SPP...");

    if (!bt_initialized) {
        spp_rx_mutex = xSemaphoreCreateMutex();
        spp_tx_done_sem = xSemaphoreCreateBinary();
        if (!spp_rx_mutex || !spp_tx_done_sem) {
            ESP_LOGE(TAG, "Failed to create BT semaphores");
            return false;
        }

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_bt_controller_init(&bt_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bt_controller_init failed: %s", esp_err_to_name(err));
            return false;
        }

        err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bt_controller_enable failed: %s", esp_err_to_name(err));
            return false;
        }

        err = esp_bluedroid_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bluedroid_init failed: %s", esp_err_to_name(err));
            return false;
        }

        err = esp_bluedroid_enable();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bluedroid_enable failed: %s", esp_err_to_name(err));
            return false;
        }

        esp_bt_gap_register_callback(gap_event_handler);
        esp_spp_register_callback(spp_event_handler);
        esp_spp_init(ESP_SPP_MODE_CB);

        bt_initialized = true;
    }

    /* In SLAVE/auto-accept mode the connection happens when the ELM327 adapter
     * pairs with us. The SPP server is already listening from spp_event_handler
     * (ESP_SPP_START_EVT). For now, return true and let the event set the state. */
    ESP_LOGI(TAG, "Bluetooth SPP ready, waiting for client to connect...");
    return true;
}

void bt_disconnect(void)
{
    if (spp_connected) {
        esp_spp_disconnect(spp_handle);
    }
    spp_connected = false;
    spp_handle = 0;
    spp_rx_len = 0;
}

bool bt_is_connected(void)
{
    return spp_connected;
}

esp_err_t bt_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (!bt_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cmd == NULL || resp == NULL || resp_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!spp_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Reset receive buffer and write completion flag before sending */
    if (xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        spp_rx_len = 0;
        xSemaphoreGive(spp_rx_mutex);
    } else {
        return ESP_ERR_TIMEOUT;
    }

    /* Drain any leftover completion from a previous write */
    if (spp_tx_done_sem) {
        xSemaphoreTake(spp_tx_done_sem, 0);
    }
    spp_write_done = false;

    /* Send the command */
    int32_t wrote = esp_spp_write(spp_handle, (int)len, cmd);
    if (wrote != (int32_t)len) {
        ESP_LOGE(TAG, "SPP write failed: %d", wrote);
        return ESP_FAIL;
    }

    /* Wait until the SPP stack confirms the write completed */
    if (spp_tx_done_sem) {
        if (xSemaphoreTake(spp_tx_done_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "SPP write done timeout");
        }
    }

    /* Wait briefly for the ELM327 adapter to respond over SPP */
    const int response_timeout_ms = 500;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(response_timeout_ms);
    size_t copied_len = 0;

    while (xTaskGetTickCount() < deadline) {
        if (xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (spp_rx_len > 0) {
                size_t to_copy = spp_rx_len;
                if (to_copy > *resp_len) {
                    to_copy = *resp_len;
                }
                /* Bug #1 fix: copy from the SPP receive buffer into the
                 * caller-provided `resp` buffer (not back into spp_rx_buf). */
                memcpy(resp, spp_rx_buf, to_copy);
                copied_len = to_copy;

                /* Consume what we just handed out */
                spp_rx_len = 0;
            }
            xSemaphoreGive(spp_rx_mutex);
        }
        if (copied_len > 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    *resp_len = copied_len;
    if (copied_len == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
