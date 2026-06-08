idf_component_register(
    SRCS "connectivity.c" "wifi_manager.c" "bt_manager.c" "usb_manager.c" "elm327_session.c"
    INCLUDE_DIRS "."
    REQUIRES app system esp_wifi bt esp_event esp_netif nvs_flash driver lwip esp_coex
)
