# bug-002: K230 rails on back-feed VUSB, USB-C hosts refuse to power the board

Found 2026-10-17 in the K230 rail bring-up (tools/power-bringup, k230_rails.png). Board: MIFA rev 1 (fab 11a6a38).

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
- Bench: lift D3 pin 5 (VCC) off its pad. The debug data still passes (the lines run through pins 1-6 and 3-4);
  only the clamp to VCC is lost. Untested.
