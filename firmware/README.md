# LEAKCAM power management: simulation and proof-of-concept firmware

The BL616 (Ai-M62-CBS, U11) runs from the always-on `3V3_SLEEP` rail and owns the K230's power.
It wakes on a leak or on its RTC, powers the K230 up, lets it capture, and powers it down again.
When USB power is plugged in it wakes, stays awake and advertises over BLE so a phone can set the
Wi-Fi credentials; unplugging returns it to the battery schedule. Every 10 minutes it wakes on its
RTC to read the AHT20 humidity sensor and goes back to sleep without the K230 unless humidity is high.

## Schematic changes for the first fab (done)

Both are in the schematic (checked 2026-09-24); the firmware in this folder is written for this
pin map only.

1. **USB-plug wake.** PGOOD (BQ24072 U1.7, R23 100k to 3V3_SLEEP) is on U11.7 IO03 (ADC_CH3,
   ACOMP0), and K230_PWR (R19 100k pull-down) is on U11.43 IO30. HBN wakes only on GPIO16-19, the
   RTC and the two always-on comparators, which only select ADC_CH0-7.
2. **AHT20 (U17) supply filter.** The datasheet (Aosong, 2024-07, figure 15) asks for an RC filter
   on VDD; 3V3_SLEEP also carries the BL616's Wi-Fi transmit bursts. U17 VDD is now a filtered node
   (R52, C4, C9). R73/R74 pull-ups stay on 3V3_SLEEP (note 1: same supply as the sensor).

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
1. **1V8 and 3V3 do not discharge by themselves.** TPS62823 (0V8, 1V1) actively discharges at
   >= 75 mA, but the TPS63802 (1V8, 3V3) has no output discharge. RD1 and RD2 (1.5 k to GND on 1V8
   and 3V3) do it instead: after power-off the core is gone in 0.5 ms and the IO rails are below 10 %
   after 56-82 ms, whatever else loads them (`sim/power_sequence_report.txt`). Powering up again
   sooner brings 0V8 up *after* 1V8/3V3, the order the guide forbids, so `k230_power_off()` waits
   300 ms after cutting the rails.
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
- **Alarm:** >= 85 %RH wakes the K230 with reason `humid`, at every sample while it stays that
  high: the BL616 keeps no memory of what was reported, the K230 decides. Every session
  gets the sample in `WAKE,<reason>,<unix s>,<rh_x10>,<t_x10>,<probe_mv>`, together with the probe
  node voltage; the agent passes them to `leakcam_wake` as `LEAKCAM_RH` / `LEAKCAM_T` /
  `LEAKCAM_PROBE_MV`, and they take part in the leak decision whatever the cameras see.
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

### Discharge resistors RD1 / RD2 (fitted)
- **1.5 k on 1V8 (RD1) and on 3V3 (RD2)** (0402). They draw 1.2 mA and 2.2 mA only while the K230 is
  on (under 1 % of its power) and nothing in idle. They make the safe off-time ~0.1 s, independent
  of what is plugged in.
- **Recovery** stays RSTN first (rails on, no wait); a full power-cycle is the second step and now
  costs 300 ms instead of seconds.
- **They do not fix finding 2:** a pin left high still feeds the rail (0.4 V and 0.3 mA per pin
  against RD2).

### Wall clock and state across hibernate (`aon_state.c`)
- **The K230 cannot keep time between wakes.** Its RTC runs from AVDD1P8_RTC on the switched 1V8
  rail, so the Y1 crystal stops with the K230 at every power-off.
- **The BL616 keeps it.** Its HBN RTC is a 40-bit counter on the Y3 32.768 kHz crystal that keeps
  counting through HBN level 0 and software resets; `pm_hbn_mode_enter()` only adds a compare
  value to it. The wall clock is a (Unix seconds, RTC count) reference pair next to it, rebased
  at every boot (the counter wraps after 388 days). Drift: the crystal's, about 1.7 s/day.
- **Time sources:** the BL616 puts its time in the reply to READY, `WAKE,<reason>,<unix s>`, and
  the agent sets the K230 clock from it; 0 means the BL616 clock is not valid. The K230 sends
  `TIME,<unix s>` once it is NTP-synchronised (Wi-Fi). The clock is lost only with 3V3_SLEEP
  (battery out) or an EN reset; WAKE then carries 0 until the K230 has real time again.
- **Found and fixed:** the session flags used `HBN_Set_Status_Flag()` = HBN_RSV0, which
  `pm_hbn_mode_enter()` overwrites with its own HBN_STATUS_ENTER_FLAG on the way into hibernate:
  the humidity wake count, leak-reported and USB-mode flags were lost at every sleep. Flags and
  clock now live in the last 64 bytes of HBN RAM (retention enabled before every sleep, magic +
  CRC-32); HBN_RSV1 (SDK wake callback) and RSV3 (ROM patch code) are not free either.

### Architecture consequence
The BL616 is also the K230's Wi-Fi: Bouffalo's NetHub bridge over SDIO (`bl616/wifi_link.c`) with
our own RT-Smart driver on the K230 (`k230_board/rtsmart/drivers/bl616_nethub/`), described in
`bl616/WIFI.md`. One BL616 firmware (`bl616/`): Wi-Fi starts when the K230 sends `WIFI` (NAK code 2
without stored credentials), and stops before the K230's rail goes down. NetHub's own low-power
mode stays off: between sessions the BL616 hibernates, which ends the association anyway.

### K230 capture and image history (`k230_capture/`, RT-Smart)
- `leakcam_wake`, run by the agent at every wake: both OV5647s through MPP VICAP (`vicap_cap.c`,
  offline mode, 1280x960 binned), white and IR chains on during the shot (25 kHz PWM on
  GPIO61/GPIO60, `led_rtsmart.c`), frames reduced to 320x240 and compared with what the history
  shows (16x12 blocks, image-circle mask, gain normalised). Anything new goes to the server over
  Wi-Fi; a leak adds 5 s of video, no leak stores the frames. The algorithm is in DESIGN.md 5.1.
- History on the SPI NAND, because the K230 loses its RAM at every power-off: per camera a
  keyframe (whole 1280x960 luminance, deflate via the bundled miniz) and deltas holding only the changed 80x80 blocks;
  new keyframe on more than half the image changed or after 96 deltas; 32 MB quota per camera,
  oldest whole group deleted first. Every file is written tmp + fsync + rename with a CRC.
- `leakcam_hist list <cam>` / `get <cam> <seq> out.pgm` rebuilds any stored frame.
- Record times come from the BL616 (see "Wall clock" below): the K230 sets its clock from the
  time in the WAKE frame at every wake.
- Build and board port steps: `k230_capture/rtsmart/README.md`.

## How this was verified
- Sequence: `sim/power_sequence.py`, datasheet timings (TPS62823 SLVSDV8, TPS63802 SLVSEU9D) and
  schematic R/C values. Off-state rail loads are unknown and swept.
- BL616 code: every source compiled for riscv32 against the current bouffalo_sdk headers (API names,
  macros and struct fields all resolve), and the whole firmware links into
  `leakcam_bl616.bin` in the build container (BUILD.md). Not flashed yet.
- Link protocol: BL616 parser tested on the host against agent-formatted frames mixed with boot
  noise, bad checksums and over-long lines.
- K230 agent: its link code runs on the host against the BL616's `k230_link.c` over a lossy
  socket pair (`k230_agent/test`); not run on a K230.
- K230 capture: `make test` checks the change detector (noise,
  exposure, puddle, mask, torn reference) and the history (keyframe + stacked deltas rebuild
  byte-exact, policy, torn delta, stale tmp, quota), the latter also under ASan/UBSan. No camera run.
- Always-on state: `test/test_aon.c` on the host (clock across the 40-bit wrap, 144 rebases
  without drift, counter-reset detection, CRC); `aon_state.c` compiled against the SDK HBN/RTC headers.
- AHT20: `aht20.c` compiled against the SDK I2C driver; CRC, frame layout and conversions tested on
  the host against the datasheet's own example (ST 0x2FFAB = -12.5 C) and the CRC-8 check value.
- BLE mode: `ble_pairing.c` and `main.c` compiled against the SDK's BLE host, controller, RF and
  easyflash headers with the stack's own build defines. This caught one real bug: the 128-bit UUID
  macro shifts its last field by 40 bits, so it must be a 64-bit literal. The resulting service
  UUID was checked byte for byte on the host.

## Bring-up checklist
1. Scope K230_PWR and K230_RSTN while the BL616 resets and while it is being flashed: both must stay
   in the safe state (PWR low, RSTN pin high). The BL616's reset-default pad pull is not documented.
2. Measure 1V8 and 3V3 decay after power-off (expected < 0.1 s with RD1/RD2); confirm the 300 ms wait in `k230_power_off()`.
3. Scope the heartbeat on IO01 after READY: an edge every 500 ms, and K230_PWR low 2 s after the
   last one (the level before READY means nothing:
   the K230 pad is JTAG_TCK with a pull-down in reset, against R32 100 k).
4. Confirm the HBN wake reason survives the reboot (`wake_reason_get()`); fall back to
   `HBN_Get_Reset_Event()` if the BootROM clears the interrupt state.
5. Measure HBN current with ACOMP1 enabled against the datasheet's 2.1 uA (and with HBN RAM
   retention on). Check the aon block survives an HBN wake and a software reset, and the RTC
   drift over a day against NTP.
6. Measure K230 boot time from SPI NAND to agent `READY`; tighten `BOOT_TIMEOUT_MS` (90 s now).
7. Plug USB during HBN: the BL616 must boot into BLE mode within about a second. Measure HBN current
   with both comparators enabled.
8. Pair from a phone (nRF Connect is enough), write SSID, passphrase and 0x01, reboot, confirm the
   credentials are read back from easyflash. Confirm pairing is refused on battery.
9. Read the AHT20 on the bench against a reference hygrometer; confirm the 10-minute RTC wake
   interval with the crystal (drift over a day) and the current per wake.
