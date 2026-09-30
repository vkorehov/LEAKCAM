# Host unit tests (no camera needed): make test. The device build is rtsmart/Makefile, run by the
# SDK after firmware/k230_board/install.sh. History files are compressed with the bundled miniz,
# as on the device.
MINIZ := -Ithird_party/miniz third_party/miniz/miniz.c

# libopus 1.5.2 (third_party/opus, BSD), float build, from its own source lists
OPUS_DIR := third_party/opus
include $(OPUS_DIR)/celt_sources.mk $(OPUS_DIR)/silk_sources.mk $(OPUS_DIR)/opus_sources.mk
OPUS_SRC := $(addprefix $(OPUS_DIR)/,$(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FLOAT) \
            $(OPUS_SOURCES) $(OPUS_SOURCES_FLOAT))
OPUS_DEFS := -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINTF -DHAVE_LRINT
OPUS_INC := -I$(OPUS_DIR)/include -I$(OPUS_DIR)/celt -I$(OPUS_DIR)/silk -I$(OPUS_DIR)/silk/float
CORE  := imgdiff.c refstore.c history.c
HDRS  := imgdiff.h refstore.h history.h cap.h

test: test/test_imgdiff test/test_history test/test_imgqual_synth test/test_rtmp test/leakcam_wake_host test/fake_stream
	./test/test_imgdiff
	./test/test_history
	./test/test_imgqual_synth
	python3 test/test_rtmp.py test/test_rtmp test/sample.h265 $(WAKE_STATE).flv   # FFMPEG_BIN: FFmpeg 7.1+
	python3 test/test_wake.py test/leakcam_wake_host $(WAKE_STATE)

test/test_imgdiff: test/test_imgdiff.c $(CORE) $(HDRS)
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgdiff.c $(CORE) $(MINIZ) -lm

test/test_history: test/test_history.c $(CORE) $(HDRS)
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_history.c $(CORE) $(MINIZ) -lm

# the wake algorithm: leakcam_wake unchanged, cameras and LEDs from test/wake_stub.c, run by
# test/test_wake.py against mock_server.py; test/fake_stream stands in for leakcam_stream -p
WAKE_STATE := /tmp/leakcam_wake_test
test/leakcam_wake_host: leakcam_wake.c netclient.c netclient.h test/wake_stub.c $(CORE) $(HDRS) led.h
	cc -O2 -Wall -Wextra -Werror -I. -DSTATE_DIR='"$(WAKE_STATE)"' -DSTREAM_PROG='"$(CURDIR)/test/fake_stream"' \
	   -DNET_TIMEOUT_S=2 -o $@ leakcam_wake.c netclient.c test/wake_stub.c $(CORE) $(MINIZ) -lm

test/fake_stream: test/fake_stream.c
	cc -O2 -Wall -Wextra -Werror -DFAKE_STREAM_LOG='"$(WAKE_STATE).stream"' -o $@ test/fake_stream.c

# image quality checks and LED step on synthetic frames
test/test_imgqual_synth: test/test_imgqual_synth.c imgqual.c imgqual.h imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual_synth.c imgqual.c -lm

# the live clip: Enhanced RTMP v2 publish of H.265 + Opus to `ffmpeg -listen`, checked by ffprobe/ffmpeg
test/test_rtmp: test/test_rtmp.c rtmp.c rtmp.h netclient.c netclient.h test/libopus.a
	cc -O2 -Wall -Wextra -Werror -I. $(OPUS_INC) -o $@ test/test_rtmp.c rtmp.c netclient.c test/libopus.a -lm

test/libopus.a: $(OPUS_SRC)
	rm -rf test/opus_obj && mkdir -p test/opus_obj
	cd test/opus_obj && cc -O2 -w $(OPUS_DEFS) $(addprefix -I../../,$(OPUS_INC:-I%=%)) -c $(addprefix ../../,$(OPUS_SRC))
	ar rcs $@ test/opus_obj/*.o && rm -rf test/opus_obj

# measurement tool, needs leakcam PGM pairs (firmware/k230_nn/README.md section 5): not part of `test`
test/test_imgqual: test/test_imgqual.c imgqual.c imgqual.h imgdiff.c imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual.c imgqual.c imgdiff.c -lm

clean:
	rm -f test/test_imgdiff test/test_history test/test_imgqual test/test_imgqual_synth test/test_rtmp \
	      test/leakcam_wake_host test/fake_stream test/libopus.a

.PHONY: test clean
