# LEAKCAM BL616 low-power setup mode test firmware (tests/test120): PDS + the SDK's lpfw advertising BLE.
#   make                       build (on an arm64 host: inside the K230 build container, firmware/bl616/host.mk)
#   make flash                 flash through PR1: switches the running firmware to the ROM by itself
SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath ../../../../bouffalo_sdk)

export BL_SDK_BASE

CHIP ?= bl616
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

ifeq ($(shell uname -m)-$(LEAKCAM_IN_CONTAINER),aarch64-)
HW_ROOT := $(abspath ../../../..)
include ../../../firmware/bl616/host.mk
else
include $(BL_SDK_BASE)/project.build
endif
