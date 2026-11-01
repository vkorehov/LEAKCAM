# Shared by firmware/bl616 and tests/firmware/bl616_powertest. Set HW_ROOT (the k230d-hw directory) first.
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
#
# sdk_patch.py applies LEAKCAM's few bouffalo_sdk changes first (idempotent; it stops the build if the SDK moved).
TOOLCHAIN ?= $(HW_ROOT)/toolchain_gcc_t-head_linux
IMAGE ?= k230-rtos-sdk-build:arm64-x86tc
ISP := $(HW_ROOT)/LEAKCAM/tools/power-bringup/bl616_isp.py

.DEFAULT_GOAL := all
.PHONY: all build flash clean
# The SDK's own build (project.build) deletes the CMake cache and regenerates autoconfig.h on every make, so every
# build recompiled the whole SDK: over 10 min (2026-10-28). With the configuration unchanged (defconfig, Kconfig and
# CMakeLists.txt older than the cache) the generated build in build/ runs directly: only changed files compile, and
# CMake still re-runs by itself when one of its files changed (sdk_patch.py's .cmake edit; it needs BL_SDK_BASE in
# the container, 2026-10-29). Otherwise the SDK's path.
INCREMENTAL := [ build/CMakeCache.txt -nt defconfig ] && [ build/CMakeCache.txt -nt Kconfig ] && \
	[ build/CMakeCache.txt -nt CMakeLists.txt ]
all build:
	python3 $(HW_ROOT)/LEAKCAM/firmware/bl616/sdk_patch.py $(BL_SDK_BASE)
	docker run --rm --user $$(id -u):$$(id -g) -e HOME=/tmp -e LEAKCAM_IN_CONTAINER=1 -e BL_SDK_BASE=$(BL_SDK_BASE) \
	    -e PATH=$(TOOLCHAIN)/bin:/usr/bin:/bin -v $(HW_ROOT):$(HW_ROOT) -w $(CURDIR) $(IMAGE) sh -c \
	    'if $(INCREMENTAL); then for t in pre_build all post_build combine; do \
	        make --no-print-directory -C build -j8 $$t || exit 1; done; else make -j5; fi'
	@ls -l build/build_out/*.bin
flash:
	port=$$($(ISP)) && \
	docker run --rm --user $$(id -u):$$(id -g) --group-add $$(stat -c %g $$port) --device $$port -e HOME=/tmp \
	    -e LEAKCAM_IN_CONTAINER=1 -v $(HW_ROOT):$(HW_ROOT) -w $(CURDIR) $(IMAGE) \
	    make flash COMX=$$port BL_FLASH_PROGRAM=$(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu
	$(ISP) --boot $$($(ISP))
	@$(ISP) --wait-app
	@python3 $(HW_ROOT)/LEAKCAM/tools/power-bringup/provision_pin.py   # the BLE pairing PIN, pre-stored once
clean:
	rm -rf build
