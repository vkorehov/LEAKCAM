# test035: history

Earlier runs, superseded findings and obstacles, newest first. Current state: README.md.

## Runs

### 2026-10-22

| Test | Result |
|---|---|
| awake, unplug USB-C 1 | "USB cable unplugged (PGOOD high)" at once; the board stays up on the cell, PR1 console unaffected (PR1 does not feed PGOOD) |
| awake, plug back | "detected (PGOOD low)" |
| HBN, USB out, armed for plug-in; plugged ~10 s later | woke **by usb**, console back 11 s after the sleep started (plug + VMware click) |
| HBN, USB in, armed for unplug (an accident: USB was back before the sleep); unplugged | woke **by usb** after 103 s, before the 120 s RTC: the unplug edge wakes it too |

Wake latency: not measured directly; the HBN wake path is ~15 ms (test021) plus PGOOD's fall (BQ24072 open drain
against R23 100 k: fast) - well inside "within ms" of the USB power being good.

**Step 3, plug-in during a K230 session (K230 running from NAND on the cell, USB-C 1 from a Mac):** 0.03 mA in VBUS,
PGOOD high, no charging - also with the meter leads shorted. `pt_off` with the cable in: charging at once.
`pt_usb offwait` (rails off, then time PGOOD): **PGOOD low 113 ms after the 3V3 rail was gone**. This is bug-002 (the
CH340's D+ pull-up back-feeds VUSB through D3; a USB-C source waits for VBUS below 0.8 V before turning it on).
Product consequence, fixed in main.c: after a session the USB power arrives only when the rails are off, so the plug
edge has already passed when `sleep_for()` arms it - the board slept on battery while plugged until the next
humidity slice (up to 10 min). Now `k230_off()` waits up to 300 ms more for PGOOD after the power-off, and
`sleep_for()` enters USB mode when USB is present. A plain 5 V charger powers the board even during a session.

**Step 4, plug cycles (plain USB charger, BL616 awake):** 120 s window, 10 unplugs and 9 plug-ins reported, ending
unplugged: every transition seen, none stuck (19 by hand, not the planned 100).

**Step 2 (USB-C 2 only):** not applicable on this board: USB-C 2's VBUS was cut by hand on 2026-10-21.

Open: the Mac once gave no power with the K230 **off** (0.02 mA after a few plugs at 17:39-17:41; most likely its
port protection after repeated plug-ins with the charger's inrush), the plain charger worked at once; the product
firmware build (rewrite).

### Earlier status

Status: **pass** (2026-10-22): plug / unplug seen awake, HBN wake on both edges, 19 charger plug transitions all reported; a plug-in during a K230 session got no power from a USB-C host until the rails went off (bug-002, PGOOD 113 ms later; solved by the U2 1-2-3 bridge 2026-10-25). Open: the Mac refusing power with the K230 off once.

### Superseded metric values

- 2026-10-22 (row fragment left in the Metrics table, its start lost in an earlier edit):
  `k5 mA at 17:29 after the plug-in (BQ24072 charging the cell it ran on while unplugged; BL616 awake, K230 off) | floor (test031) | USB-C 1 VBUS meter, D1 off; VBAT at C15 |`

## Obstacles

- **First plug-wake try armed the wrong edge**: the user plugged back within the 2-3 s between my unplug check and
  the sleep command, so the firmware saw USB present and armed the unplug edge -> the script waits for 5 s of
  "unplugged" first and the user counts to 15 before plugging. (It proved the unplug wake instead.)
- **The Mac stopped powering the port after several plug-ins** (K230 off): switched to a plain USB charger (user).
- **The user's first "20 cycles" window passed before the cycling started** -> the count ran again and caught it.
- **"woke by leak" after a flash**: the HBN interrupt state survives the flasher's reset, an ACOMP1 event latched while
  awake showed as a wake reason. The product clears all three at boot and before each sleep; harmless.
