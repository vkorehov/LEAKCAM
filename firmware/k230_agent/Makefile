# leakcam_agent runs on the K230 under RT-Smart: the SDK builds it into the image with the other
# leakcam programs (k230_capture/rtsmart/Makefile, after k230_board/install.sh).
# Host test (no hardware): this agent's link code against the BL616's, over a lossy socket pair.
test:
	$(MAKE) -C test

clean:
	$(MAKE) -C test clean

.PHONY: test clean
