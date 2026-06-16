#include "esp_log.h"
#include "nvs_flash.h"

#include "bsp.h"
#include "vehicle_data.h"
#include "ble_obd.h"
#include "elm327.h"
#include "obd_pids.h"
#include "obd_dtc.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP32-S3 OBD2 Dashboard starting");

    vehicle_data_init();

    if (!bsp_display_init()) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    bsp_buzzer_init();

    ui_init();
    ui_start_update_timer();

    ble_obd_init();
    elm327_init();
    obd_pids_init();
    obd_dtc_init();

    elm327_start();
    obd_pids_start();
    ble_obd_start();

    ESP_LOGI(TAG, "All layers ready");
}
