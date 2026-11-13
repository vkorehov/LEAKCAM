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
# the microphone's Opus (audio.c): libopus 1.5.2 (third_party/opus, BSD), float build, from its own source lists, as
# tests/firmware/k230_apps/rtsmart/Makefile (leakcam_stream, tests/test012)
OPUS_DIR := third_party/opus
include $(SRC_ROOT)/$(OPUS_DIR)/celt_sources.mk $(SRC_ROOT)/$(OPUS_DIR)/silk_sources.mk \
        $(SRC_ROOT)/$(OPUS_DIR)/opus_sources.mk
OPUS_SRC := $(addprefix $(OPUS_DIR)/,$(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FLOAT) \
            $(OPUS_SOURCES) $(OPUS_SOURCES_FLOAT))
OPUS     := -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINTF -DHAVE_LRINT \
            $(addprefix -I$(SRC_ROOT)/$(OPUS_DIR)/,include celt silk silk/float)
OBJS     += $(addprefix $(BUILD)/,$(OPUS_SRC:.c=.o))

# the cameras (vicap_cap.c, 2026-11-12): MPP's headers and libraries, as tests/firmware/k230_apps/rtsmart/Makefile
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

CFLAGS   := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -std=gnu11 -I$(SRC_ROOT)
CFLAGS   += $(LIB_CFLAGS) -I$(SDK_SRC_ROOT_DIR)/include -include "generated/autoconf.h"
CFLAGS   += -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS  := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections $(LIB_LDFLAGS)
# the raw frames packed (rawpack.c, firmware/VIDEO.txt): the ISP library's register writes through our wrappers; the
# map for reading the program's addresses
LDFLAGS  += -Wl,--wrap=VirtualHalWriteReg -Wl,--wrap=SwitchUpdateCommonRegister -Wl,--wrap=VsiCamDeviceGetBufferSize
LDFLAGS  += -Wl,-Map=$(BUILD)/leakcam.map
.DEFAULT_GOAL := all
# ... and libisp's local VIHalWriteReg replaced (rawpack.c): a copy of libisp.a with it weak + global, found first
ISP_LIB  := $(firstword $(wildcard $(addsuffix /libisp.a,$(lib_mpp_lib_dir))))
PATCHED  := $(BUILD)/patched
LDFLAGS  := -L$(PATCHED) $(LDFLAGS)

$(PATCHED)/libisp.a: $(ISP_LIB) $(CURDIR)/Makefile
	@mkdir -p $(PATCHED)/obj
	cp $< $@
	cd $(PATCHED)/obj && $(AR) x $< isp_mcm.o && \
	  $(OBJCOPY) --globalize-symbol=VIHalWriteReg isp_mcm.o && \
	  $(OBJCOPY) --weaken-symbol=VIHalWriteReg isp_mcm.o && \
	  $(AR) r $@ isp_mcm.o

.PHONY: all clean distclean
all: $(ROM_C)
	@$(ECHO) [BUILD] applications leakcam done.

$(ROM_C): $(PROG) $(CURDIR)/rom_c.py
	$(Q)python3 $(CURDIR)/rom_c.py $(PROG) $@

$(PROG): $(OBJS) $(PATCHED)/libisp.a
	@echo "[LD] $@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -lm -o $@
	$(Q)$(STRIP) $@

$(BUILD)/$(OPUS_DIR)/%.o: $(SRC_ROOT)/$(OPUS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(OPUS) -w -c $< -o $@

$(BUILD)/%.o: $(SRC_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -I$(SRC_ROOT)/$(OPUS_DIR)/include -c $< -o $@

clean:
	$(Q)rm -rf $(BUILD)
	$(Q)python3 $(CURDIR)/rom_c.py - $(ROM_C)

distclean: clean
