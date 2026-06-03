idf_component_register(
    SRCS "display.c"
        "lvgl_port/lvgl_driver.c"
        "lvgl_port/tca9554_expander.c"
        "lvgl_port/cst820_touch.c"
        "ui/dashboard.c"
        "ui/gauge.c"
        "ui/styles.c"
        "ui/splash.c"
        "ui/wifi_settings_ui.c"
    INCLUDE_DIRS "." "lvgl_port" "ui"
    REQUIRES lvgl esp_lcd espressif__esp_lcd_st7701 espressif__esp_lcd_panel_io_additions
             espressif__esp_io_expander espressif__esp_lcd_touch driver app obd connectivity system
)
