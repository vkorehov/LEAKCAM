cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# the same Wi-Fi library build options as the SDK's NetHub example
sdk_add_compile_definitions(-DCONFIG_RWNX_LWIP)
sdk_add_compile_definitions(-DBL_RA_EN=1)
sdk_add_compile_definitions(-DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)

sdk_add_include_directories(.)

target_sources(app PRIVATE
                   aon_state.c
                   k230_power.c
                   k230_link.c
                   leak_wake.c
                   usb_power.c
                   ble_pairing.c
                   aht20.c
                   wifi_link.c
                   )

sdk_set_main_file(main.c)

project(leakcam)
