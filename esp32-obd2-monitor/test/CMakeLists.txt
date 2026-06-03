idf_component_register(
    SRCS "test_obd_parser.c" "test_pid_support.c"
    INCLUDE_DIRS "."
    REQUIRES unity obd
)
