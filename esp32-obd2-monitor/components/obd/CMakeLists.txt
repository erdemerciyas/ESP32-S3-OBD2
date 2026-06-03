idf_component_register(
    SRCS "obd_service.c" "pid_table.c" "obd_parser.c" "pid_support.c"
    INCLUDE_DIRS "."
    REQUIRES connectivity freertos esp_timer
)
