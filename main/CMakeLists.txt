idf_component_register(
    SRCS
        "main.c"
        "bsp/bsp_display.cpp"
        "bsp/lvgl_v8_port.cpp"
        "data/vehicle_data.c"
        "data/vehicle_profile.c"
        "data/app_log.c"
        "obd/ble_obd.c"
        "obd/elm327.c"
        "obd/obd_pids.c"
        "ui/theme.c"
        "ui/ui.c"
        "ui/screen_connect.c"
        "ui/screen_dash.c"
        "ui/screen_grid.c"
        "ui/screen_settings.c"
        "ui/lv_font_montserrat_56.c"
        "ui/lv_font_montserrat_94_bold.c"
    INCLUDE_DIRS
        "."
        "bsp"
        "data"
        "obd"
        "ui"
    REQUIRES
        nvs_flash
        bt
        esp_timer
        freertos
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
