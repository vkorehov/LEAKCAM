# leakcam, the K230 product program, as a k230_rtos_sdk application (RT-Smart user space).
# Installed by firmware/k230_board/install.sh: this folder -> src/applications/leakcam, the sources in ./src.
# The binary lands in $(SDK_APPS_IMAGE_DIR) = /sdcard/app on the target (RTT_AUTO_EXEC_CMD starts it).
include ../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

SRC_ROOT ?= $(if $(wildcard $(CURDIR)/src/leakcam.c),$(CURDIR)/src,$(realpath $(CURDIR)/..))
BUILD    := $(SDK_APPS_BUILD_DIR)/leakcam
PROG     := $(SDK_APPS_IMAGE_DIR)/leakcam

CFLAGS   := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -std=gnu11 -I$(SRC_ROOT)
CFLAGS   += -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS  := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections

.PHONY: all clean distclean
all: $(PROG)
	@$(ECHO) [BUILD] applications leakcam done.

$(PROG): $(BUILD)/leakcam.o
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(BUILD)/%.o: $(SRC_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(Q)rm -rf $(BUILD) $(PROG)

distclean: clean
