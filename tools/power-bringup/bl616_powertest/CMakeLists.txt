cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# The LEAKCAM two-port USB console (firmware/bl616/usb_console2.c: the BL616 shell + the K230 console) replaces
# the SDK's single-port bsp/common/usb_app/usb_console.c; CONFIG_BSP_CONSOLE_USB_CDC stays on (printf and
# board_init() use the same function names). find_package() above already added the SDK file to app.
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/usb_console2.c)
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)


sdk_add_include_directories(.)

# the product firmware's AHT20 driver (I2C test); unused code is dropped by the linker
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/aht20.c)

# the product's watchdog (the BL616 never stays hung)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/wdg_guard.c)
# crash_rec.c: faults and hangs with a backtrace, kept across the reset (user 2026-10-31: every firmware has it)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/crash_rec.c)
sdk_add_link_options(-Wl,--wrap=exception_entry -Wl,--wrap=vApplicationStackOverflowHook)
# the product's memory metrics (`mem`: every test logs it, leak detection)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/mem_stat.c)
sdk_add_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616)

# pt_wifi (test100): the product's Wi-Fi library build options (attic/firmware/bl616/CMakeLists.txt, from the SDK's NetHub example)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pt_wifi.c)
# the product's Wi-Fi link (NetHub bridge + the BL616-only path): pt_wifi runs the code the product ships
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/wifi_link.c)
# pt_ble (test040): the product's BLE provisioning and its credential check
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pt_ble.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/ble_pairing.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/wifi_check.c)
sdk_add_compile_definitions(-DCONFIG_RWNX_LWIP)
sdk_add_compile_definitions(-DBL_RA_EN=1)
sdk_add_compile_definitions(-DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)

# pt_sdu: four SDIO ports, as the K230 driver and the SDK's sdio2_test use
sdk_add_compile_definitions(-D SDIO2_MAX_PORT_NUM=4)

# HBN-retained variables (.hbn_noinit): the product's linker script
sdk_set_linker_script_macro(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/bl616_leakcam.ld.in)

sdk_set_main_file(main.c)

project(bl616_powertest)
