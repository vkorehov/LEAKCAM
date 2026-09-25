# Cross-compile with the k230_sdk Linux toolchain, e.g.
#   make CROSS_COMPILE=riscv64-unknown-linux-gnu-
# Host test (no hardware):  make test
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror
LDLIBS  += -lpthread

# the link protocol constants are shared with the BL616 firmware
CFLAGS  += -I../bl616_pwrmgr

leakcam_agent: leakcam_agent.c ../bl616_pwrmgr/k230_link.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# host test: this agent's link code against the BL616's, over a lossy socket pair
test:
	$(MAKE) -C test

clean:
	rm -f leakcam_agent
	$(MAKE) -C test clean

.PHONY: test clean
