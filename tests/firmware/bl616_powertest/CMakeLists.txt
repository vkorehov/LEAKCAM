cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# The LEAKCAM two-port USB console (firmware/bl616_common/usb_console2.c: the BL616 shell + the K230 console) replaces
# the SDK's single-port bsp/common/usb_app/usb_console.c; CONFIG_BSP_CONSOLE_USB_CDC stays on (printf and
# board_init() use the same function names). find_package() above already added the SDK file to app.
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/usb_console2.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/device_id.c)
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)


sdk_add_include_directories(.)

# the product's AHT20 driver (I2C test, tests/test021); unused code is dropped by the linker
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/aht20.c)

# the product's watchdog (the BL616 never stays hung)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/wdg_guard.c)
# crash_rec.c: faults and hangs with a backtrace, kept across the reset (user 2026-10-31: every firmware has it)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/crash_rec.c)
sdk_add_link_options(-Wl,--wrap=exception_entry -Wl,--wrap=vApplicationStackOverflowHook -Wl,--wrap=malloc
                     -Wl,--wrap=calloc -Wl,--wrap=realloc)
# the product's memory metrics (`mem`: every test logs it, leak detection)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/mem_stat.c)
sdk_add_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common)
# the full firmware's lwipopts_user.h (the NetHub bridge's lwIP options): this test firmware runs its wifi_link.c
sdk_add_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_full)

# pt_wifi (test100): the Wi-Fi library build options (from the SDK's NetHub example)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pt_wifi.c)
# the product's Wi-Fi link (NetHub bridge + the BL616-only path): pt_wifi runs the code the product ships
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_full/wifi_link.c)
# pt_ble (test040): the setup firmware's BLE provisioning and the credential check
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pt_ble.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_setup/ble_pairing.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_setup/provision.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/wifi_check.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/api.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/battery.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/metrics.c)
# pt_fault (test110): loss injected into the provisioning through wraps; the product sources stay as they are
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pt_fault.c)
sdk_add_link_options(-Wl,--wrap=bt_gatt_notify_cb -Wl,--wrap=bt_conn_set_security -Wl,--wrap=lwip_getaddrinfo
                     -Wl,--wrap=lwip_recv)
sdk_add_compile_definitions(-DCONFIG_RWNX_LWIP)
sdk_add_compile_definitions(-DBL_RA_EN=1)
sdk_add_compile_definitions(-DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)

# pt_sdu: four SDIO ports, as the K230 driver and the SDK's sdio2_test use
sdk_add_compile_definitions(-D SDIO2_MAX_PORT_NUM=4)

# HBN-retained variables (.hbn_noinit): the product's linker script
sdk_set_linker_script_macro(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/bl616_leakcam.ld.in)

# this image's name and the product version (crash_rec.c, slot.c; firmware/VERSION)
set(LEAKCAM_IMAGE powertest)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616_common/leakcam_version.cmake)
sdk_set_main_file(main.c)

project(bl616_powertest)
