cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(.)

# the product firmware's AHT20 driver (I2C test); unused code is dropped by the linker
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/bl616/aht20.c)

sdk_set_main_file(main.c)

project(bl616_powertest)
