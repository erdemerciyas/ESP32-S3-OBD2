idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES app display obd system connectivity nvs_flash esp_wifi bt esp_event esp_netif esp_timer driver freertos
)
