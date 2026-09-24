cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# the same Wi-Fi library build options as the SDK's NetHub example
sdk_add_compile_definitions(-DCONFIG_RWNX_LWIP)
sdk_add_compile_definitions(-DBL_RA_EN=1)
sdk_add_compile_definitions(-DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)

sdk_add_include_directories(.)
# K230 power sequencing and the pin map are shared with the power manager
sdk_add_include_directories(../bl616_pwrmgr)

target_sources(app PRIVATE
                   wifi_link.c
                   ../bl616_pwrmgr/k230_power.c
                   )

sdk_set_main_file(main.c)

project(leakcam_wifi)
