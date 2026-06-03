cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format" APPEND)
project(esp32_obd2_monitor)
