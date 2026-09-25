# Cross-compile with the k230_linux_sdk toolchain, e.g.
#   make CROSS_COMPILE=riscv64-unknown-linux-gnu-
# (zlib is in the SDK rootfs; BR2_PACKAGE_PYTHON3_ZLIB selects it)
# Host unit tests (no camera needed):  make test
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror
LDLIBS  += -lz -lm

CORE := imgdiff.c refstore.c history.c
HDRS := imgdiff.h refstore.h history.h cap.h

all: leakcam_capture leakcam_hist

leakcam_capture: leakcam_capture.c v4l2cap.c led_linux.c $(CORE) $(HDRS) led.h
	$(CC) $(CFLAGS) -o $@ leakcam_capture.c v4l2cap.c led_linux.c $(CORE) $(LDLIBS)

leakcam_hist: leakcam_hist.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ leakcam_hist.c $(CORE) $(LDLIBS)

test: test/test_imgdiff test/test_history test/test_history_miniz test/test_imgqual_synth test/test_audio
	./test/test_imgdiff
	./test/test_history
	./test/test_history_miniz
	./test/test_imgqual_synth
	./test/test_audio

test/test_imgdiff: test/test_imgdiff.c $(CORE) $(HDRS)
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgdiff.c $(CORE) -lz -lm

test/test_history: test/test_history.c $(CORE) $(HDRS)
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_history.c $(CORE) -lz -lm

# the RT-Smart build compresses with the bundled miniz instead of zlib: same tests, same format
test/test_history_miniz: test/test_history.c $(CORE) $(HDRS) third_party/miniz/miniz.c
	cc -O2 -Wall -Wextra -Werror -I. -Ithird_party/miniz -DLEAKCAM_MINIZ -o $@ test/test_history.c $(CORE) third_party/miniz/miniz.c -lm

# image quality verdicts and LED step on synthetic frames
test/test_imgqual_synth: test/test_imgqual_synth.c imgqual.c imgqual.h imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual_synth.c imgqual.c -lm

# leakcam_audio.c included unchanged, MPP stand-ins in test/mpp_stub
test/test_audio: test/test_audio.c leakcam_audio.c $(wildcard test/mpp_stub/*.h)
	cc -O2 -Wall -Wextra -Werror -I. -Itest/mpp_stub -o $@ test/test_audio.c -lm

# measurement tool, needs leakcam PGM pairs (firmware/k230_nn/README.md section 5): not part of `test`
test/test_imgqual: test/test_imgqual.c imgqual.c imgqual.h imgdiff.c imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual.c imgqual.c imgdiff.c -lm

clean:
	rm -f leakcam_capture leakcam_hist test/test_imgdiff test/test_history test/test_history_miniz test/test_imgqual \
	      test/test_imgqual_synth test/test_audio

.PHONY: all test clean
