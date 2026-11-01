/* LEAKCAM K230 bring-up: the smallest program that proves the K230 runs code and talks on its debug UART.

   Loaded over USB into SRAM by the boot ROM (k230_flash_cli --custom-loader, nothing is written to the NAND)
   and run in place. Prints one status line per second on UART0 (0x91400000, DW 8250, reg-shift 2), which the
   CH340X on USB-C 2 shows as /dev/ttyUSB0. The ROM set UART0's baud divisor for its own "boot failed" message
   (115200), but in USB boot mode the UART0 clocks may be gated: they are switched back on here (CMU
   ls_clken_cfg0, bits from RT-Smart's sysctl_clk.c), the divisor is kept as the ROM left it.
   Each line also toggles GPIO2 = K230_ALIVE (BL616 IO01): `pt_status` on the BL616 console shows it flip, which
   proves the program runs even if the UART stays silent. IO2 is muxed as in pins.py: GPIO, 3.3 V bank (msc 0).
   The CPU clock is whatever the ROM left; the line shows the cycle counter so the real clock can be read off. */
#include <stdint.h>

#define UART0     0x91400000UL
#define UART_THR  (*(volatile uint32_t *)(UART0 + 0x00))
#define UART_LSR  (*(volatile uint32_t *)(UART0 + 0x14))
#define UART_LCR  (*(volatile uint32_t *)(UART0 + 0x0c))
#define LSR_THRE  0x20

#define CMU_LS_CLKEN0 (*(volatile uint32_t *)0x91100024UL)   /* ls_clken_cfg0 */
#define LS_PCLK_SRC   (1u << 0)
#define UART0_PCLK    (1u << 1)
#define GPIO_PCLK     (1u << 11)
#define UART0_CLK     (1u << 16)

#define IOMUX_IO2     (*(volatile uint32_t *)(0x91105000UL + 4 * 2))
#define IO2_GPIO_OUT  ((4u << 1) | (1u << 7) | (1u << 8))       /* sel 0 GPIO2, ds 4, oe, ie, msc 0 = 3.3 V */
#define GPIO0_DR      (*(volatile uint32_t *)0x9140B000UL)
#define GPIO0_DDR     (*(volatile uint32_t *)0x9140B004UL)
#define ALIVE_BIT     (1u << 2)

static inline uint64_t cycles(void);

/* UART0 register READS return 0 in this state (LSR, USR read 0x0 on rev 1, 2026-10-17) while writes work, so
   the THRE bit can never be polled: characters are paced by time instead, 200k cycles each (>= 125 us at up
   to 1.6 GHz; a character at 115200 baud takes 87 us), so the transmit FIFO never overflows. */
static void putc_(char c)
{
    uint64_t t = cycles();
    UART_THR = (uint32_t)c;
    while (cycles() - t < 200000)
        ;
}

static void puts_(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            putc_('\r');
        putc_(*s);
    }
}

static void putu(uint64_t v)
{
    char b[21];
    int i = 20;
    b[i] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    puts_(&b[i]);
}

static void puthex(uint32_t v)
{
    puts_("0x");
    for (int i = 28; i >= 0; i -= 4)
        putc_("0123456789abcdef"[(v >> i) & 15]);
}

static inline uint64_t cycles(void)
{
    uint64_t c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

static inline uint64_t hartid(void)
{
    uint64_t h;
    __asm__ volatile("csrr %0, mhartid" : "=r"(h));
    return h;
}

#define CYCLES_PER_TICK 800000000ULL      /* one line per 800 M cycles: 1 s at 800 MHz */

void main(void)
{
    uint64_t n = 0, t0 = cycles(), next = t0;

    CMU_LS_CLKEN0 |= LS_PCLK_SRC | UART0_PCLK | GPIO_PCLK | UART0_CLK;
    if ((UART_LCR & 0xbf) != 0x03)
        UART_LCR = 0x03;                    /* 8N1, DLAB off; the divisor stays */
    IOMUX_IO2 = IO2_GPIO_OUT;
    GPIO0_DDR |= ALIVE_BIT;

    puts_("\nLEAKCAM K230 hello: running from SRAM, hart ");
    putu(hartid());
    puts_(" LSR=");
    puthex(UART_LSR);
    puts_(" USR=");
    puthex(*(volatile uint32_t *)(UART0 + 0x7c));
    puts_(" LCR=");
    puthex(UART_LCR);
    puts_("\n");
    for (;;) {
        while ((int64_t)(cycles() - next) < 0)
            ;
        next += CYCLES_PER_TICK;
        GPIO0_DR ^= ALIVE_BIT;
        puts_("status n=");
        putu(n++);
        puts_(" mcycle=");
        putu(cycles() - t0);
        puts_(" ok\n");
    }
}
