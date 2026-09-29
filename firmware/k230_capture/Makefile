# Host unit tests (no camera needed): make test. The device build is rtsmart/Makefile, run by the
# SDK after firmware/k230_board/install.sh. History files are compressed with the bundled miniz,
# as on the device.
MINIZ := -Ithird_party/miniz third_party/miniz/miniz.c
CORE  := imgdiff.c refstore.c history.c
HDRS  := imgdiff.h refstore.h history.h cap.h

test: test/test_imgdiff test/test_history test/test_imgqual_synth test/test_audio test/leakcam_wake_host test/fake_stream
	./test/test_imgdiff
	./test/test_history
	./test/test_imgqual_synth
	./test/test_audio
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

test/fake_stream: test/fake_stream.c netclient.c netclient.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/fake_stream.c netclient.c

# image quality checks and LED step on synthetic frames
test/test_imgqual_synth: test/test_imgqual_synth.c imgqual.c imgqual.h imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual_synth.c imgqual.c -lm

# leakcam_audio.c included unchanged, MPP stand-ins in test/mpp_stub
test/test_audio: test/test_audio.c leakcam_audio.c $(wildcard test/mpp_stub/*.h)
	cc -O2 -Wall -Wextra -Werror -I. -Itest/mpp_stub -o $@ test/test_audio.c -lm

# measurement tool, needs leakcam PGM pairs (firmware/k230_nn/README.md section 5): not part of `test`
test/test_imgqual: test/test_imgqual.c imgqual.c imgqual.h imgdiff.c imgdiff.h
	cc -O2 -Wall -Wextra -Werror -I. -o $@ test/test_imgqual.c imgqual.c imgdiff.c -lm

clean:
	rm -f test/test_imgdiff test/test_history test/test_imgqual test/test_imgqual_synth test/test_audio \
	      test/leakcam_wake_host test/fake_stream

.PHONY: test clean
