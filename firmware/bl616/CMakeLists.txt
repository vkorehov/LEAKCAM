cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# The LEAKCAM two-port USB console (firmware/bl616/usb_console2.c: the BL616 shell + the K230 console) replaces
# the SDK's single-port bsp/common/usb_app/usb_console.c; CONFIG_BSP_CONSOLE_USB_CDC stays on (printf and
# board_init() use the same function names). find_package() above already added the SDK file to app.
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/./usb_console2.c)
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)


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

# HBN-retained variables (.hbn_noinit) need the LEAKCAM linker script
sdk_set_linker_script_macro(${CMAKE_CURRENT_SOURCE_DIR}/bl616_leakcam.ld.in)

sdk_set_main_file(main.c)

project(leakcam)
