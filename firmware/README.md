# LEAKCAM power management: simulation and proof-of-concept firmware

The BL616 (Ai-M62-CBS, U11) runs from the always-on `3V3_SLEEP` rail and owns the K230's power.
It wakes on a leak or on its RTC, powers the K230 up, lets it capture, and powers it down again.

| Path | What it is |
|---|---|
| `sim/power_sequence.py` | rail-by-rail timing model of the EN/PG chain, checks the K230 sequencing rules; output in `sim/power_sequence_report.txt` |
| `bl616_pwrmgr/` | BL616 firmware, bouffalo_sdk layout (`make CHIP=bl616 BOARD=bl616dk`) |
| `k230_agent/` | K230 Linux daemon: heartbeat, UART protocol, sync and read-only remount before power-off |

## Power chain as built (POWER.SchDoc)

```
BL616 IO03 K230_PWR -> U6  TPS62823  0V8   (0.798 V)  --PG--+
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

## Bring-up checklist
1. Scope K230_PWR and K230_RSTN while the BL616 resets and while it is being flashed: both must stay
   in the safe state (PWR low, RSTN pin high). The BL616's reset-default pad pull is not documented.
2. Measure 1V8 and 3V3 decay after power-off with cameras and USB plugged in; set `K230_MIN_OFF_MS`.
3. Check IO01 really follows 3V3 while the K230 is off (it relies on K230 GPIO2 staying high-impedance).
4. Confirm the HBN wake reason survives the reboot (`wake_reason_get()`); fall back to
   `HBN_Get_Reset_Event()` if the BootROM clears the interrupt state.
5. Measure HBN current with ACOMP1 enabled against the datasheet's 2.1 uA.
6. Measure K230 boot time from SPI NAND to agent `READY`; tighten `BOOT_TIMEOUT_MS` (90 s now).
