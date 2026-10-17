# LEAKCAM BL616 power-up test firmware.
#   make                       build (on an arm64 host: inside the K230 build container, see below)
#   make flash COMX=/dev/ttyACM0    flash through PR1 (BL616 in ROM boot mode, README.md)
#   make clean
SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath ../../../../bouffalo_sdk)
TOOLCHAIN ?= $(abspath ../../../../toolchain_gcc_t-head_linux)
IMAGE ?= k230-rtos-sdk-build:arm64-x86tc

export BL_SDK_BASE

CHIP ?= bl616
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

# The T-Head RISC-V toolchain is x86-64 only. On an arm64 host the build runs in the K230 build
# container (firmware/BUILD.md 2.2: native arm64 Debian with amd64 runtime libraries), which this
# Makefile starts by itself. Flashing runs there too, with the x86-64 BLFlashCommand-ubuntu: the SDK's
# BLFlashCommand-arm is a 32-bit armhf binary and does not start on an arm64-only host.
ifeq ($(shell uname -m)-$(LEAKCAM_IN_CONTAINER),aarch64-)
.DEFAULT_GOAL := all
.PHONY: all build flash clean
all build:
	docker run --rm --user $$(id -u):$$(id -g) -e HOME=/tmp -e LEAKCAM_IN_CONTAINER=1 \
	    -e PATH=$(TOOLCHAIN)/bin:/usr/bin:/bin -v $(abspath ../../../..):$(abspath ../../../..) \
	    -w $(CURDIR) $(IMAGE) make -j5
	@ls -l build/build_out/*.bin
flash:
	docker run --rm --user $$(id -u):$$(id -g) --group-add $$(stat -c %g $(COMX)) --device $(COMX) -e HOME=/tmp \
	    -e LEAKCAM_IN_CONTAINER=1 -v $(abspath ../../../..):$(abspath ../../../..) -w $(CURDIR) $(IMAGE) \
	    make flash COMX=$(COMX) BL_FLASH_PROGRAM=$(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu
clean:
	rm -rf build
else
include $(BL_SDK_BASE)/project.build
endif
