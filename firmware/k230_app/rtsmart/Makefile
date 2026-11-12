# leakcam, the K230 product program, as a k230_rtos_sdk application (RT-Smart user space).
# Installed by firmware/k230_board/install.sh: this folder -> src/applications/leakcam, the sources in ./src.
# The program goes into the kernel's ROMFS as /app/leakcam (DESIGN.md 5.3.7: inside the A/B slot, updated and rolled
# back with the kernel): rom_c.py turns it into app_canmv/leakcam_rom.c, and the SDK's top Makefile (install.sh) links
# the kernel again after this build. RTT_AUTO_EXEC_CMD starts /app/leakcam; nothing goes to /sdcard.
include ../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

SRC_ROOT ?= $(if $(wildcard $(CURDIR)/src/leakcam.c),$(CURDIR)/src,$(realpath $(CURDIR)/..))
BUILD    := $(SDK_APPS_BUILD_DIR)/leakcam
PROG     := $(BUILD)/leakcam
ROM_C    := $(SDK_RTSMART_SRC_DIR)/rtsmart/kernel/bsp/maix3/app_canmv/leakcam_rom.c
OBJS     := $(patsubst $(SRC_ROOT)/%.c,$(BUILD)/%.o,$(wildcard $(SRC_ROOT)/*.c))

# the cameras (vicap_cap.c, 2026-11-12): MPP's headers and libraries, as tests/firmware/k230_apps/rtsmart/Makefile
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

CFLAGS   := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -std=gnu11 -I$(SRC_ROOT)
CFLAGS   += $(LIB_CFLAGS) -I$(SDK_SRC_ROOT_DIR)/include -include "generated/autoconf.h"
CFLAGS   += -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS  := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections $(LIB_LDFLAGS)

.PHONY: all clean distclean
all: $(ROM_C)
	@$(ECHO) [BUILD] applications leakcam done.

$(ROM_C): $(PROG) $(CURDIR)/rom_c.py
	$(Q)python3 $(CURDIR)/rom_c.py $(PROG) $@

$(PROG): $(OBJS)
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDFLAGS) -lm -o $@
	$(Q)$(STRIP) $@

$(BUILD)/%.o: $(SRC_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(Q)rm -rf $(BUILD)
	$(Q)python3 $(CURDIR)/rom_c.py - $(ROM_C)

distclean: clean
