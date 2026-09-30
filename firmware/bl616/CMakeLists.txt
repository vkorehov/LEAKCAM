cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# the same Wi-Fi library build options as the SDK's NetHub example
sdk_add_compile_definitions(-DCONFIG_RWNX_LWIP)
sdk_add_compile_definitions(-DBL_RA_EN=1)
sdk_add_compile_definitions(-DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)

sdk_add_include_directories(.)

# debug build: JTAG on IO12-IO15 instead of Wi-Fi (DEBUG.txt): rm -rf build, then LEAKCAM_JTAG=1 make
if(DEFINED ENV{LEAKCAM_JTAG})
    sdk_add_compile_definitions(-DLEAKCAM_JTAG)
endif()
# The default build is for board bring-up: first boot holds the K230 in reset until a terminal
# opens the PR1 console, then 30 min READY/heartbeat timeouts (DEBUG.txt). The release build
# leaves that out: rm -rf build, then LEAKCAM_RELEASE=1 make
if(DEFINED ENV{LEAKCAM_RELEASE})
    sdk_add_compile_definitions(-DLEAKCAM_RELEASE)
endif()

target_sources(app PRIVATE
                   aon_state.c
                   evlog.c
                   k230_power.c
                   k230_link.c
                   leak_wake.c
                   usb_power.c
                   ble_pairing.c
                   aht20.c
                   wifi_link.c
                   wifi_check.c
                   )

sdk_set_main_file(main.c)

project(leakcam)
