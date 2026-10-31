SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath ../../../bouffalo_sdk)

export BL_SDK_BASE

CHIP ?= bl616
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

# make / make flash: on an arm64 host in the build container, flash without BOOT/EN wires (host.mk)
ifeq ($(shell uname -m)-$(LEAKCAM_IN_CONTAINER),aarch64-)
HW_ROOT := $(abspath ../../..)
include host.mk
else
include $(BL_SDK_BASE)/project.build
endif
