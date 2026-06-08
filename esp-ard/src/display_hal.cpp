#include "display_hal.h"
#include "app_logger.h"
#include "lvgl_v8_port.h"

using namespace esp_panel::board;

static Board *s_board = nullptr;

bool displayHalInit() {
    OBD_LOG("[Display] Initializing board...");
    s_board = new Board();
    if (!s_board->init()) {
        OBD_LOG("[Display] Board init failed");
        return false;
    }

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = s_board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        /* Waveshare 2.1" board default: width*10; VSYNC ile tam kare yenileme (mode 1) */
        static_cast<esp_panel::drivers::BusRGB *>(lcd_bus)
            ->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif

    if (!s_board->begin()) {
        OBD_LOG("[Display] Board begin failed");
        return false;
    }

    OBD_LOG("[Display] Initializing LVGL port...");
    if (!lvgl_port_init(s_board->getLCD(), s_board->getTouch())) {
        OBD_LOG("[Display] LVGL port init failed");
        return false;
    }

    OBD_LOG("[Display] Ready");
    return true;
}

Board *displayHalBoard() {
    return s_board;
}

bool displayHalLvglLock(int timeoutMs) {
    return lvgl_port_lock(timeoutMs);
}

void displayHalLvglUnlock() {
    lvgl_port_unlock();
}
