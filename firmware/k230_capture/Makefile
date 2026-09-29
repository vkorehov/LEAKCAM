# Host unit tests (no camera needed): make test. The device build is rtsmart/Makefile, run by the
# SDK after firmware/k230_board/install.sh. History files are compressed with the bundled miniz,
# as on the device.
MINIZ := -Ithird_party/miniz third_party/miniz/miniz.c
CORE  := imgdiff.c refstore.c history.c
HDRS  := imgdiff.h refstore.h history.h cap.h

test: test/test_imgdiff test/test_history test/test_imgqual_synth test/test_flv test/leakcam_wake_host test/fake_stream
	./test/test_imgdiff
	./test/test_history
	./test/test_imgqual_synth
	./test/test_flv test/sample.h264 $(WAKE_STATE).flv
	python3 test/test_flv.py $(WAKE_STATE).flv
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

test/fake_stream: test/fake_stream.c netclient.c netclient.h flv.c flv.h test/sample.h264
	cc -O2 -Wall -Wextra -Werror -I. -DSAMPLE_H264='"$(CURDIR)/test/sample.h264"' -o $@ test/fake_stream.c \
	   netclient.c flv.c -lm

# image quality checks and LED step on synthetic frames
test/test_imgqual_synth: test/test_imgqual_synth.c imgqual.c imgqual.h imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual_synth.c imgqual.c -lm

# the clip's FLV (H.264 + G.711): mu-law values here, container and decode by ffprobe/ffmpeg
test/test_flv: test/test_flv.c flv.c flv.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_flv.c flv.c -lm

# measurement tool, needs leakcam PGM pairs (firmware/k230_nn/README.md section 5): not part of `test`
test/test_imgqual: test/test_imgqual.c imgqual.c imgqual.h imgdiff.c imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual.c imgqual.c imgdiff.c -lm

clean:
	rm -f test/test_imgdiff test/test_history test/test_imgqual test/test_imgqual_synth test/test_flv \
	      test/leakcam_wake_host test/fake_stream

.PHONY: test clean
