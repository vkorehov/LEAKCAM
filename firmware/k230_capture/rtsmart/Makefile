# leakcam_wake, leakcam_hist, leakcam_stream and leakcam_agent as k230_rtos_sdk applications
# (RT-Smart user space), and leakcam_wake's change net leakcam_change.kmodel.
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
SRC_ROOT ?= $(if $(wildcard $(CURDIR)/src/leakcam_wake.c),$(CURDIR)/src,$(realpath $(CURDIR)/..))
BUILD    := $(SDK_APPS_BUILD_DIR)/leakcam
CAPTURE  := $(SDK_APPS_IMAGE_DIR)/leakcam_wake
HIST     := $(SDK_APPS_IMAGE_DIR)/leakcam_hist
STREAM   := $(SDK_APPS_IMAGE_DIR)/leakcam_stream
KMODEL   := $(SDK_APPS_IMAGE_DIR)/leakcam_change.kmodel
AGENT    := $(SDK_APPS_IMAGE_DIR)/leakcam_agent

CORE     := imgdiff.c refstore.c history.c third_party/miniz/miniz.c
CAP_SRCS := leakcam_wake.c vicap_cap.c led_rtsmart.c netclient.c nnstore.c $(CORE)
HIST_SRCS:= leakcam_hist.c $(CORE)
CAP_OBJS := $(addprefix $(BUILD)/cap/, $(CAP_SRCS:.c=.o) change.o)
HIST_OBJS:= $(addprefix $(BUILD)/hist/, $(HIST_SRCS:.c=.o))
# libopus 1.5.2 (third_party/opus, BSD), float build, from its own source lists
OPUS_DIR := third_party/opus
include $(SRC_ROOT)/$(OPUS_DIR)/celt_sources.mk $(SRC_ROOT)/$(OPUS_DIR)/silk_sources.mk \
        $(SRC_ROOT)/$(OPUS_DIR)/opus_sources.mk
OPUS_SRC := $(addprefix $(OPUS_DIR)/,$(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FLOAT) \
            $(OPUS_SOURCES) $(OPUS_SOURCES_FLOAT))
OPUS     := -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINTF -DHAVE_LRINT \
            $(addprefix -I$(SRC_ROOT)/$(OPUS_DIR)/,include celt silk silk/float)
STREAM_SRCS := leakcam_stream.c rtmp.c netclient.c $(OPUS_SRC)
STREAM_OBJS := $(addprefix $(BUILD)/stream/, $(STREAM_SRCS:.c=.o))
AGENT_OBJS  := $(BUILD)/agent/leakcam_agent.o

# the nncase runtime, for leakcam_wake only
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libnncase.mk
NNCASE_LDFLAGS := $(LIB_LDFLAGS)
LIB_LDFLAGS :=
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

# miniz: only the one-shot compress/uncompress calls are used
MINIZ    := -DMINIZ_NO_STDIO -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_TIME -I$(SRC_ROOT)/third_party/miniz
CFLAGS   := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -std=gnu11
CFLAGS   += $(MINIZ) -I$(SRC_ROOT) $(LIB_CFLAGS) -I$(SDK_SRC_ROOT_DIR)/include
CFLAGS   += -include "generated/autoconf.h" -Wall -Wextra -ffunction-sections -fdata-sections
CXXFLAGS := $(filter-out -std=gnu11 $(MINIZ),$(CFLAGS)) -std=c++20 -Wno-multichar   # nncase model.h
LDFLAGS  := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections $(LIB_LDFLAGS) -lm

.PHONY: all clean distclean
all: $(CAPTURE) $(KMODEL) $(HIST) $(STREAM) $(AGENT)
	@$(ECHO) [BUILD] applications leakcam done.

$(CAPTURE): $(CAP_OBJS)
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CFLAGS) $^ $(LDFLAGS) $(NNCASE_LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(KMODEL): $(SRC_ROOT)/leakcam_change.kmodel
	@mkdir -p $(dir $@)
	$(Q)cp $< $@

$(HIST): $(HIST_OBJS)
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

$(STREAM): $(STREAM_OBJS)
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDFLAGS) -lpthread -o $@
	$(Q)$(STRIP) $@

$(AGENT): $(AGENT_OBJS)
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDFLAGS) -lpthread -o $@
	$(Q)$(STRIP) $@

$(BUILD)/agent/%.o: $(SRC_ROOT)/%.c
	@echo "[CC] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

# one rule per program: a pattern rule with two targets would mean "one run makes both"
$(BUILD)/cap/%.o: $(SRC_ROOT)/%.c
	@echo "[CC] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/cap/change.o: $(SRC_ROOT)/change.cc
	@echo "[CXX] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) -MD -MP -c $< -o $@

$(BUILD)/hist/%.o: $(SRC_ROOT)/%.c
	@echo "[CC] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/stream/%.o: $(SRC_ROOT)/%.c
	@echo "[CC] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(OPUS) -MD -MP -c $< -o $@

clean:
	@rm -f $(CAPTURE) $(KMODEL) $(HIST) $(STREAM) $(AGENT)

distclean: clean
	@rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
