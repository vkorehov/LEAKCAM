# TEMPORARY (tests/test027): copy this folder to <SDK>/src/applications/venctest and add
# "subdirs-y += venctest" to src/applications/apps.mk; remove both after the test.
# helloworld's Makefile, cut down to libmpp (VB, VENC).
include ../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

BUILD   := $(SDK_APPS_BUILD_DIR)/venctest
BIN     := $(SDK_APPS_IMAGE_DIR)/venctest
CFLAGS  := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -O2 -std=gnu11 -Wall -Wextra
CFLAGS  += $(LIB_CFLAGS) -I$(SDK_SRC_ROOT_DIR)/include -include "generated/autoconf.h"
LDFLAGS := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static $(LIB_LDFLAGS) -lpthread

.PHONY: all clean distclean
all: $(BIN)
	@$(ECHO) [BUILD] applications venctest done.

$(BIN): venctest.c
	@mkdir -p $(dir $@) $(BUILD)
	$(Q)$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@
	$(Q)$(STRIP) $@

clean:
	@rm -f $(BIN)

distclean: clean
