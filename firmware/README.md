# LEAKCAM power management: simulation and proof-of-concept firmware

The BL616 (Ai-M62-CBS, U11) runs from the always-on `3V3_SLEEP` rail and owns the K230's power.
It wakes on a leak or on its RTC, powers the K230 up, lets it capture, and powers it down again.
When USB power is plugged in it wakes, stays awake and advertises over BLE so a phone can set the
Wi-Fi credentials; unplugging returns it to the battery schedule. Every 10 minutes it wakes on its
RTC to read the AHT20 humidity sensor and goes back to sleep without the K230 unless humidity is high.

## Schematic changes needed before the first fab

### 1. USB-plug wake: swap two nets on U11

A USB plug must wake the BL616 from hibernate, and with the schematic as of 22 Sep it cannot. PGOOD (BQ24072
U1.7) goes to IO27, which is ADC_CH10: HBN wakes only on GPIO16-19 (not brought out by the module /
used by the 32 kHz crystal), the RTC and the two always-on comparators, and the comparators can
only select ADC_CH0-7. Every comparator-capable pin is taken, but IO03 (ADC_CH3) only drives
K230_PWR, which any GPIO can do. Swap two nets on U11:

| Net | Now | Next spin |
|---|---|---|
| PGOOD (R23 100k to 3V3_SLEEP stays) | U11.39 IO27 | **U11.7 IO03** (ADC_CH3, ACOMP0) |
| K230_PWR (R19 100k pull-down stays) | U11.7 IO03 | **U11.43 IO30** (free today) |

IO27 becomes unused. The firmware in this folder is written for this pin map only.

### 2. AHT20 (U17) supply filter

The AHT20 datasheet (Aosong, 2024-07, page 8, figure 15 and notes 2-3) asks for an RC filter on the
sensor's VDD: R1 330-390 ohm in series and C1 10 uF to GND. U17 VDD sits directly on 3V3_SLEEP with
only C110 100 nF, and 3V3_SLEEP is a buck-boost output that also carries the BL616's Wi-Fi transmit
bursts (266 mA). Add R 390 ohm (0402) from 3V3_SLEEP to a new U17 VDD node and 10 uF (0603) from that
node to GND. The 390 ohm is what sets the 41 Hz corner that rejects the burst ripple; 10 ohm would
only filter switching ripple. Drop at the 570 uA measuring current: 0.22 V, leaving ~3.08 V
(spec 2.2-5.5 V). **R73/R74 stay on 3V3_SLEEP**: figure 15 hangs the 4.7 k pull-ups on the supply
before R1; note 1 only requires them to come from the same supply as the sensor.

## Power chain as built (POWER.SchDoc)

```
BL616 IO30 K230_PWR -> U6  TPS62823  0V8   (0.798 V)  --PG--+     (IO03 in the schematic until change 1)
                                                            +-> U8  TPS63802  3V3 (3.308 V)
                                                            +-> U10 TPS63802  1V8 (1.775 V) --PG_1V8--> U7 TPS62823 1V1 (1.100 V)
U7 PG -> RSTN (R30 100k to 1V8, C23 100n)        BL616 IO00 K230_RSTN -> Q4 -> RSTN (high = held in reset)
```

## Findings

### Works as designed
- **Cold power-up meets every rule in the K230 Hardware Design Guide.** Modelled with datasheet
  timings: 0V8 reaches 90 % at 1.1 ms, 0.5 ms before 1V8 and 3V3 start; 1V1 follows at 2.9 ms;
  RSTN crosses 1.26 V at 15.4 ms, 12.5 ms after the last rail. 0V8_MIPI/1V8_MIPI and
  1V8_RTC/1V8_LDO inherit the order through their beads.
- **K230 PMU cold start**: INT4 is pulled up to 1V8 (R45), which is the documented cold-start trigger.
- **Voltage domains**: all six K230 IO banks and VDD3P3_SD are on 3V3, matching the BL616's 3.3 V IO.
- **Leak wake from hibernate is possible** without a hardware change: GPIO20 is ADC channel 0, and
  the always-on comparator ACOMP1 can watch it and wake HBN (SDK example `pmu/bl616/hbn_acomp`).
- **Programming**: BL616 boot strap IO2 has R35 100k + the module's 33k pull-down, and USB + BOOT +
  EN are on PR1, so the UART stays free for the K230 link.
- **CH340X** is powered from switched 3V3, so it cannot back-power an off K230.
- **BL616 CHIP_EN timing**: the module already has R2 33k to VDD33 and C11 100 nF on CHIP_EN
  (Ai-M62-CBS spec, Figure 7). With R38 100k in parallel, EN rises with tau ~2.5 ms, well past the
  datasheet's 0.1 ms minimum after the supply.

### Must be handled, and is, in this firmware
1. **1V8 and 3V3 do not discharge.** TPS62823 (0V8, 1V1) actively discharges at >= 75 mA, but the
   TPS63802 (1V8, 3V3) has no output discharge. After power-off the core is gone in 0.5 ms while the
   IO rails take 0.6 s to 3.7 s to fall below 10 %, depending on the unknown off-state load. Powering
   up again sooner brings 0V8 up *after* 1V8/3V3, the order the guide forbids. The firmware enforces
   `K230_MIN_OFF_MS` = 5000 and checks that 3V3 reads low on IO01 (via R32) before re-enabling.
2. **Back-feed through the 10 k pull-ups.** SDIO and UART pull-ups (R34 R36 R43 R44 R57 R65 R70) go to
   switched 3V3. Any BL616 pin driven high while the K230 is off pushes current into the dead rail:
   one UART TX idling high holds 3V3 at up to 2.9 V. The firmware parks all eight pins in analog
   mode before K230_PWR drops and attaches the UART only after 3V3 is up.
3. **SDK console is on the K230 link pins.** The BL616 dev-board console defaults to UART0 on
   GPIO21/22, which on LEAKCAM are AI_TXD/AI_RXD. `defconfig` moves the console to USB CDC (PR1).
4. **LEAK_SENS dry level is not a logic level.** 1.65 V sits between VIL (0.99 V) and VIH (2.31 V).
   The pin stays analog and is read by ACOMP1 at 0.825 V (trips below ~500 k between probes,
   ~406 k with 2 x 47 k series resistors).
5. **HBN wakes only on GPIO16-19, RTC and ACOMP**, and releases every non-AON pad, so K230_PWR falls to
   R19's pull-down. The K230 can only run while the BL616 is awake; the firmware turns it off first.
6. **K230 GPIO2 is JTAG_TCK at reset**, so the heartbeat is edges, not a level, and the device tree
   must mux IO2 to GPIO and enable UART1 on IO40/IO41.
7. **USB plug wake** (after the pin swap): PGOOD on IO03 is watched by ACOMP0 at 1.65 V and wakes HBN
   on its falling edge; the leak line keeps ACOMP1. On USB the BL616 never hibernates: the charger's
   power path runs the system from USB, so staying awake costs the cell nothing.
   Requires schematic change 1.

### Humidity (AHT20)
- **Periodic only.** The AHT20 has no interrupt or alert pin (pins: NC, VDD, SCL, SDA, GND, NC), so
  it cannot wake anything. HBN's RTC wake is used: every `HUM_PERIOD_S` (600 s) the BL616 boots,
  measures (80 ms), and hibernates again. The K230's requested interval is served in these slices;
  the number left is kept in the HBN status register across the reboots.
- **I2C verified against the netlist and datasheets:** SCL on IO28 and SDA on IO29, which the module
  pin table lists as I2C_SCL / I2C_SDA; R73/R74 4.7 k pull-ups and VDD on 3V3_SLEEP, so the sensor
  and bus stay powered in hibernate; address 0x38 (datasheet writes 0x70/0x71); 100 kHz, inside the
  datasheet's 10-400 kHz, and samples far apart from the >= 1 s minimum period.
- In hibernate the BL616 releases IO28/IO29, the pull-ups hold the bus idle-high and no current
  flows; the AHT20 sleeps at <= 0.2 uA.
- **Alarm:** >= 85 %RH starts a K230 session with reason `humid`; re-armed below 75 %RH. Every session
  also gets `ENV,<rh_x10>,<t_x10>`, which the agent passes to the hook as `LEAKCAM_RH` / `LEAKCAM_T`.
- **Cost (estimate, to measure):** ~120 ms awake per sample at an assumed 8-15 mA MCU-only current
  (the datasheet gives only 38 mA with the radio receiving) = 1-1.8 mAs, so 1.7-3 uA average at 10 min.
- The RTC runs from the 32.768 kHz crystal Y3 (`rtc_use_crystal()`), RC32K until it has started.

### USB / BLE mode
- On boot, if PGOOD is low: FreeRTOS + BLE (bring-up as the SDK's `examples/btble/peripheral`),
  advertising as `LEAKCAM-xxyy` with one service, `4c43a000-4c45-4b43-414d-000000000001`:
  SSID (...0002), passphrase (...0003), commit (...0004, write 0x01). All three need an encrypted
  link. The credentials are stored in easyflash on the BL616, which is the Wi-Fi device.
- Pairing is LE Secure Connections Just Works (no display, no buttons) and is accepted only while
  USB power is present: plugging in is the proof of physical access.
- The probes are still watched; a leak on USB starts a normal K230 session.
- Unplug (debounced 1 s): BLE stops, the BL616 reboots into the battery path with a flag that
  suppresses the cold-start K230 session, and hibernates with both comparators armed.

### Optional hardware change: bleed resistors
- **1 k on 1V8 and on 3V3** (0402). They draw 1.8 mA and 3.3 mA only while the K230 is on (under 1 %
  of its power) and nothing in idle. They shorten the safe off-time from ~5 s to ~0.1 s and make it
  independent of what is plugged in.
- **Not needed for correctness.** The firmware already waits `K230_MIN_OFF_MS` before any restart,
  and recovers a hung K230 with an RSTN pulse first (rails stay on, no wait); a full power-cycle
  is only the second step. Worth adding only if a fast power-cycle matters or the off-state load
  turns out lighter than 50 uA at bring-up (check item 2). They do not fix finding 2.

### Architecture consequence
The BL616 is also the K230's Wi-Fi (SDIO, `examples/wifi/sdio_wifi` with the `nethub` Linux host
driver). There is one BL616 firmware, so in production this power manager becomes a task inside
the SDIO Wi-Fi application, and the Wi-Fi low-power firmware and this HBN policy have to agree.

## How this was verified
- Sequence: `sim/power_sequence.py`, datasheet timings (TPS62823 SLVSDV8, TPS63802 SLVSEU9D) and
  schematic R/C values. Off-state rail loads are unknown and swept.
- BL616 code: every source compiled for riscv32 against the current bouffalo_sdk headers (API names,
  macros and struct fields all resolve). Not linked or flashed: the T-Head toolchain is x86-64 only.
- Link protocol: BL616 parser tested on the host against agent-formatted frames mixed with boot
  noise, bad checksums and over-long lines.
- K230 agent: builds natively with `-Wall -Wextra -Werror`; not run on a K230.
- AHT20: `aht20.c` compiled against the SDK I2C driver; CRC, frame layout and conversions tested on
  the host against the datasheet's own example (ST 0x2FFAB = -12.5 C) and the CRC-8 check value.
- BLE mode: `ble_pairing.c` and `main.c` compiled against the SDK's BLE host, controller, RF and
  easyflash headers with the stack's own build defines. This caught one real bug: the 128-bit UUID
  macro shifts its last field by 40 bits, so it must be a 64-bit literal. The resulting service
  UUID was checked byte for byte on the host.

## Bring-up checklist
1. Scope K230_PWR and K230_RSTN while the BL616 resets and while it is being flashed: both must stay
   in the safe state (PWR low, RSTN pin high). The BL616's reset-default pad pull is not documented.
2. Measure 1V8 and 3V3 decay after power-off with cameras and USB plugged in; set `K230_MIN_OFF_MS`.
3. Check IO01 really follows 3V3 while the K230 is off (it relies on K230 GPIO2 staying high-impedance).
4. Confirm the HBN wake reason survives the reboot (`wake_reason_get()`); fall back to
   `HBN_Get_Reset_Event()` if the BootROM clears the interrupt state.
5. Measure HBN current with ACOMP1 enabled against the datasheet's 2.1 uA.
6. Measure K230 boot time from SPI NAND to agent `READY`; tighten `BOOT_TIMEOUT_MS` (90 s now).
7. Plug USB during HBN: the BL616 must boot into BLE mode within about a second. Measure HBN current
   with both comparators enabled.
8. Pair from a phone (nRF Connect is enough), write SSID, passphrase and 0x01, reboot, confirm the
   credentials are read back from easyflash. Confirm pairing is refused on battery.
9. Read the AHT20 on the bench against a reference hygrometer; confirm the 10-minute RTC wake
   interval with the crystal (drift over a day) and the current per wake.
