# Shared by the BL616 firmwares (firmware/bl616_full, bl616_setup) and the test firmwares (tests/firmware/*). Set HW_ROOT (the k230d-hw directory) first.
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
# A test firmware may build from a product's defconfig (DEFCONFIG_FILE) and the SDK's Kconfig: the files it really uses.
INCREMENTAL := $(foreach f,$(or $(DEFCONFIG_FILE),defconfig) $(wildcard Kconfig) CMakeLists.txt,[ build/CMakeCache.txt -nt $(f) ] &&) true
all build:
	python3 $(HW_ROOT)/LEAKCAM/firmware/bl616_common/sdk_patch.py $(BL_SDK_BASE)
	docker run --rm --user $$(id -u):$$(id -g) -e HOME=/tmp -e LEAKCAM_IN_CONTAINER=1 -e BL_SDK_BASE=$(BL_SDK_BASE) \
	    -e PATH=$(TOOLCHAIN)/bin:/usr/bin:/bin -v $(HW_ROOT):$(HW_ROOT) -w $(CURDIR) $(IMAGE) sh -c \
	    'if $(INCREMENTAL); then for t in pre_build all post_build combine; do \
	        make --no-print-directory -C build -j8 $$t || exit 1; done; else make -j5; fi'
	@ls -l build/build_out/*.bin
# The SDK's flash config plus: the partition table into both of boot2's copies (0xE000, 0xF000): after a slot switch
# (slot.c) the copy at 0xF000 is newer and still names the other slot, so writing 0xE000 alone booted the old image.
# SLOT1_BIN=<image> also writes slot 1 (0x210000): the setup firmware next to the full one (DESIGN.md 5.3.1).
# Each image carries "leakcam-build <image> <version>" (crash_rec.c): both images of a pair must have the same product
# version and commit (firmware/VERSION; user 2026-11-02: one product version for both firmwares; a dirty tree's build
# time may differ), and every flashed ELF is kept as ~/.cache/leakcam-elf/<image>-<version>.elf: the crash record
# names its build, and crash_decode.py finds the ELF by it. No commas inside $(if ...) recipes: make splits on them.
FLASH_INI := $(CURDIR)/build/flash_leakcam.ini
ELF_KEEP := $(HOME)/.cache/leakcam-elf
ELF := $(firstword $(wildcard build/build_out/*_bl616.elf))
build_tag = $$(grep -ao 'leakcam-build [a-z_]* [0-9A-Za-z.+-]*' $(1) | head -1)
keep_elf = t="$(call build_tag,$(1:.elf=.bin))"; [ -n "$$t" ] || { echo "flash: no leakcam-build tag in $(1:.elf=.bin)"; exit 1; }; \
	set -- $$t; mkdir -p $(ELF_KEEP); cp $(1) "$(ELF_KEEP)/$$2-$$3.elf"; echo "flash: $$t"
flash:
	@$(call keep_elf,$(ELF))
	$(if $(SLOT1_BIN),@$(call keep_elf,$(abspath $(SLOT1_BIN:.bin=.elf))))
	$(if $(SLOT1_BIN),@a="$(call build_tag,$(ELF:.elf=.bin))"; b="$(call build_tag,$(abspath $(SLOT1_BIN)))"; \
	    a=$${a##* }; b=$${b##* }; [ "$${a%%-dirty*}" = "$${b%%-dirty*}" ] || \
	    { echo "flash: slot 0 is $$a but slot 1 $$b: one product version and commit for both (firmware/VERSION)"; exit 1; })
	cp build/flash_prog_cfg.ini $(FLASH_INI)
	printf '\n[partition1]\nfiledir = %s\naddress = 0xF000\n' $(CURDIR)/build/build_out/partition.bin >> $(FLASH_INI)
	$(if $(SLOT1_BIN),printf '\n[FW1]\nfiledir = %s\naddress = 0x210000\n' $(abspath $(SLOT1_BIN)) >> $(FLASH_INI))
	port=$$($(ISP)) && \
	docker run --rm --user $$(id -u):$$(id -g) --group-add $$(stat -c %g $$port) --device $$port -e HOME=/tmp \
	    -e LEAKCAM_IN_CONTAINER=1 -v $(HW_ROOT):$(HW_ROOT) -w $(CURDIR) $(IMAGE) \
	    $(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --interface=uart --baudrate=2000000 \
	    --port=$$port --chipname=bl616 --config=$(FLASH_INI)
	$(ISP) --boot $$($(ISP))
	@$(ISP) --wait-app
	@python3 $(HW_ROOT)/LEAKCAM/tools/power-bringup/provision_pin.py   # the BLE pairing PIN, pre-stored once
	@python3 $(HW_ROOT)/LEAKCAM/tools/power-bringup/provision_tls.py   # the mTLS CA, certificate and key, once
clean:
	rm -rf build
