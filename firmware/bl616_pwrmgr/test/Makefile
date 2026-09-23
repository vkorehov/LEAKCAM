# Host test of the BL616 frame parser against the K230 agent's framing (no SDK needed).
#   make -C firmware/bl616_pwrmgr/test
test_link: test_link.c ../k230_link.c stub/bflb_uart.h
	$(CC) -Wall -Wextra -Werror -Istub -I.. -o $@ test_link.c ../k230_link.c
	./$@

clean:
	rm -f test_link

.PHONY: clean
