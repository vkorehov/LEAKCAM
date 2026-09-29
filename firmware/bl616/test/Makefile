# Host tests (no SDK needed): BL616 link parser vs the K230 agent's framing, AHT20 arithmetic,
# always-on state (clock across the RTC wrap, counter reset, CRC), and wifi_link.c, unchanged,
# against the SDK stand-ins in stub/ plus the layout of wifi_ctrl_proto.h.
#   make -C firmware/bl616/test
all: test_link test_aht20 test_aon test_wifi_link

test_link: test_link.c ../k230_link.c stub/bflb_uart.h
	$(CC) -Wall -Wextra -Werror -Istub -I.. -o $@ test_link.c ../k230_link.c
	./$@

test_aht20: test_aht20.c ../aht20_calc.h
	$(CC) -Wall -Wextra -Werror -I.. -o $@ test_aht20.c
	./$@

test_aon: test_aon.c ../aon_state_calc.h
	$(CC) -Wall -Wextra -Werror -I.. -o $@ test_aon.c
	./$@

test_wifi_link: test_wifi_link.c ../wifi_link.c ../wifi_link.h ../wifi_ctrl_proto.h ../board_pins.h $(wildcard stub/*.h stub/lwip/*.h stub/lwip/prot/*.h)
	$(CC) -O2 -Wall -Wextra -Werror -Istub -I.. -o $@ test_wifi_link.c ../wifi_link.c
	./$@

clean:
	rm -f test_link test_aht20 test_aon test_wifi_link

.PHONY: all clean
