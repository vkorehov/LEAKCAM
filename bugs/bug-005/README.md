# bug-005: the Pi Zero camera cable lands camera 3V3 on J4 pin 1 (GND)

Found: 2026-10-24, test017 (the K230's 3V3 stopped coming up). Status: open, cause found; rev 1: top-contact connector + rotated cameras, rev 2: reversed pinout.

## What happened

With the stopgap OV5647 (Pi camera V1.3 format, anodas.lt) on J4 through the 4 cm 15-to-22 cable
(EXTERNAL_PARTS.md part 3), `pt_on` failed: "3V3 not up 100 ms after K230_PWR (0 of 5 lines high)", also after a
BL616 reboot; no K230 USB boot device. Camera unplugged: 3V3 up in 1.7 ms again. The rails had come up with the
camera plugged earlier the same evening (shift100.py, ~19:50), the failure started ~20:03.

## Findings

- Board side is right: ODB++ (MIFA.tgz) J4 and J5 pads follow the Pi Zero 22-pin camera pinout pin for pin (pin 1
  GND, pairs on 2-9, CAM_IO0 pin 17 pulled up by R13/R16, CAM_IO1 pin 18 to the K230, SCL/SDA 20/21, 3V3 pin 22,
  shell tabs GND). Next to 3V3 (22) is only SDA (21): a one-pin shift cannot short 3V3.
- Cable under the microscope (user): contacts clean and continuous into their traces; the camera's 3V3 conductor
  arrives at **J4 pin 1 (GND)**. Camera pin 1 (GND) then lands on J4 pin 22 (3V3): 3V3 straight into ground through
  the camera's ground plane, U8 (TPS63802) holds the rail down until the camera is unplugged.
- The "verified compatible" note in EXTERNAL_PARTS.md part 3 covered the pin numbering, not the contact side: a
  same-side-contact cable that only makes contact turned over in a connector with the other contact side mirrors
  the order (cable position 1 <-> 22).

## Cause (2026-10-24)

The connector's **contact side**, not the footprint or the pinout:

| | BPI-CanMV-K230D-Zero (reference) | LEAKCAM J4/J5 |
|---|---|---|
| Part | JUSHUO AFC07-S22ECA-00 (LCSC C262642) | Hanyones 05B20L22P (LCSC C22459552) |
| Contact | **top** (LCSC listing) | **bottom** ("L: Low Contact", Hanyones drawing) |
| Pin numbering | 1 GND ... 20 SCL, 21 SDA, 22 3V3 (schematic X1) | the same (ODB++) |
| Footprint (EasyEDA, mechanical/models/easyeda/) | pad 1 left, signal row above the shell tabs | the same geometry |

The Pi Zero cable is made for the top-contact side; in a bottom-contact connector it only contacts turned over,
which mirrors cable position 1 <-> 22. How it got in: the schematic first had Molex 54548-2271 (bottom contact);
when it was discontinued (2026-09-15, BOM_LCSC.txt note, commit f96f74f) the replacement was chosen as "same type,
bottom-contact" and its footprint verified; the contact side was never compared with the reference board's part.

## Fix

Folding, rotating or twisting a flat cable never changes which end pin meets which: only the cable type (same- or
opposite-side contacts), the connector's contact type or a crossover does. Decided 2026-10-24 (user):

- Rev 1: J4/J5 reworked to a top-contact connector, JUSHUO AFC07-S22ECA-00 (LCSC C262642, the reference board's
  part; AFC07-S22ECA-00_drawing.pdf: E = upper contact, land 0.30 x 1.20 / tabs 2.2 x 3.3 at 1.60 beyond the end pins,
  2.17 below the row; ours 0.30 x 1.30 / 2.0 x 3.0 at 1.65, 2.35: lands pad for pad), with the existing cable type; on the plain fold the camera then faces
  the board, so the cameras are rotated 180 degrees (lens out) and need longer cables to reach their connectors.
- Rev 2: reverse the J4/J5 pinout (bugs/v2.txt item 10).
- Before power after any connector or cable change: continuity camera pin 15 (3V3) -> J4/J5 pin 22.
- Review rule: for every connector, trace each conductor from the far end through the cable's contact sides and
  the connector's contact type to the pad, in the mounted (folded) position, not only the pin numbers.

## Open

- Which way the cable sat when the rails still came up at ~19:50.
- Camera health: ohms between its 15-pin connector pin 1 (GND) and pin 15 (3V3), unpowered.
- Do not plug a camera into J4/J5 with the current cables.

## Ordered (2026-10-24)

- Farnell: Molex 52435-2271 (2679571), cancelled 2026-10-28: Farnell needs a VAT number on the account.
- LCSC (2026-10-28, ~10 days): JUSHUO AFC07-S22ECA-00 (C262642) for the J4/J5 rework.
- AliExpress: same-side 15-to-22 camera cables, 8 cm (bottom camera, J5, ~70 mm needed) and 15 cm (top camera, J4,
  ~83 mm needed; no 10 cm sold), COME ON DIY Store listing 1005005609470913. Check same-side contacts on arrival.
- On arrival: rework J4/J5, then continuity camera pin 15 -> J4/J5 pin 22 before any power, then test020/test030.
