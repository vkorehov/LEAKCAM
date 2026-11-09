# Bench relays (wireless): HM-10 (CC2541) + 4-relay module

Replaces the hand steps on the LEAKCAM's USB-C 1: unplug/plug, and moving it between the bench PC and the phone
(test113, test116, test129 --battery, the cable tests). The HM-10's firmware (HMSoft) switches its PIO pins on AT
commands sent over BLE (`AT+MODE2`); the pins drive the relay inputs. The FT4232H board flashes the HM-10 once
(only if it runs clone firmware).

![wiring](wiring.png)

`wiring.png` is drawn by `draw_wiring.py <HM-10 photo> <FT4232H photo>` (20261108_145123.jpg, 20261108_165713.jpg).

Parts: keyestudio Bluetooth-4.0 V3 (HM-10, TI CC2541 F256, on a carrier with a 3.3 V regulator), a 4-relay module
(SPDT COM/NO/NC; IN1..IN4, VCC, GND; active LOW), two USB-C to USB-C cables cut in half (PC, phone, LEAKCAM halves;
the 4th half + a USB-C charger power the box), a 5.1 k resistor, the FT4232H breakout (flashing).

## HM-10 pads

Top view, counter-clockwise: 1-13 on one long edge (1 at the antenna), 14-21 under the carrier end, 22-34 on the
other edge (34 at the antenna). **Check first:** pad 12 = 3.3 V to GND with the carrier powered.

| Pad | Signal | Goes to |
|---|---|---|
| 7 | DC | FT4232H ADBUS0 (flashing) |
| 8 | DD | FT4232H ADBUS1 (flashing) |
| 11 | RESET_N | FT4232H ADBUS2 (flashing) |
| 12 | VCC 3.3 V | relay VCC on a JD-VCC board |
| 25 / 26 / 27 / 28 | PIO2 / PIO3 / PIO4 / PIO5 | relay IN1 / IN2 / IN3 / IN4 |
| carrier VCC | 5 V in | charger half VBUS, relay VCC (or JD-VCC); FT4232H PWR while flashing |
| carrier GND | GND | charger half GND, relay GND; FT4232H GND while flashing |

## Flashing and setup (done 2026-11-08)

The FTDI breakout is an **FT2232H** (0403:6010) on a PCB labelled for an FT4232H: its "ADBUS" pads are channel A,
its **"CDBUS" pads are serial port B** (the "BDBUS" pads are channel A's ACBUS). Its VIO pads need 3.3 V wired from
the board's regulator, else no pin moves.

- Flashing (channel A, `tools/cc-debugger/ccdbg.py`): ADBUS0 -> pad 7 DC, ADBUS1 -> pad 8 DD, ADBUS2 -> pad 11 RESET,
  GND. The chip: CC2541 rev 0x13, was debug-locked (clone firmware, no backup possible); now HMSoft V709 (full image,
  Jason2866/CCLoader Bin/CC2541hm10v709.bin), written and verified.
- Serial (port B, 115200 8N1, the image's default): CDBUS0 -> carrier RXD, CDBUS1 <- carrier TXD, GND.
- Set once (stored): `AT+ROLE0` (peripheral), `AT+IMME0` (advertises at start), `AT+MODE2` (remote PIO over BLE),
  `AT+NAMELCRELAY`, `AT+BEFC3C0` and `AT+AFTC3C0` (PIO2-5 HIGH at power-on and after a connection = relays off; the leftmost hex digit is PIO2: `00F` set PIO8-B and the relays all came on).
  The image came as ROLE1 + IMME1 (a central waiting for AT): no advertising until changed.
- Advertises as **LCRELAY**, A4:06:E9:9D:30:7E (nRF Connect on the bench phone, -61 dBm on the bench).

## As wired (2026-11-08)

The relays carry the USB-C cable wires in this order (checked one by one on 2026-11-08: PIO2 LOW -> green, PIO3 -> white, PIO4 -> yellow, PIO5 -> red +5 V;
releasing PIO2..PIO5 one by one released green, white, yellow, red):

| HM-10 | Relay input | Relay | USB-C wire it switches |
|---|---|---|---|
| pad 25 PIO2 | IN1 | K1 | green = D+ |
| pad 26 PIO3 | IN2 | K2 | white = D- |
| pad 27 PIO4 | IN3 | K3 | yellow = CC |
| pad 28 PIO5 | IN4 | K4 | red = VBUS |

This replaces the K1..K4 order and the sequences above (verified 2026-11-08, `phone.hand()` / `relay()`):

- Unplug = K4 (VBUS, PIO5) alone onto the phone side: the bench port gives 5 V always, so cutting CC did not
  unplug; the phone gives none while it cannot see CC. The console stays (D+/D- to the PC); `metrics` chg=0.
- To the phone: K4 on, 1 s, K1 + K2 + K3 on. Back: K3 off, 1 s, K1 + K2 + K4 off.
- HMSoft returns the pins to the power-on state (BEFC) when the phone disconnects: each step stores its end state
  first (`AT+BEFC` bench 3C0, unplugged 380, phone 000; leftmost bit PIO2, 1 = off), then one `AT+MPIO` per phase
  (the same bits): every relay of a phase switches at once, not one by one.

The FT2232H stays wired for now (flashing ADBUS0-2, serial CDBUS0/1): keep it on the Mac's USB, an unpowered FTDI
can hold RESET (pad 11) low.

## Power for the box

A USB-C charger through the 4th cable half: VBUS -> carrier VCC + relay VCC, GND -> GND, a 5.1 k resistor
(4.7-5.6 k) from its CC to GND (a USB-C charger gives 5 V only when it sees it; a cheap one may always: check with a
meter). Not from the PC or phone half: their 5 V goes off with the switching. Load ~0.3 A.

## Relays

| Relay | PIO | Switches | Off (NC) | On (NO) |
|---|---|---|---|---|
| K1 | PIO2 | VBUS | bench PC | phone |
| K2 | PIO3 | CC | bench PC | phone |
| K3 | PIO4 | D+ | bench PC | phone |
| K4 | PIO5 | D- | bench PC | phone |

Box unpowered or BLE lost: relays off = the LEAKCAM on the bench PC.

A USB-C host gives 5 V only while it sees the device's 5.1 k Rd on CC (the LEAKCAM has it). So **unplug = K2 alone
to the other side**: the host on the VBUS side loses CC and drops its 5 V (the other host sees CC but its 5 V goes
nowhere). The phone becomes host by itself (no OTG adapter). The two hosts' VBUS are never joined.

Cable halves: VBUS red, GND black (+ the braid), D+ green, D- white; CC = plug pin A5 (find it with a meter, colour
varies). SBU, SuperSpeed and VCONN wires: insulated, not connected. Each LEAKCAM wire -> its relay's COM, the PC's ->
NC, the phone's -> NO; GND of all three joined. Wires short (< 10 cm), D+/D- twisted.

## Sequences (`AT+PIOn0` = relay on, `AT+PIOn1` = off)

- Unplug on the bench: K2 on; plug: K2 off.
- To the phone: K2 on (unplugged), wait 1 s, K1 + K3 + K4 on (the phone powers and enumerates the LEAKCAM).
- Back to the bench: K2 off (unplugged), wait 1 s, K1 + K3 + K4 off.

## Rules

- On the phone the LEAKCAM is powered by the phone and the phone does not charge: swap back to the bench when done
  (the phone ran down on 2026-11-08 powering the board).
- If high speed (480 Mbit/s) fails through the contacts, the BL616 falls back to full speed: check the console ports
  come up on both hosts.
- The cell needs a 5th relay (a 2nd module on PIO6).
