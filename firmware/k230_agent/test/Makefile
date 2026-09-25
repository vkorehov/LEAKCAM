# Host test: the agent's link code against the BL616's k230_link.c over a lossy socket pair.
#   make -C firmware/k230_agent/test
BL := ../../bl616_pwrmgr
all: test_link_e2e

test_link_e2e: test_link_e2e.c ../leakcam_agent.c $(BL)/k230_link.c $(BL)/k230_link.h
	$(CC) -Wall -Wextra -Werror -I$(BL) -I$(BL)/test/stub -o $@ test_link_e2e.c $(BL)/k230_link.c -lpthread
	./$@

clean:
	rm -f test_link_e2e

.PHONY: all clean
