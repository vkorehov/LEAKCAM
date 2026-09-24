# LEAKCAM firmware: build and flash, from an empty machine

The board carries two independent firmwares:

| Chip | Firmware | Source here | SDK | Flashed through |
|---|---|---|---|---|
| K230D (U3) | SPL + RT-Smart kernel + `leakcam_capture` / `leakcam_hist`, one `.kdimg` for the SPI NAND (U16 W25N02KV) | `k230_board/`, `k230_capture/` | `kendryte/k230_rtos_sdk` (CanMV manifest) | USB-C 1, K230 boot ROM USB mode, `k230_flash` |
| BL616 (U11 Ai-M62-CBS) | power manager, leak/humidity wake, BLE provisioning | `bl616_pwrmgr/` | `bouffalolab/bouffalo_sdk` | PR1 pads (USB D+/D-, BOOT, EN), `BLFlashCommand` |

Flash the BL616 first: it owns the K230's power (K230_PWR, K230_RSTN), and a blank BL616 keeps the
K230 off.

There is one build of each, with no variants. The K230 image boots in one way (ROM -> SPL ->
RT-Smart, described below).

## 1. Host

The build host is Linux on **arm64** (the reference machine: Ubuntu 24.04 aarch64, 5 cores). An
x86-64 Linux host also works, with the same steps.

- **Docker**, with your user in the `docker` group.
- **qemu binfmt for x86-64**, arm64 hosts only: `sudo apt install qemu-user-static binfmt-support`.
  Check that `/proc/sys/fs/binfmt_misc/qemu-x86_64` exists and its flags include `F`. The SDK's RISC-V
  toolchains and prebuilt host tools exist only as x86-64 binaries.
- **git, python3, curl.**
- **About 25 GB of disk**: SDK checkout 9 GB, toolchains 3 GB, one build output 10 GB.

## 2. K230: RT-Smart image

### 2.1 Get the SDK (once)

```
mkdir -p ~/k230_rtos_sdk && cd ~/k230_rtos_sdk
curl -fsSL https://raw.githubusercontent.com/canmv-k230/git-repo/main/repo -o ~/bin/repo && chmod +x ~/bin/repo
repo init -u https://github.com/canmv-k230/manifest -b master --repo-url=https://github.com/canmv-k230/git-repo.git \
     -g sdk_super,sdk,sdk-common,sdk-special --depth=1 --no-clone-bundle
repo sync -c --no-tags -j4
```

Tested revisions (2026-09-24):

| Tree | Revision |
|---|---|
| k230_rtos_sdk | 4162ec7 |
| rtsmart | 6cb12a8 |
| u-boot | c096dae |
| mpp | 2db9fbf |

A newer sync can move files that `install.sh` patches. If it complains, check out these
revisions.

### 2.2 Build container (once)

```
LEAKCAM=~/k230d-hw/LEAKCAM            # this repository
cp $LEAKCAM/firmware/k230_board/docker/leakcam_docker_build.sh ~/k230_rtos_sdk/
mkdir -p ~/k230_rtos_sdk/leakcam_docker && cp $LEAKCAM/firmware/k230_board/docker/Dockerfile ~/k230_rtos_sdk/leakcam_docker/
cd ~/k230_rtos_sdk
./leakcam_docker_build.sh dl_toolchain      # builds the image on first use, then installs the toolchains
```

- **The image.** `k230-rtos-sdk-build:arm64-x86tc` is native arm64 Debian with amd64 runtime
  libraries. make, python, scons and bison run natively; the x86-64 toolchains run through qemu.
  A plain `linux/amd64` container does not work under qemu-user: bison/m4 crash.
- **The toolchains.** They go to `~/.kendryte/k230_toolchains` on the host, which is bind-mounted
  into the container, so they survive image rebuilds.
- **Running commands.** `./leakcam_docker_build.sh --shell` opens a shell in the container, and
  `--rebuild-image` rebuilds the image.

### 2.3 Install the LEAKCAM board into the SDK

```
$LEAKCAM/firmware/k230_board/install.sh ~/k230_rtos_sdk
```

**Rerun it after every change in `k230_board/` or `k230_capture/`.** It is idempotent. It does
the following:

1. **Pin files.** Runs `pins.py`, which regenerates the RT-Smart pin mux and the U-Boot pin
   `.dtsi` from the one pin table.
2. **Board files.** Copies the board folder, the defconfigs, the U-Boot device tree and the app
   (`k230_capture/rtsmart` -> `src/applications/leakcam`).
3. **Board registration.** Adds the board to `boards/Kconfig`, the dtb to U-Boot's dts Makefile
   and the app to `apps.mk`.
4. **UFFS patch.** Patches the RT-Smart NAND glue so UFFS gets 64 spare bytes; see
   `k230_board/README.md`.
5. **Stale object.** Deletes the stale `pinmux.o`, which scons does not rebuild when the pin table
   changes.

### 2.4 Build

```
cd ~/k230_rtos_sdk
./leakcam_docker_build.sh k230d_rtos_leakcam_defconfig     # select the board (once, or after install.sh changed the defconfig)
./leakcam_docker_build.sh log                              # full build, log in log.txt
```

- **Time.** A clean build takes about 4 minutes on the 5-core arm64 host; a rebuild after an app
  change takes about 1 minute.
- **Exit status.** `log` exits with make's status, unlike the SDK's own `make log`, which always
  exits 0.

Output in `output/k230d_rtos_leakcam_defconfig/`:

| File | What |
|---|---|
| `LEAKCAM_K230D_rtsmart_local_nncase_v2.11.0.kdimg` (78 MB) | the image to flash |
| `...kdimg.gz` | same, compressed |
| `uboot/spl/u-boot-spl.bin`, `uboot/u-boot.bin` | SPL and U-Boot, for the pad check |
| `opensbi/opensbi_rtt_system.bin` | OpenSBI + RT-Smart kernel, what slots A and B hold |
| `bin.uffs`, `sdcard.uffs` | the two UFFS filesystems |

### 2.5 Check the image before flashing

```
python3 $LEAKCAM/firmware/k230_board/check_pad_voltage.py \
    output/k230d_rtos_leakcam_defconfig/uboot/spl/u-boot-spl.bin output/k230d_rtos_leakcam_defconfig/uboot/u-boot.bin
```

**Both must say `64 pads checked, OK`.** The check decodes the pad registers (MSC = 1.8 V/3.3 V
mode) from the device trees inside the binaries and compares them with the bank voltages of the
board.

**Never flash an SPL that fails this check.** Every LEAKCAM bank is 3.3 V except the fixed 1.8 V
IO0/IO1, and a pad set to 1.8 V mode on a 3.3 V bank can damage the chip. Canaan's prebuilt EVB
SPI-NAND SPL fails it on 50 of 64 pads, which is why the port builds SPL and U-Boot from source.

### 2.6 What is in the image (NAND layout, 256 MiB)

| Offset | Size | Partition | Contents |
|---|---|---|---|
| 0 | 512K | spl_a | SPL, read by the boot ROM |
| 512K | 384K | spl_b | SPL copy; the ROM tries it if spl_a is bad |
| 896K (0xe0000) | 128K | TOC | partition table the SPL reads (below) |
| 1M | 256K | ota_meta | A/B slot records, two copies (empty = boot slot A) |
| 2M | 2M | uboot | U-Boot, fallback only |
| 4M | 512K | uboot_env | U-Boot environment |
| 5M | 20M | rtt_a | OpenSBI + RT-Smart, slot A |
| 25M | 20M | rtt_b | the same, slot B |
| 48M | 20M | bin | UFFS `/bin`: ISP tuning files, SDK tools |
| 72M | 176M | sdcard | UFFS `/sdcard`: `/sdcard/app/leakcam_*`, image history (`/sdcard/leakcam`) |
| 248M | 8M | (free) | bad-block reserve |

**Boot:**
1. **Boot ROM.** Straps BOOT0 = 1, BOOT1 = 0 select SPI NAND. The ROM loads the SPL into SRAM.
2. **SPL.** Trains DDR, sets the pad mux and voltages, then reads the TOC. It picks the slot from
   `ota_meta` (A when that is empty), loads the image from `rtt_a` or `rtt_b`, and starts it on
   the big core. If that slot does not load, it tries the other one.
3. **OpenSBI, then RT-Smart.** The kernel mounts `/bin` and `/sdcard`.
4. **U-Boot fallback.** U-Boot runs only if the TOC is missing or neither slot loads; it then
   boots slot A.

**TOC entries.** Each is 64 bytes: name, offset, size, `load`, `boot`. rtt_a and rtt_b carry
`load = 1`, `boot = 0x3` (bit 0 = boot this, bits 1-2 = core 1, the big core). The load address
comes from the image header inside the slot.

## 3. K230: flashing over USB

### 3.1 Tool

`k230_flash` ships with the SDK: `tools/k230_flash.py` wraps `tools/k230_flash/bin/k230_flash_cli`.
The CLI sends a small loader to the boot ROM over USB; the loader then writes the NAND.

- **Host requirements.** `k230_flash_cli` and its `libkburn.so` are **x86-64 only**. Flash from
  an x86-64 Linux PC, or from Windows with `k230_flash_cli.exe`. It is not tested through qemu
  on the arm64 build host.
- **Linux USB access.** The PC needs pyusb and libusb (`pip install pyusb pyserial`), plus a udev
  rule for the boot ROM device:
  ```
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="29f1", ATTR{idProduct}=="0230", MODE="0666"' | sudo tee /etc/udev/rules.d/99-k230.rules
  sudo udevadm control --reload
  ```
- **Windows USB access.** Install the WinUSB driver for the "K230 USB Boot Device" once with
  Zadig.

### 3.2 Getting the K230 into USB mode

USB mode comes from the boot ROM itself. The ROM enters it when booting from the strapped medium
fails and it sees VBUS. **Only USB-C 1 carries the K230 USB data lines.** USB-C 2 is the CH340X
console.

| Situation | What to do |
|---|---|
| Blank NAND (first flash) | Plug USB-C 1 into the PC and power the K230. The NAND boot fails by itself and the ROM enumerates as `29f1:0230`. |
| NAND holds an image | Short test points **B1_P/B1_N** while the K230 powers up; release after it enumerates. That sets BOOT1 = 1, so the ROM tries MMC0, which is the BL616's SDIO and has no card. The boot fails and the ROM drops to USB. |
| Running RT-Smart | Not implemented yet. The plan is for the agent to ask the BL616 to power-cycle the K230 with BOOT1 driven high; this needs the rev 1 IO27 -> 10k -> BOOT1 resistor. |

**The K230 has power only when the BL616 turns it on.** A board whose BL616 is blank keeps the
K230 off and in reset (R19 pulls K230_PWR low, Q4 holds RSTN). So flash the BL616 first (section 4).

### 3.3 Flash

```
cd ~/k230_rtos_sdk/tools
python3 k230_flash.py --list-devices
python3 k230_flash.py -m SPI_NAND -f ../output/k230d_rtos_leakcam_defconfig/LEAKCAM_K230D_rtsmart_local_nncase_v2.11.0.kdimg
```

The CLI underneath is `k230_flash_cli -m SPI_NAND -f <image.kdimg> --auto-reboot`.

- **What it writes.** It writes every partition at its offset, including the TOC. The empty
  `ota_meta` means slot A.
- **Afterwards.** With `--auto-reboot` (the wrapper's default) the K230 restarts from the NAND.
  With the strapping back at 1/0, that happens by itself once B1 is released.

### 3.4 First boot, on the console

Plug in USB-C 2: the CH340X appears as `/dev/ttyUSB0`, 115200 8N1. The CH340X is powered from the
K230's 3V3, so it only appears while the K230 is on. Expect this order on the console:

1. SPL banner and DDR training.
2. `k230_read_toc`, then slot A loading.
3. The OpenSBI banner.
4. The RT-Smart `msh />` prompt.
5. `ls /sdcard/app` should list `leakcam_capture` and `leakcam_hist`.

**Not yet verified on hardware:**
- that the ROM reads the NAND on the 3.3 V bank;
- DDR training on this board;
- that the loader recognises the W25N02KV (JEDEC EF AA 22). If `k230_flash` stops at the NAND
  probe, the loader must be rebuilt from our U-Boot.

## 4. BL616: power manager

### 4.1 SDK and toolchain (once)

```
cd ~/k230d-hw                                            # next to LEAKCAM: the Makefile expects ../../../bouffalo_sdk
git clone --depth 1 https://github.com/bouffalolab/bouffalo_sdk.git
git clone --depth 1 https://github.com/bouffalolab/toolchain_gcc_t-head_linux.git
export PATH=$PWD/toolchain_gcc_t-head_linux/bin:$PATH    # riscv64-unknown-elf-gcc
```

The T-Head toolchain is x86-64 only: on the arm64 host it runs through qemu, like the K230 one.
The sources have been compiled against the SDK headers, but not yet linked into a firmware
binary; the first full `make` is still to be done.

### 4.2 Build

```
cd ~/k230d-hw/LEAKCAM/firmware/bl616_pwrmgr
make                         # CHIP=bl616 BOARD=bl616dk CROSS_COMPILE=riscv64-unknown-elf- are the defaults
```

The firmware lands in `build/build_out/leakcam_pwrmgr_bl616.bin`. `defconfig` moves the SDK
console to USB CDC, because GPIO21/22 are the K230 link.

### 4.3 Flash through PR1

PR1 is on the bottom side: pad 1 D+, pad 2 D-, pad 3 GND, pad 4 AI_BOOT, pad 5 AI_EN.

1. Power the board from the battery or USB-C 1; PR1 carries no supply.
2. Wire a USB cable's D+/D-/GND to pads 1-3.
3. Hold **AI_BOOT high**: PR1 has no 3.3 V pad, so use a wire to 3V3_SLEEP, for example the top of
   R38. Pulse **AI_EN low**, then release it.
4. The BL616 ROM enumerates as `Bouffalo CDC` (349b:6160), `/dev/ttyACM0`. Run:
   ```
   make flash COMX=/dev/ttyACM0        # BLFlashCommand --interface=uart --baudrate=2000000 --chipname=bl616
   ```
   The SDK picks `BLFlashCommand-arm` on aarch64 and `BLFlashCommand-ubuntu` on x86-64.
5. Release BOOT and pulse EN.

Rev 1 adds a sixth PR1 pad for 3V3_SLEEP, so a pogo fixture can drive BOOT.

## 5. Host tests (no hardware)

```
make -C firmware/k230_capture test          # change detector, image history (zlib and miniz builds)
make -C firmware/bl616_pwrmgr/test          # link parser, AHT20 maths, always-on clock/state
make -C firmware/k230_capture clean; make -C firmware/bl616_pwrmgr/test clean
```

Both pass as of 2026-09-24. Run them before every firmware commit.

## 6. Everyday loop

| Changed | Do |
|---|---|
| `k230_capture/*.c` | `install.sh`, `./leakcam_docker_build.sh log`, flash (or copy the two programs to `/sdcard/app` once networking works) |
| pin table (`k230_board/pins.py`) | the same; check `check_pad_voltage.py` again |
| kernel config (`k230_board/rtsmart/configs/`) | `install.sh`, `./leakcam_docker_build.sh k230d_rtos_leakcam_defconfig`, `log` |
| NAND layout | edit `genimage-spinand.cfg`, `spinand_parts.h` and the U-Boot dts partitions together: they must agree |
| BL616 | `make && make flash COMX=/dev/ttyACM0` |

## 7. Known build problems and their fixes

| Symptom | Cause | Fix (already in the port) |
|---|---|---|
| `mkuffs: Invalid spare size` | spare 128 in the layout; mkuffs accepts at most 64 | layout uses `spare-size = 64`, kernel patched to 64 |
| `#error "RTC PMU is not supported"` (drv_fpioa.h) | board lacks `BOARD_NOT_SUPPORT_HW_RTC` | set in `sdk/boards/k230d_leakcam/Kconfig` |
| undefined `board_specific_pin_init_sequence` | stale `pinmux.o` after a pin-table change | `install.sh` deletes it |
| missing `opensbi_fw_jump.bin` | prebuilt OpenSBI selected | OpenSBI built from source |
| undefined `RT_SYSTEM_WORKQUEUE_PRIORITY`, `tsensor_*`, `rt_adc_*` | SDIO/network or ADC/TS stripped from the kernel | kept on: the SDK's pm and audio code need them, Wi-Fi needs SDIO |
| `repo: command not found` during `gen_image.sh` | image lacks the repo launcher | Dockerfile installs it |
| bison / m4 `QEMU internal SIGSEGV` | amd64 container under qemu | native arm64 container |
