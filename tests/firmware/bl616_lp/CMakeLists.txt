cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

if (CONFIG_USBH_MSC)
target_sources(app PRIVATE
    usbh_msc_fatfs.c
              )
endif()

target_sources(app PRIVATE
    wl_ble_lp_app.c
              )

# LEAKCAM (tests/test120): the board's two-port USB console with the device ID as its serial number, the product's
# watchdog (isp for hands-free flashing) and crash recorder
set(F ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616)
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_sources(app PRIVATE ${F}/usb_console2.c ${F}/device_id.c ${F}/wdg_guard.c ${F}/crash_rec.c ${F}/mem_stat.c)
# the setup firmware's BLE provisioning: the product's pairing and credential check on the BL616's own station
# (wifi_local.c, no NetHub bridge), driven by the power test firmware's pt_ble command (test040's runner, provision_pin.py)
target_sources(app PRIVATE ${F}/ble_pairing.c ${F}/wifi_check.c ${F}/wifi_local.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/../bl616_powertest/pt_ble.c)
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)
# this folder first: its FreeRTOSConfig.h (tickless idle), lwipopts_user.h and mbedtls config, not the product's
# (with the product's first the kernel never called the sleep hook: 61 mA, test120 2026-11-01)
sdk_add_include_directories(.)
sdk_add_include_directories(${F})
sdk_add_link_options(-Wl,--wrap=exception_entry -Wl,--wrap=vApplicationStackOverflowHook -Wl,--wrap=malloc
                     -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=vApplicationSleep)
sdk_set_linker_script_macro(${F}/bl616_leakcam.ld.in)

if (CONFIG_BLUETOOTH)
    sdk_add_compile_definitions(-DCFG_BLE_ENABLE)
endif()

sdk_add_compile_definitions(-DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)


sdk_set_main_file(main.c)

project(ble_lp)
