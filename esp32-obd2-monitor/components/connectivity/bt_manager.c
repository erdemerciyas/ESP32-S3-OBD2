#include "bt_manager.h"
#include "esp_log.h"

static const char *TAG = "bt";

#if CONFIG_BT_CLASSIC_ENABLED && CONFIG_BT_SPP_ENABLED

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <inttypes.h>

#include "app.h"

#define BT_DEVICE_NAME "ESP32-S3_OBD2"
#define SPP_RX_BUF_SIZE 256

static bool spp_connected = false;
static uint32_t spp_handle = 0;
static bool bt_initialized = false;

static uint8_t spp_rx_buf[SPP_RX_BUF_SIZE];
static size_t spp_rx_len = 0;
static SemaphoreHandle_t spp_rx_mutex = NULL;
static SemaphoreHandle_t spp_tx_done_sem = NULL;

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
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "SPP_SERVICE");
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

    case ESP_SPP_DATA_IND_EVT:
        if (p->data_ind.len > 0 && xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            size_t to_copy = (p->data_ind.len < SPP_RX_BUF_SIZE) ? p->data_ind.len : SPP_RX_BUF_SIZE;
            memcpy(spp_rx_buf, p->data_ind.data, to_copy);
            spp_rx_len = to_copy;
            xSemaphoreGive(spp_rx_mutex);
        }
        break;

    case ESP_SPP_WRITE_EVT:
        if (p->write.status == ESP_SPP_SUCCESS && spp_tx_done_sem) {
            xSemaphoreGive(spp_tx_done_sem);
        }
        break;

    default:
        break;
    }
}

static void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_AUTH_CMPL_EVT) {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "GAP auth success");
        } else {
            ESP_LOGE(TAG, "GAP auth failed: %d", param->auth_cmpl.stat);
        }
    }
}

bool bt_connect(void)
{
    if (!bt_initialized) {
        spp_rx_mutex = xSemaphoreCreateMutex();
        spp_tx_done_sem = xSemaphoreCreateBinary();
        if (!spp_rx_mutex || !spp_tx_done_sem) {
            return false;
        }

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&bt_cfg) != ESP_OK ||
            esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK ||
            esp_bluedroid_init() != ESP_OK ||
            esp_bluedroid_enable() != ESP_OK) {
            ESP_LOGE(TAG, "Bluetooth init failed");
            return false;
        }

        esp_bt_gap_register_callback(gap_event_handler);
        esp_spp_register_callback(spp_event_handler);
        esp_spp_init(ESP_SPP_MODE_CB);
        bt_initialized = true;
    }

    ESP_LOGI(TAG, "Bluetooth SPP ready");
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
    if (!bt_initialized || !spp_connected || !cmd || !resp || !resp_len) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    spp_rx_len = 0;
    xSemaphoreGive(spp_rx_mutex);

    if (spp_tx_done_sem) {
        xSemaphoreTake(spp_tx_done_sem, 0);
    }

    if (esp_spp_write(spp_handle, (int)len, cmd) != (int32_t)len) {
        return ESP_FAIL;
    }

    if (spp_tx_done_sem) {
        xSemaphoreTake(spp_tx_done_sem, pdMS_TO_TICKS(500));
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(500);
    size_t copied_len = 0;

    while (xTaskGetTickCount() < deadline) {
        if (xSemaphoreTake(spp_rx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (spp_rx_len > 0) {
                size_t to_copy = spp_rx_len;
                if (to_copy > *resp_len) {
                    to_copy = *resp_len;
                }
                memcpy(resp, spp_rx_buf, to_copy);
                copied_len = to_copy;
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
    return copied_len > 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

#else /* ESP32-S3: no Classic BT / SPP */

bool bt_connect(void)
{
    ESP_LOGW(TAG, "Classic Bluetooth SPP is not supported on this chip (use WiFi or USB)");
    return false;
}

void bt_disconnect(void) {}

bool bt_is_connected(void)
{
    return false;
}

esp_err_t bt_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    (void)cmd;
    (void)len;
    (void)resp;
    (void)resp_len;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
