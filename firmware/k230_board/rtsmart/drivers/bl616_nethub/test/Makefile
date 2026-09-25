# Host tests for the BL616 NetHub driver: the three driver sources, unchanged, against the
# RT-Thread stand-ins in stub/ and a simulated BL616 SDU (no SDK, no hardware).
#   make -C firmware/k230_board/rtsmart/drivers/bl616_nethub/test
DRV   := ..
PROTO := ../../../../../bl616_wifi
SRCS  := $(DRV)/bl616_sdio.c $(DRV)/bl616_msg.c $(DRV)/bl616_wifi.c host_rt.c sim_bl616.c test_nethub.c
HDRS  := $(DRV)/bl616_nethub.h $(PROTO)/wifi_ctrl_proto.h host.h $(wildcard stub/*.h stub/drivers/*.h)

all test: test_nethub
	./test_nethub

test_nethub: $(SRCS) $(HDRS)
	$(CC) -O2 -Wall -Wextra -Werror -Istub -I$(DRV) -I$(PROTO) -I. -o $@ $(SRCS) -lpthread

clean:
	rm -f test_nethub

.PHONY: all test clean
