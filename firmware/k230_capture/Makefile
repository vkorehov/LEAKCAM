# Cross-compile with the k230_linux_sdk toolchain, e.g.
#   make CROSS_COMPILE=riscv64-unknown-linux-gnu-
# (zlib is in the SDK rootfs; BR2_PACKAGE_PYTHON3_ZLIB selects it)
# Host unit tests (no camera needed):  make test
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror
LDLIBS  += -lz -lm

CORE := imgdiff.c refstore.c history.c
HDRS := imgdiff.h refstore.h history.h v4l2cap.h

all: leakcam_capture leakcam_hist

leakcam_capture: leakcam_capture.c v4l2cap.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ leakcam_capture.c v4l2cap.c $(CORE) $(LDLIBS)

leakcam_hist: leakcam_hist.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ leakcam_hist.c $(CORE) $(LDLIBS)

test: test/test_imgdiff test/test_history
	./test/test_imgdiff
	./test/test_history

test/test_imgdiff: test/test_imgdiff.c $(CORE) $(HDRS)
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgdiff.c $(CORE) -lz -lm

test/test_history: test/test_history.c $(CORE) $(HDRS)
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_history.c $(CORE) -lz -lm

clean:
	rm -f leakcam_capture leakcam_hist test/test_imgdiff test/test_history

.PHONY: all test clean
