# Cross-compile with the k230_sdk Linux toolchain, e.g.
#   make CROSS_COMPILE=riscv64-unknown-linux-gnu-
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror
LDLIBS  += -lpthread

leakcam_agent: leakcam_agent.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f leakcam_agent

.PHONY: clean
