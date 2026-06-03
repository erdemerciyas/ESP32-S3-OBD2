idf_component_register(
    SRCS "obd_service.c" "pid_table.c"
    INCLUDE_DIRS "."
    REQUIRES connectivity freertos
)
