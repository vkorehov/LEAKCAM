cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# the setup firmware's test bed (tests/test120, test121): the setup firmware's own sources (firmware/bl616_setup) and the
# shared modules (firmware/bl616_common), so what passes here is what ships; this folder adds only test commands (main.c)
# and the power test firmware's pt_ble (test040's runner, provision_pin.py). The defconfig is the
# setup firmware's (Makefile DEFCONFIG_FILE)
set(P ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_setup)
set(S ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common)
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)
# this folder (tests.h), then the setup firmware's (FreeRTOSConfig.h with tickless idle, lwipopts_user.h), then the
# shared one: with the full firmware's FreeRTOSConfig.h first the kernel never called the sleep hook (61 mA, test120)
sdk_add_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
sdk_add_include_directories(${P})
sdk_add_include_directories(${S})

target_sources(app PRIVATE
               ${CMAKE_CURRENT_SOURCE_DIR}/../bl616_powertest/pt_ble.c
               ${P}/setup_boot.c
               ${P}/setup_sleep.c
               ${P}/switch_to_full.c
               ${P}/wifi_setup.c
               ${S}/usb_console2.c
               ${S}/wdg_guard.c
               ${S}/crash_rec.c
               ${S}/mem_stat.c
               ${S}/device_id.c
               ${P}/ble_pairing.c
               ${S}/wifi_check.c
               ${S}/slot.c
               ${S}/api.c)

sdk_add_link_options(-Wl,--wrap=exception_entry -Wl,--wrap=vApplicationStackOverflowHook -Wl,--wrap=malloc
                     -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=vApplicationSleep)
sdk_add_compile_definitions(-DCFG_BLE_ENABLE -DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)
sdk_set_linker_script_macro(${S}/bl616_leakcam.ld.in)

# this image's name and the product version (crash_rec.c, slot.c; firmware/VERSION)
set(LEAKCAM_IMAGE setup_test)
include(${S}/leakcam_version.cmake)
sdk_set_main_file(main.c)

project(bl616_setup_test)
