cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# the two-port USB console (usb_console2.c) replaces the SDK's usb_app/usb_console.c, which find_package() added
get_target_property(_app_sources app SOURCES)
list(FILTER _app_sources EXCLUDE REGEX "usb_app/usb_console\\.c$")
set_property(TARGET app PROPERTY SOURCES ${_app_sources})
target_include_directories(app PRIVATE ${BL_SDK_BASE}/bsp/common/usb_app)
# the full firmware (DESIGN.md 5.3.1: operational, USB power, the K230): its own sources here, the modules it shares
# with the setup firmware in firmware/bl616; this folder first (FreeRTOSConfig.h, lwipopts_user.h are its own)
set(S ${CMAKE_CURRENT_SOURCE_DIR}/../bl616)
sdk_add_include_directories(.)
sdk_add_include_directories(${S})

target_sources(app PRIVATE
               k230_power.c
               usb_power.c
               provision.c
               presence.c
               wifi_link.c
               ${S}/usb_console2.c
               ${S}/wdg_guard.c
               ${S}/crash_rec.c
               ${S}/mem_stat.c
               ${S}/device_id.c
               ${S}/ble_pairing.c
               ${S}/wifi_check.c)

# crash_rec.c: the trap entry and the Wi-Fi library's stack overflow hook (skill firmware-crash-recorder)
sdk_add_link_options(-Wl,--wrap=exception_entry -Wl,--wrap=vApplicationStackOverflowHook -Wl,--wrap=malloc
                     -Wl,--wrap=calloc -Wl,--wrap=realloc)
# the Wi-Fi library build options (the SDK's NetHub example); NetHub's SDIO device has four ports
sdk_add_compile_definitions(-DCONFIG_RWNX_LWIP -DBL_RA_EN=1 -DWPA_SUPPLICANT_CLEANUP_INTERVAL=1800)
sdk_add_compile_definitions(-D SDIO2_MAX_PORT_NUM=4)
# .hbn_noinit (the crash record) in HBN RAM
sdk_set_linker_script_macro(${S}/bl616_leakcam.ld.in)

sdk_set_main_file(main.c)

project(leakcam_full)
