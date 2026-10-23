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

# pt_sdu: four SDIO ports, as the K230 driver and the SDK's sdio2_test use
sdk_add_compile_definitions(-D SDIO2_MAX_PORT_NUM=4)

# HBN-retained variables (.hbn_noinit): the product's linker script
sdk_set_linker_script_macro(${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/bl616_leakcam.ld.in)

sdk_set_main_file(main.c)

project(bl616_powertest)
