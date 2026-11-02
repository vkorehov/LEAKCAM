# LEAKCAM BL616 full firmware (operational: Wi-Fi bridge, K230 sessions, USB power; DESIGN.md 5.3.1).
#   make          build (on an arm64 host inside the K230 build container: host.mk)
#   make flash    flash the product pair through PR1 (the running firmware switches to the ROM by itself): this image
#                 in slot 0 and the setup firmware (build firmware/bl616_setup first) in slot 1, one product version
#                 (firmware/VERSION); SLOT1_BIN=<image> puts another one there (a test bed, tests/test126)
#   make clean
SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath ../../../bouffalo_sdk)

export BL_SDK_BASE
SLOT1_BIN ?= $(abspath ../bl616_setup/build/build_out/leakcam_setup_bl616.bin)

CHIP ?= bl616
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

ifeq ($(shell uname -m)-$(LEAKCAM_IN_CONTAINER),aarch64-)
HW_ROOT := $(abspath ../../..)
include ../bl616_common/host.mk
else
include $(BL_SDK_BASE)/project.build
endif
