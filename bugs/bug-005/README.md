# bug-005: the Pi Zero camera cable lands camera 3V3 on J4 pin 1 (GND)

Found: 2026-10-24, test017 (the K230's 3V3 stopped coming up). Status: open, cause on the cable/connector side.

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

## Open

- Contact side of the 05B20L22P (LCSC C22459552) vs the Pi Zero's connector, and which way the cable sat when it
  still worked at ~19:50.
- Camera health: ohms between its 15-pin connector pin 1 (GND) and pin 15 (3V3), unpowered.
- Fix without a board change: a 15-to-22 cable with the contacts on opposite sides at the two ends (mirrors the
  order), or the custom 45-48 mm FPC planned for the enclosure (leakcam-camera-parcels memory) laid out for this
  connector. Do not plug a camera into J4/J5 with the current cables.
