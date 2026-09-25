# Host test for the BL616 side of the Wi-Fi link: wifi_link.c, unchanged, against the SDK
# stand-ins in stub/ (no Bouffalo SDK needed), plus the layout of wifi_ctrl_proto.h.
#   make -C firmware/bl616_wifi/test
all test: test_wifi_link
	./test_wifi_link

test_wifi_link: test_wifi_link.c ../wifi_link.c ../wifi_link.h ../wifi_ctrl_proto.h ../../bl616_pwrmgr/board_pins.h $(wildcard stub/*.h stub/lwip/*.h stub/lwip/prot/*.h)
	$(CC) -O2 -Wall -Wextra -Werror -Istub -I.. -I../../bl616_pwrmgr -o $@ test_wifi_link.c ../wifi_link.c

clean:
	rm -f test_wifi_link

.PHONY: all test clean
