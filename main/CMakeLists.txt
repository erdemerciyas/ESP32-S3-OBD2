idf_component_register(
    SRCS
        "main.c"
        "app_mode.c"
        "bsp/bsp_display.cpp"
        "bsp/lvgl_v8_port.cpp"
        "data/vehicle_data.c"
        "data/vehicle_profile.c"
        "data/app_log.c"
        "data/dtc_db.c"
        "data/clock.c"
        "imu/qmi8658.c"
        "imu/imu_data.c"
        "obd/ble_obd.c"
        "obd/wifi_obd.c"
        "obd/obd_link.c"
        "obd/elm327.c"
        "obd/obd_pids.c"
        "obd/obd_dtc.c"
        "nav/nav_state.c"
        "nav/nav_service.c"
        "nav/nav_mock.c"
        "nav/nav_codec_json.c"
        "nav/nav_transport_ble.c"
        "nav/nav_track.c"
        "nav/nav_map.c"
        "ui/theme.c"
        "ui/ui.c"
        "ui/screen_splash.c"
        "ui/screen_connect.c"
        "ui/screen_dash.c"
        "ui/screen_grid.c"
        "ui/screen_dtc.c"
        "ui/screen_settings.c"
        "ui/screen_gyro.c"
        "ui/screen_home.c"
        "ui/screen_clock.c"
        "ui/screen_nav.c"
        "ui/screen_nav_map.c"
        "ui/screen_nav_trip.c"
        "ui/screen_nav_bg.c"
        "ui/ui_shots.c"
        "ui/fx3d.c"
        "ui/lv_font_montserrat_56.c"
        "ui/lv_font_montserrat_94_bold.c"
        "ui/lv_font_tr_16.c"
        "ui/lv_font_tr_20.c"
    INCLUDE_DIRS
        "."
        "bsp"
        "data"
        "imu"
        "obd"
        "nav"
        "ui"
    REQUIRES
        nvs_flash
        bt
        esp_wifi
        esp_netif
        esp_event
        lwip
        esp_timer
        freertos
        driver
        json
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)

idf_build_get_property(build_components BUILD_COMPONENTS)
foreach(COMPONENT ${build_components})
    if(COMPONENT MATCHES "lvgl" OR COMPONENT MATCHES "lvgl__lvgl")
        set(TARGET_COMPONENT ${COMPONENT})
        break()
    endif()
endforeach()
if(TARGET_COMPONENT STREQUAL "")
    message(STATUS "lvgl component not yet resolved at configure time")
else()
    idf_component_get_property(LVGL_LIB ${TARGET_COMPONENT} COMPONENT_LIB)
    target_compile_options(${LVGL_LIB} PRIVATE "-Wno-format")
endif()
