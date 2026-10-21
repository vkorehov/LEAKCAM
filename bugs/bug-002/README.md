# bug-002: K230 rails on back-feed VUSB, USB-C hosts refuse to power the board

Found 2026-10-17 in the K230 rail bring-up (tests/test003-k230-rails-and-usb-boot, k230_rails.png). Board: MIFA rev 1 (fab 11a6a38).

## Symptom

Board on the battery, `pt_on` (K230 rails up, K230 held in reset), USB-C cable from a Mac into USBC1:
- D1 off, the BL616 reads PGOOD high ("USB cable: not detected");
- VUSB (C2 pad 1) reads about **2.2 V** instead of 5 V.

`pt_off` (rails off) with the cable still in: PGOOD goes low at once and the port supplies 5 V. With VUSB already at 5 V,
`pt_on` again is fine: the source stays on.

A plain 5 V charger powers the board either way. Only USB-C sources that check VBUS fail: a laptop, a Mac, most
USB-PD chargers. Before turning VBUS on, a USB-C source waits for VBUS to be below vSafe0V (0.8 V); the back-fed
2.2 V keeps it waiting for ever.

**In the product:** a camera running on the battery (K230 on) does not charge when plugged into such a port.

## Cause (likely, to confirm)

U2 (CH340X, the debug USB-serial on USBC2) is powered from the switched 3V3. Its D+ has an internal pull-up, so
D+ (DBGP_D_P) sits near 3.3 V whenever 3V3 is up, cable or not. That line passes through the ESD array D3
(PRTR5V0U2F), whose VCC pin (5) is tied to **VUSB**. The steering diode from D+ to VCC conducts, and VUSB is fed
from 3V3 through the pull-up: about 3.3 V - a diode drop, under the load on VUSB (C2, R8, R63, U1 IN): ~2.2 V.

The K230 was held in reset during the measurement, so it is not the source. D5 (the ESD array on the K230's own USB,
USBC1) has the same VCC = VUSB wiring and will back-feed the same way whenever the K230 pulls its D+ up as a USB
device. R63 (30 k, VUSB to K230 B7) is a third, weaker path.

**To confirm:** rails on, no USB cable: VUSB ~2.2 V, and D3 pin 6 (DBGP_D_P) ~3.3 V.

## Solution (rev 2)

- Power U2 (CH340X) from **USBC2's own VBUS** (after D2), not from 3V3: the debug chip then only lives while a debug
  cable is in, and it no longer draws from the battery.
- Tie D3's VCC to USBC2's VBUS and D5's VCC to USBC1's VBUS ahead of the charger, or use ESD parts without a VCC
  pin (e.g. a 2-line TVS to GND only), so no data line can lift VUSB.
- Check R63 / K230 B7 for the same.

## Workaround (rev 1 boards)

- **Plug USB in before the K230 rails come up**, or use a plain 5 V charger. Once VUSB is at 5 V the source stays on.
- **The CH340 counts too** (2026-10-19): with the CH340 (USB-C 2) attached, the K230's USB boot device on USB-C 1
  never reached the Mac (no VMware popup), though the ROM was in USB boot (`exit code 19`). Order that works: CH340
  unplugged, USB-C 1 in, then `pt_on`, `pt_run`; CH340 back after the boot device is attached.
- **CH340 drops on every USB-C 1 plug while the K230 runs** (2026-10-21, reproduced, tests/test022): it
  re-enumerates 10 s later (VMware popup, connect again); unplug does nothing. Harmless, debug console only.
- **Rework (rev 1, not applied: not worth it, the plug order above is enough; user 2026-10-19): D3 out, D+/D-
  bridged** ([d3_rework.png](d3_rework.png)). There is no track to cut: in the fab
  ODB++ (11a6a38) D3 pin 5 meets a VUSB via at its pad edge (34.77, 18.22) that continues on mid-layer 3 (0.76 mm) and
  the bottom layer, so the break has to be at the part. Remove D3 (PRTR5V0U2F, SOT886) with hot air, bridge pad 1 to
  pad 6 (D+) and pad 3 to pad 4 (D-) (the part passes them straight through), leave pad 5 (VUSB) and pad 2 (GND) bare.
  Check: rails on, no USB cable: VUSB ~0 V instead of ~2.2 V. Only the debug USB loses its ESD clamp.
- Alternative: lift only D3 pin 5. The clamp stays (the PRTR5V0U2F's Zener sits between its internal VCC node and
  GND), but one pin of a 1 x 1.45 mm SOT886 is hard to lift.

## Firmware

Cannot fix or detect it: while VUSB sits at ~2.2 V (below the BQ24072's 3.3 V UVLO) PGOOD stays high, so the
BL616 does not see the plug. It is limited by itself: the source turns VBUS on once a session ends and 3V3 drops,
so on battery charging waits for the current session at most; with the low-battery cutoff (bug-004) a low cell
never starts the K230, so a plugged charger always gets through.
