# LEAKCAM BL616 setup firmware test bed (tests/test120, test121): the setup firmware's sources plus test commands.
#   make                       build (on an arm64 host: inside the K230 build container, firmware/bl616/host.mk)
#   make flash                 flash through PR1: switches the running firmware to the ROM by itself
SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath ../../../../bouffalo_sdk)
# the setup firmware's build options, not a copy (the SDK's Kconfig: no Kconfig here)
DEFCONFIG_FILE := $(abspath ../../../firmware/bl616_setup/defconfig)

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
