cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(.)

target_sources(app PRIVATE
                   aon_state.c
                   k230_power.c
                   k230_link.c
                   leak_wake.c
                   usb_power.c
                   ble_pairing.c
                   aht20.c
                   )

sdk_set_main_file(main.c)

project(leakcam_pwrmgr)
