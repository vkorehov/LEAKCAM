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

## Seen again with the K230 running (2026-10-22, tests/test035)

K230 booted from NAND and running on the cell, USB-C 1 (Mac) unplugged and plugged back: 0.03 mA in VBUS, PGOOD high,
the charger off - with the meter leads shorted too. `pt_off` with the cable in: charging at once; `pt_usb offwait`:
PGOOD low **113 ms** after the 3V3 rail was gone. So in the product a cable plugged in during a session is invisible
until the session ends. Firmware handling (rev 1): `k230_off()` waits up to 300 ms more for PGOOD after the power-off
(the source's VBUS turn-on), and `sleep_for()` enters USB mode when USB is present instead of arming a plug edge
that has already passed (main.c). The hardware fix stays the rev 2 solution above.

## Seen again without the CH340 (2026-10-23, the U2 path gone)

USB-C 1 plugged in while the K230 sat in its USB boot ROM (BOOT1 high, rails on): 0 mA, no VMware offer; `pt_off`:
PGOOD low at once. With the K230 booted from NAND a mid-session plug works (16:27, its USB idle). So the D5 path
predicted above is real: the ROM's USB device pulls D+ up, D5 lifts VUSB. Plug USB-C 1 before `pt_on` for USB boot.

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
- **USB boot with the CH340 left plugged works** (2026-10-21, three flashes): USB-C 1 already attached, `pt_off`,
  `pt_boot1 high`, `pt_on`, `pt_run`; the K230 boot device attached each time. Unplug the CH340 only if it does not.
- **CH340 drops on every USB-C 1 plug while the K230 runs** (2026-10-21, reproduced, tests/test023): it
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

## Seen again under full load (2026-10-25, tests/test105)

Every camera-free load on (both LED chains 100 %, Wi-Fi upload, memtest), USB-C 1 and USB-C 2 unplugged, the cell
carrying it for minutes, then USB-C 1 plugged back in with the K230 still running: **no charging** until `pt_off`, then
at once (0.9 A, tapering). With no cable in either port the path is all on the board, so "both USB cables out" does
not remove it: the CH340X (U2) on the switched 3V3 pulls its D+ up internally, D3's steering diode passes that into
VUSB (D3 VCC = VUSB), the USB-C source sees ~2.2 V and never turns VBUS on.

**To pin the path** ([probe_points.png](probe_points.png), whole side [probe_points_board.png](probe_points_board.png); rails
on with `pt_on`, K230 held in reset, no cable in either port; D = U2 pin 1, the CH340X's UD+, same net as D3 pin 6): VUSB (C2 pad 1) ~2.2 V and
D3 pin 6 (DBGP_D_P) ~3.3 V confirm D3; then lift D3 pin 5 (VCC): VUSB below 0.8 V = fixed (rev 1 rework: D3 out or its
VCC pin lifted; USB-C 2 is buried in the enclosure and unused in the product). **Rework only with hot air** (D3 off
whole): no pin lifting, and no trace cut (the D3 VCC track runs on an inner layer) (user, 2026-10-25); until then the firmware's end-of-session
handling stands, the hardware fix is rev 2. VUSB still high = another path: D5 (the
K230 USB ESD array, same VCC wiring; only with the K230's D+ pulled up as a device, and test003 saw 2.2 V with the
K230 in reset) or R63 (30 k, VUSB to K230 B7).

## Measured step by step, 2026-10-25 (cell full or nearly, meter in the USB-C 1 cable; probe sheets in this folder)

V = VUSB at C2 pad 1, D = CH340X UD+ at U2 pin 1 ([probe_points.png](probe_points.png)); PR1 (the BL616 console)
always in, USB-C 2 never plugged.

| Step | Rails | USB-C 1 | V | D | Charging |
|---|---|---|---|---|---|
| A | off | out (PR1 out too) | 4.17 V | 6 mV | - |
| B | on, K230 in reset | out | 4.17 V | 3.0 V | - |
| B2/B3 | on | Mac plugged in | 4.15 V | 2.8 V | **no** |
| C | switched off with the Mac in | in | 4.3 V | ~0 | **yes, at once** |
| D2 | off, 2.1 kohm across C2 | out | **4.15 V** (~2 mA) | - | - |
| D2' | off | Mac plugged in (V 4.15 V before) | 4.3 V | - | **yes** |
| F | on, the Mac still plugged in (not the wall charger: my mistake) | in | - | - | **no**, PGOOD high |
| F' | switched off with the Mac in | in | - | - | PGOOD low at once |
| F'' | switched on again with the Mac in | in | - | - | **yes, keeps charging** |
| G | on, K230 running + both LEDs 100 % (cell discharged a little first) | out | 3.99 V (the cell under load) | 3.1 V | - |
| G2 | the same, a plain 5 V wall charger plugged in | in | - | - | **yes, at once** (PGOOD low) |
| H | on (K230 in reset), Mac unplugged and plugged back | in | - | - | **no** (reproduced) |
| H2 | the same, **D (U2 pin 1) shorted to GND for a moment** | in | - | 0 | **yes, charging started** |

- **The fault is only at plug-in with the rails on, and only with a USB-C source that checks VBUS** (the Mac's port):
  it never starts. **A plain 5 V wall charger charges at once with the rails on** (G2), as in the first finding. Once
  the Mac's input is accepted, the rails can go on and it keeps charging.
- **U1 back-feeds VUSB from the cell** with nothing plugged: 4.17 V open, 4.15 V into 2.1 kohm (~2 mA, ~10 ohm): a
  stiff path through the charger itself (the only part on VUSB with the battery side behind it). It does **not** block
  the start (plug-in works with it when the rails are off), so it is a separate oddity to check against the BQ24072's
  input blocking spec. It also rules out a bleed resistor across C2 as a fix (it cannot pull a ~10 ohm path down).
- **Cause confirmed (H2): the CH340X's D+ pull-up through D3 into VUSB.** Grounding D for a moment let the Mac start
  at once with the rails on. (D5, the K230's USB lines, is not needed for it.)
- **Rev 1 fix options:** a solder bridge over U2 pins 1-2-3 (UD+, UD-, GND, adjacent): D+ held at ground, an iron job,
  undone with wick; costs ~2 mA through the pull-up while the rails are on, nothing asleep; the USB-C 2 debug console is
  lost (buried, unused: the K230 console runs through the BL616). Or U2 off with hot air (no cost). Rev 2 as above.
- Was: "remaining suspects D3 or D5" - settled by H2. The finer probe points (D5 pins, CC on R4 /
  R9: [usbc1_top.png](usbc1_top.png), [usbc1_bottom.png](usbc1_bottom.png)) were too fine to probe by hand. Next with
  hot air: D3 off, then one plug-in with the rails on answers D3 vs D5.
- Product impact unchanged: a plug-in during a K230 session charges when the session ends (firmware handles it).
- Obstacle: the measurements went wrong twice on my side - I asked for readings before switching the rails on, and I
  mixed up which cable was the "debug cable" (PR1) - which cost several rounds.

## Rev 1 workaround: solder bridge over U2 pins 1-2-3 (2026-10-25)

![U2 bridge](fix_bridge_u2.png)

From H2 (grounding the CH340X's D+ let the Mac start with the rails on): one solder blob across **U2 pins 1, 2, 3**
(UD+, UD-, GND; the three right-most pads of U2's row nearest D3 in [fix_bridge_u2.png](fix_bridge_u2.png)). Pin 4
must stay free.

- Effect: the CH340X's D+ pull-up is held at ground, nothing reaches VUSB through D3, a USB-C source (Mac) starts at
  plug-in with the K230 running.
- Cost: ~2 mA through the CH340X's internal pull-up while the K230 rails are on (~1 % of a session's 220 mA idle),
  nothing asleep; the USB-C 2 debug console stops working (buried in the enclosure, unused: the K230 console runs
  through the BL616 from the first SPL line; only the boot ROM's own messages needed the CH340).
- Undo: solder wick. The clean variant is U2 off with hot air (no current at all); the real fix is rev 2 (above).
- Check after the bridge: rails on (`pt_on`), Mac into USB-C 1: it charges at once (PGOOD low).

