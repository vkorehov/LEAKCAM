cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# the two-port USB console (usb_console2.c) replaces the SDK's usb_app/usb_console.c, which find_package() added
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)

# the setup firmware (DESIGN.md 5.3.1): its own sources here, the modules it shares with the full firmware in
# firmware/bl616_common. This folder first: FreeRTOSConfig.h (tickless idle) and lwipopts_user.h are its own; with the
# product's first the kernel never called the sleep hook (61 mA, tests/test120)
set(S ${CMAKE_CURRENT_SOURCE_DIR}/../bl616_common)
sdk_add_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
sdk_add_include_directories(${S})

target_sources(app PRIVATE
               ${CMAKE_CURRENT_SOURCE_DIR}/setup_boot.c
               ${CMAKE_CURRENT_SOURCE_DIR}/setup_sleep.c
               ${CMAKE_CURRENT_SOURCE_DIR}/switch_to_full.c
               ${CMAKE_CURRENT_SOURCE_DIR}/wifi_setup.c
               ${S}/usb_console2.c
               ${S}/wdg_guard.c
               ${S}/crash_rec.c
               ${S}/mem_stat.c
               ${S}/device_id.c
               ${CMAKE_CURRENT_SOURCE_DIR}/ble_pairing.c
               ${S}/provision.c
               ${S}/wifi_check.c
               ${S}/slot.c
               ${S}/api.c)

# crash_rec.c: the trap entry, the Wi-Fi library's stack overflow hook, malloc failures (skill firmware-crash-recorder);
# setup_sleep.c: the SDK's idle hook behind the BLE-idle check
sdk_add_link_options(-Wl,--wrap=exception_entry -Wl,--wrap=vApplicationStackOverflowHook -Wl,--wrap=malloc
                     -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=vApplicationSleep)
sdk_add_compile_definitions(-DCFG_BLE_ENABLE -DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)
# .hbn_noinit (the crash record) in HBN RAM
sdk_set_linker_script_macro(${S}/bl616_leakcam.ld.in)

# this image's name and the product version (crash_rec.c, slot.c; firmware/VERSION)
set(LEAKCAM_IMAGE setup)
include(${S}/leakcam_version.cmake)
sdk_set_main_file(main.c)

project(leakcam_setup)
