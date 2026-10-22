# Shared by firmware/bl616 and tools/power-bringup/bl616_powertest. Set HW_ROOT (the k230d-hw directory) first.
#
# The T-Head RISC-V toolchain is x86-64 only. On an arm64 host the build runs in the K230 build container
# (firmware/BUILD.md 2.2: native arm64 Debian with amd64 runtime libraries), which this file starts by itself.
# Flashing runs there too, with the x86-64 BLFlashCommand-ubuntu: the SDK's BLFlashCommand-arm is a 32-bit
# armhf binary and does not start on an arm64-only host.
#
# make flash needs no BOOT link while the firmware's USB console runs: bl616_isp.py sends `reboot uart` and
# the ROM comes up on USB. `reboot uart` leaves the ROM's user-boot field set and a reset keeps it, so after
# flashing bl616_isp.py --boot clears it and resets through the ROM (no EN tap), then make waits for the new
# firmware's console.
TOOLCHAIN ?= $(HW_ROOT)/toolchain_gcc_t-head_linux
IMAGE ?= k230-rtos-sdk-build:arm64-x86tc
ISP := $(HW_ROOT)/LEAKCAM/tools/power-bringup/bl616_isp.py

.DEFAULT_GOAL := all
.PHONY: all build flash clean
all build:
	docker run --rm --user $$(id -u):$$(id -g) -e HOME=/tmp -e LEAKCAM_IN_CONTAINER=1 \
	    -e PATH=$(TOOLCHAIN)/bin:/usr/bin:/bin -v $(HW_ROOT):$(HW_ROOT) -w $(CURDIR) $(IMAGE) make -j5
	@ls -l build/build_out/*.bin
flash:
	port=$$($(ISP)) && \
	docker run --rm --user $$(id -u):$$(id -g) --group-add $$(stat -c %g $$port) --device $$port -e HOME=/tmp \
	    -e LEAKCAM_IN_CONTAINER=1 -v $(HW_ROOT):$(HW_ROOT) -w $(CURDIR) $(IMAGE) \
	    make flash COMX=$$port BL_FLASH_PROGRAM=$(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu
	$(ISP) --boot $$($(ISP))
	@$(ISP) --wait-app --timeout 120
clean:
	rm -rf build
