idf_component_register(
    SRCS "display.c"
        "lvgl_port/lvgl_driver.c"
        "ui/dashboard.c"
        "ui/gauge.c"
        "ui/styles.c"
    INCLUDE_DIRS "." "lvgl_port" "ui"
    REQUIRES lvgl esp_lcd driver app obd
)
