# Host tests (no SDK needed): BL616 link parser vs the K230 agent's framing, AHT20 arithmetic,
# always-on state (clock across the RTC wrap, counter reset, CRC).
#   make -C firmware/bl616_pwrmgr/test
all: test_link test_aht20 test_aon

test_link: test_link.c ../k230_link.c stub/bflb_uart.h
	$(CC) -Wall -Wextra -Werror -Istub -I.. -o $@ test_link.c ../k230_link.c
	./$@

test_aht20: test_aht20.c ../aht20_calc.h
	$(CC) -Wall -Wextra -Werror -I.. -o $@ test_aht20.c
	./$@

test_aon: test_aon.c ../aon_state_calc.h
	$(CC) -Wall -Wextra -Werror -I.. -o $@ test_aon.c
	./$@

clean:
	rm -f test_link test_aht20 test_aon

.PHONY: all clean
