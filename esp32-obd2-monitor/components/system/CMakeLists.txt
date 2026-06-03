idf_component_register(
    SRCS "settings.c" "conn_log.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash app esp_timer
)
