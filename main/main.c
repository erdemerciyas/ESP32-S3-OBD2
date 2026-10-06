#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"

#include "bsp.h"
#include "vehicle_data.h"
#include "vehicle_profile.h"
#include "obd_link.h"
#include "elm327.h"
#include "obd_pids.h"
#include "obd_dtc.h"
#include "ui.h"
#include "imu_data.h"
#include "app_mode.h"
#include "roll_feed.h"
#include "roll_cfg.h"
#include "clock.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "AURA v%s (OBD2 + NAV + ROLL) starting", esp_app_get_description()->version);

    vehicle_data_init();
    vehicle_profile_init();

    if (!bsp_display_init()) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    bsp_buzzer_init();
    clock_init();     /* I2C hattı (RTC) ekran kartıyla açılır */

    imu_init();
    imu_start();

    obd_dtc_init();   /* UI açılışta son DTC kaydını gösterir */
    roll_cfg_init();
    roll_feed_init();
    app_mode_init();  /* son mod (OBD / NAV / ROLL); UI açılışta onu gösterir */
    ui_init();
    ui_start_update_timer();

    obd_link_init();
    elm327_init();
    obd_pids_init();

    elm327_start();
    obd_pids_start();
    app_mode_start(); /* NAV ise OBD radyosu hiç açılmaz */

    ESP_LOGI(TAG, "All layers ready");
}
