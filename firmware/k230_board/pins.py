#!/usr/bin/env python3
"""
LEAKCAM K230D pin table: the one source for the RT-Smart pin mux and the U-Boot device tree.

    python3 pins.py            # rewrites rtsmart/board/k230d_leakcam/pinmux_config.c and
                               # uboot/dts/k230d_leakcam_pins.dtsi next to this file

Every VDDIO bank is on 3V3 on this board (LEAKCAM.SchDoc: VDDIO_BANK0..5 all on net 3V3), so
every bank is declared 3.3 V (MSC = 0). IO0/IO1 (BOOT0/1) sit in the fixed 1.8 V bank; the
straps R64 (BOOT0 -> 1V8) and R31 (BOOT1 -> GND) are external, so no internal pulls there.
A wrong MSC is not cosmetic: Canaan's own device trees warn it "will damage the chip".

Fields per pin: sel (function select), ie, oe, pu, pd, ds (drive 0..15), st (Schmitt), note.
Function numbers are the column in drv_fpioa.c's pin table: { GPIOn, func1, func2, func3, func4 }.
Pins IO9, IO42, IO43, IO46, IO47, IO50-IO58 are not bonded out on the K230D; they keep the
values of Canaan's K230D boards (U-Boot handles them in k230d.dtsi drop_pins).
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# pin: (sel, ie, oe, pu, pd, ds, st, note)
NC_IN = (0, 1, 0, 0, 1, 4, 1)          # not connected: GPIO input, pulled down, never floating
PINS = {
    0:  (0, 1, 0, 0, 0, 2, 0, "BOOT0 strap (R64 10k to 1V8), read by the ROM"),
    1:  (0, 1, 0, 0, 0, 2, 0, "BOOT1 strap (R31 10k to GND; BL616 IO27 via RBT1 forces 1 for USB boot)"),
    2:  (0, 1, 1, 0, 0, 4, 0, "GPIO2 K230_ALIVE heartbeat to BL616 IO01 (reset function is JTAG_TCK)"),
    3:  NC_IN + ("NC",), 4: NC_IN + ("NC",), 5: NC_IN + ("NC",), 6: NC_IN + ("NC",),
    7:  (2, 1, 1, 1, 0, 7, 1, "IIC4_SCL, CAM1 (J5, CSI2), R14/R15 4.7k to 3V3"),
    8:  (2, 1, 1, 1, 0, 7, 1, "IIC4_SDA, CAM1 (J5, CSI2)"),
    10: NC_IN + ("NC",), 11: NC_IN + ("NC",), 12: NC_IN + ("NC",), 13: NC_IN + ("NC",),
    # OSPI drive 7 of 15, not Canaan's 15: the NAND traces are 12-20 mm (under 130 ps one way) with no
    # room for series resistors at the balls (only SPI_CLK has R42), so a weaker driver keeps the
    # edges from ringing; 50 MHz quad needs nowhere near full strength (layout audit 2026-09-24).
    14: (1, 0, 1, 1, 0, 7, 1, "OSPI_CS  -> U16 W25N02KV CS# (R39 pull-up)"),
    15: (1, 0, 1, 0, 0, 7, 1, "OSPI_CLK -> U16 CLK via R42 22R"),
    16: (1, 1, 1, 0, 0, 7, 1, "OSPI_D0  <> U16 DI/IO0"),
    17: (1, 1, 1, 0, 0, 7, 1, "OSPI_D1  <> U16 DO/IO1"),
    18: (1, 1, 1, 0, 0, 7, 1, "OSPI_D2  <> U16 WP#/IO2 (R40 pull-up)"),
    19: (1, 1, 1, 0, 0, 7, 1, "OSPI_D3  <> U16 HOLD#/IO3 (R41 pull-up)"),
}
for p in list(range(20, 38)):
    PINS[p] = NC_IN + ("NC" if p > 25 else "NC (bank 1)",)
for p in (26, 27, 28, 29, 30, 31):
    PINS[p] = NC_IN + ("NC (MMC1 pads: BOOT0=0/BOOT1=1 would try an SD card here and fail)",)
PINS.update({
    38: (1, 0, 1, 0, 0, 7, 1, "UART0_TXD console -> U2 CH340X RXD (USB-C 2)"),
    39: (1, 1, 0, 0, 0, 7, 1, "UART0_RXD console <- U2 CH340X TXD"),
    40: (1, 0, 1, 0, 0, 7, 1, "UART1_TXD -> BL616 GPIO22/RXD (AI_RXD, R36 pull-up)"),
    41: (1, 1, 0, 0, 0, 7, 1, "UART1_RXD <- BL616 GPIO21/TXD (AI_TXD, R34 pull-up)"),
    44: NC_IN + ("NC",), 45: NC_IN + ("NC",),
    48: (3, 1, 1, 1, 0, 7, 1, "IIC0_SCL, CAM2 (J4, CSI0), R17/R18 4.7k to 3V3"),
    49: (3, 1, 1, 1, 0, 7, 1, "IIC0_SDA, CAM2 (J4, CSI0)"),
    59: NC_IN + ("NC",),
    60: (1, 1, 1, 0, 0, 7, 1, "PWM0 -> LED_CTL1 -> U15 TPS61161 CTRL, IR chain (J2)"),
    61: (1, 1, 1, 0, 0, 7, 0, "PWM1 -> LED_CTL2 -> U14 TPS61161 CTRL, white chain (J1)"),
    62: (0, 0, 1, 0, 1, 4, 1, "GPIO62 -> J4 pin 18 CAM_IO1 (camera LED on Pi v1.3 boards): driven low"),
    63: (0, 0, 1, 0, 1, 4, 1, "GPIO63 -> J5 pin 18 CAM_IO1 (camera LED): driven low"),
})
# not bonded on the K230D (Canaan's K230D values)
NOT_BONDED = {9: (0, 0, 0, 0, 0, 7, 1), 42: (0, 0, 0, 0, 0, 7, 1), 43: (0, 0, 0, 0, 0, 7, 1),
              46: (0, 0, 0, 0, 0, 7, 1), 47: (0, 0, 0, 0, 0, 7, 1)}
for p in range(50, 59):
    NOT_BONDED[p] = (0, 0, 0, 0, 0, 7, 1)

BANK_MACRO = [(0, 1, "VOL_BANK_IO0_1"), (2, 13, "VOL_BANK0_IO2_13"), (14, 25, "VOL_BANK1_IO14_25"),
              (26, 37, "VOL_BANK2_IO26_37"), (38, 49, "VOL_BANK3_IO38_49"),
              (50, 61, "VOL_BANK4_IO50_61"), (62, 63, "VOL_BANK5_IO62_63")]
DTS_BANK = [(0, 1, "BANK_VOLTAGE_IO0_IO1"), (2, 13, "BANK_VOLTAGE_IO2_IO13"),
            (14, 25, "BANK_VOLTAGE_IO14_IO25"), (26, 37, "BANK_VOLTAGE_IO26_IO37"),
            (38, 49, "BANK_VOLTAGE_IO38_IO49"), (50, 61, "BANK_VOLTAGE_IO50_IO61"),
            (62, 63, "BANK_VOLTAGE_IO62_IO63")]


def bank(p, table):
    for lo, hi, name in table:
        if lo <= p <= hi:
            return name
    raise ValueError(p)


def check():
    for p in range(64):
        assert (p in PINS) != (p in NOT_BONDED), "pin %d must be in exactly one table" % p
    # the NAND must be on the OSPI function, the BL616 link on UART1, the LEDs on PWM
    assert all(PINS[p][0] == 1 for p in range(14, 20))
    assert all(PINS[p][5] == 7 for p in range(14, 20))      # OSPI drive, see the table
    assert PINS[40][0] == 1 and PINS[41][0] == 1 and PINS[60][0] == 1 and PINS[61][0] == 1


HEADER = """/*
 * LEAKCAM (K230D + BL616) pin mux for RT-Smart, board/configs/k230d_leakcam/pinmux_config.c.
 * GENERATED by LEAKCAM firmware/k230_board/pins.py -- edit the table there, not this file.
 *
 * All VDDIO banks are on 3V3 on this board: every bank is declared 3.3 V.
 */

#include "drv_fpioa.h"

#define VOL_BANK_IO0_1     BANK_VOL_1V8_MSC   /* fixed 1.8 V (BOOT0/BOOT1) */
#define VOL_BANK0_IO2_13   BANK_VOL_3V3_MSC
#define VOL_BANK1_IO14_25  BANK_VOL_3V3_MSC   /* W25N02KV SPI NAND, 3.3 V */
#define VOL_BANK2_IO26_37  BANK_VOL_3V3_MSC
#define VOL_BANK3_IO38_49  BANK_VOL_3V3_MSC
#define VOL_BANK4_IO50_61  BANK_VOL_3V3_MSC
#define VOL_BANK5_IO62_63  BANK_VOL_3V3_MSC

/* clang-format off */
const board_pinmux_cfg_t board_pinmux_cfg[FPIOA_PIN_MAX_NUM] = {
"""

FOOTER = """
#if FPIOA_PIN_MAX_NUM > 64   /* 72 with the PMU RTC in use */
    /* PMU IO: not used on LEAKCAM (INT4 is the R45 cold-start pull-up), left as GPIO like
     * Canaan's K230D boards */
    [64] = PMU_GPIO(GPIO64),
    [65] = PMU_GPIO(GPIO65),
    [66] = PMU_GPIO(GPIO66),
    [67] = PMU_GPIO(GPIO67),
    [68] = PMU_GPIO(GPIO68),
    [69] = PMU_GPIO(GPIO69),
    [70] = PMU_GPIO(GPIO70),
    [71] = PMU_GPIO(GPIO71),
#endif
};
/* clang-format on */

/* Run by board/pinmux.c after the GPIO driver is up (RT_BOARD_ENABLE_PINMUX selects
 * RT_BOARD_ENABLE_PIN_INIT_SEQUENCE). J4/J5 pin 18 is CAM_IO1, the red status LED on Pi v1.3
 * style camera boards: drive it low so no LED light reaches the fisheye lenses. */
static inline __attribute__((always_inline)) void board_specific_pin_init_sequence()
{
    pin_output(62, 0);
    pin_output(63, 0);
}
"""


def rtsmart_c():
    out = [HEADER]
    for p in range(64):
        if p in PINS:
            sel, ie, oe, pu, pd, ds, st, note = PINS[p]
            msc = bank(p, BANK_MACRO)
        else:
            sel, ie, oe, pu, pd, ds, st = NOT_BONDED[p]
            msc, note = "0", "not bonded on K230D"
        out.append("    [%-2d] = PINMUX_CFG(%d, %s, %d, %d, %d, %d, %d, %d), // %s\n"
                   % (p, sel, msc, ie, oe, pu, pd, ds, st, note))
    out.append(FOOTER)
    return "".join(out)


def uboot_dtsi():
    out = ["/*\n * LEAKCAM pin mux for U-Boot and SPL (included by k230d_leakcam.dts).\n"
           " * GENERATED by LEAKCAM firmware/k230_board/pins.py -- edit the table there.\n */\n",
           "&iomux {\n\tpinctrl-names = \"default\";\n\tpinctrl-0 = <&drop_pins &pins>;\n\n",
           "\tpins: iomux_pins {\n\t\tu-boot,dm-pre-reloc;\n\t\tpinctrl-single,pins = <\n"]
    for p in sorted(PINS):
        sel, ie, oe, pu, pd, ds, st, note = PINS[p]
        out.append("\t\t(IO%-2d) ( %d<<SEL | 0<<SL | %s<<MSC | %d<<IE | %d<<OE | %d<<PU | %d<<PD | %d<<DS | %d<<ST ) // %s\n"
                   % (p, sel, bank(p, DTS_BANK), ie, oe, pu, pd, ds, st, note))
    out.append("\t\t>;\n\t};\n};\n")
    return "".join(out)


if __name__ == "__main__":
    check()
    c = os.path.join(HERE, "rtsmart", "board", "k230d_leakcam", "pinmux_config.c")
    d = os.path.join(HERE, "uboot", "dts", "k230d_leakcam_pins.dtsi")
    os.makedirs(os.path.dirname(c), exist_ok=True)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    open(c, "w").write(rtsmart_c())
    open(d, "w").write(uboot_dtsi())
    print("wrote", c, "and", d)
