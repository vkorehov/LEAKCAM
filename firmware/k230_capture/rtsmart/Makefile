# leakcam_capture and leakcam_hist as k230_rtos_sdk applications (RT-Smart user space).
#
# Installed into an SDK by firmware/k230_board/install.sh (copies this folder to
# src/applications/leakcam with the sources in ./src, registers it in apps.mk).
# Binaries land in $(SDK_APPS_IMAGE_DIR) = /sdcard/app on the target.
#
# Follows src/applications/helloworld/Makefile, with explicit sources from the parent folder.
include ../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

# sources: the folder above this Makefile in the LEAKCAM repo, or ./src when install.sh copied
# them into the SDK (the Docker build only sees the SDK tree)
SRC_ROOT ?= $(if $(wildcard $(CURDIR)/src/leakcam_capture.c),$(CURDIR)/src,$(realpath $(CURDIR)/..))
BUILD    := $(SDK_APPS_BUILD_DIR)/leakcam
CAPTURE  := $(SDK_APPS_IMAGE_DIR)/leakcam_capture
HIST     := $(SDK_APPS_IMAGE_DIR)/leakcam_hist

CORE     := imgdiff.c refstore.c history.c third_party/miniz/miniz.c
CAP_SRCS := leakcam_capture.c vicap_cap.c led_rtsmart.c $(CORE)
HIST_SRCS:= leakcam_hist.c $(CORE)
CAP_OBJS := $(addprefix $(BUILD)/cap/, $(CAP_SRCS:.c=.o))
HIST_OBJS:= $(addprefix $(BUILD)/hist/, $(HIST_SRCS:.c=.o))

include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

# miniz: only the one-shot compress/uncompress calls are used
MINIZ    := -DLEAKCAM_MINIZ -DMINIZ_NO_STDIO -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_TIME -I$(SRC_ROOT)/third_party/miniz
CFLAGS   := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -std=gnu11
CFLAGS   += -DLEAKCAM_RTSMART $(MINIZ) -I$(SRC_ROOT) $(LIB_CFLAGS) -I$(SDK_SRC_ROOT_DIR)/include
CFLAGS   += -include "generated/autoconf.h" -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS  := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections $(LIB_LDFLAGS) -lm

.PHONY: all clean distclean
all: $(CAPTURE) $(HIST)
	@$(ECHO) [BUILD] applications leakcam done.

$(CAPTURE): $(CAP_OBJS)
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(HIST): $(HIST_OBJS)
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

# two rules: a pattern rule with two targets would mean "one run makes both"
$(BUILD)/cap/%.o: $(SRC_ROOT)/%.c
	@echo "[CC] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/hist/%.o: $(SRC_ROOT)/%.c
	@echo "[CC] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

clean:
	@rm -f $(CAPTURE) $(HIST)

distclean: clean
	@rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
