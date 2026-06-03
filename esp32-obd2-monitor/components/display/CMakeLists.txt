idf_component_register(
    SRCS "display.c"
        "lvgl_port/lvgl_driver.c"
        "lvgl_port/tca9554_expander.c"
        "lvgl_port/cst820_touch.c"
        "fonts/font_ui_12.c"
        "fonts/font_ui_14.c"
        "fonts/font_ui_16.c"
        "fonts/font_ui_20.c"
        "fonts/font_icons_24.c"
        "fonts/font_icons_28.c"
        "ui/dashboard.c"
        "ui/gauge.c"
        "ui/styles.c"
        "ui/splash.c"
        "ui/wifi_settings_ui.c"
        "ui/haptic.c"
        "ui/ui_icons.c"
    INCLUDE_DIRS "." "lvgl_port" "ui" "fonts"
    REQUIRES lvgl esp_lcd espressif__esp_lcd_st7701 espressif__esp_lcd_panel_io_additions
             espressif__esp_io_expander espressif__esp_lcd_touch driver app telemetry connectivity system obd
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
