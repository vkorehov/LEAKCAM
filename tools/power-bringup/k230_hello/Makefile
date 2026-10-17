# LEAKCAM K230 hello: a status line per second on the debug UART, run from SRAM over USB (README in ..).
#   make            build k230_hello.bin (in the K230 build container: the T-Head toolchain is x86-64)
#   make run        load and start it through the boot ROM (K230 in USB boot mode, 29f1:0230)
TOOLCHAIN ?= $(abspath ../../../../toolchain_gcc_t-head_linux)
FLASH_BIN ?= $(HOME)/k230_rtos_sdk/tools/k230_flash/bin
IMAGE ?= k230-rtos-sdk-build:arm64-x86tc
TOP := $(abspath ../../../..)
CC := $(TOOLCHAIN)/bin/riscv64-unknown-elf-gcc
OBJCOPY := $(TOOLCHAIN)/bin/riscv64-unknown-elf-objcopy
DOCKER := docker run --rm --user $$(id -u):$$(id -g) -v $(TOP):$(TOP) -w $(CURDIR) $(IMAGE)

k230_hello.bin: start.S hello.c link.ld
	$(DOCKER) $(CC) -march=rv64gc -mabi=lp64d -mcmodel=medany -O2 -nostdlib -ffreestanding -fno-builtin \
	    -T link.ld -o k230_hello.elf start.S hello.c
	$(DOCKER) $(OBJCOPY) -O binary k230_hello.elf k230_hello.bin
	@ls -l k230_hello.bin

run: k230_hello.bin
	docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb -v $(FLASH_BIN):$(FLASH_BIN) -v $(CURDIR):$(CURDIR) \
	    -w $(CURDIR) -e LD_LIBRARY_PATH=$(FLASH_BIN) $(IMAGE) timeout 4 $(FLASH_BIN)/k230_flash_cli -m SPI_NAND \
	    --custom-loader --loader-file $(CURDIR)/k230_hello.bin --load-address 0x80360000 \
	    --read-data --read-size 16 -f /tmp/unused.bin || true    # loads and starts it; the read then times out

clean:
	rm -f k230_hello.elf k230_hello.bin
