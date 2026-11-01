# LEAKCAM BL616 product firmware.
#   make          build (on an arm64 host inside the K230 build container: host.mk)
#   make flash    flash through PR1, the running firmware switches to the ROM by itself
#   make clean
SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath ../../../bouffalo_sdk)

export BL_SDK_BASE

CHIP ?= bl616
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

ifeq ($(shell uname -m)-$(LEAKCAM_IN_CONTAINER),aarch64-)
HW_ROOT := $(abspath ../../..)
include host.mk
else
include $(BL_SDK_BASE)/project.build
endif
