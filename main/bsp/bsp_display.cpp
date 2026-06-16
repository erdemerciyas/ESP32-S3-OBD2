#include "esp_check.h"
#include "esp_display_panel.hpp"
#include "esp_lib_utils.h"
#include "lvgl_v8_port.h"
#include "bsp.h"
#include "chip/esp_expander_base.hpp"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static const char *TAG = "bsp";
static Board *s_board = nullptr;

extern "C" bool bsp_display_init(void)
{
    s_board = new Board();
    if (!s_board) {
        return false;
    }

    ESP_LOGI(TAG, "Initializing board");
    if (!s_board->init()) {
        ESP_LOGE(TAG, "Board init failed");
        return false;
    }

#if LVGL_PORT_AVOID_TEARING_MODE
    {
        auto lcd = s_board->getLCD();
        lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
        auto lcd_bus = lcd->getBus();
        if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
            static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
        }
#endif
    }
#endif

    if (!s_board->begin()) {
        ESP_LOGE(TAG, "Board begin failed");
        return false;
    }

    ESP_LOGI(TAG, "Initializing LVGL");
    if (!lvgl_port_init(s_board->getLCD(), s_board->getTouch())) {
        ESP_LOGE(TAG, "LVGL init failed");
        return false;
    }

    return true;
}

extern "C" void bsp_display_lock(int timeout_ms)
{
    lvgl_port_lock(timeout_ms);
}

extern "C" void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}

/* Buzzer on IO expander EXIO8 = pin 7 */
static esp_expander::Base *s_expander = nullptr;

extern "C" void bsp_buzzer_init(void)
{
    if (s_board) {
        s_expander = s_board->getIO_Expander()->getBase();
        if (s_expander) {
            s_expander->pinMode(7, OUTPUT);
            s_expander->digitalWrite(7, LOW);
            ESP_LOGI(TAG, "Buzzer initialized on EXIO8");
        }
    }
}

extern "C" void bsp_buzzer_on(void)
{
    if (s_expander) {
        s_expander->digitalWrite(7, HIGH);
    }
}

extern "C" void bsp_buzzer_off(void)
{
    if (s_expander) {
        s_expander->digitalWrite(7, LOW);
    }
}

extern "C" void bsp_buzzer_beep(int duration_ms)
{
    if (!s_expander) return;
    s_expander->digitalWrite(7, HIGH);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    s_expander->digitalWrite(7, LOW);
}
