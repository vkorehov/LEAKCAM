/*
 * The BLE core wedge's registers (skill bl616-ble-low-power 5, tests/test120 HISTORY 2026-11-01): dumped into the
 * retained evlog (EV_REG) at the points below, so a wedge met on the cell is read after the next plug (`evlog`), and
 * live by `blecore`. Wedged then: the core's registers read 0, bit 31 of 0x28000100 (sample the native clock) never
 * clears; PDS_CTL/CTL4/CTL5 3459fbb0/03303003/00000300 against a fresh chip's 14006b00/0ff0f00f/00000000.
 */
#ifndef LEAKCAM_BLECORE_H
#define LEAKCAM_BLECORE_H

enum blecore_point {
    BLECORE_BOOT = 1,       /* main() before its PDS reset: the PDS registers only (the core's clock may be off) */
    BLECORE_SLEEP_BEGIN,
    BLECORE_SLEEP_END,
    BLECORE_PARKED,         /* EV_WEDGE 1 */
    BLECORE_STUCK,          /* EV_WEDGE 2 */
};

/* EV_REG entries: arg = point * 8 + register (blecore.c regs[]), val = the register */
void blecore_dump(enum blecore_point point);

#endif
