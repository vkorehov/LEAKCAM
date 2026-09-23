#!/usr/bin/env python3
"""LEAKCAM K230 power-rail sequence simulation (time-stepped, 5 us).

Models the enable/power-good chain exactly as drawn in POWER.SchDoc:

  BL616 IO03 (K230_PWR) --R51 100k--> U6 EN   TPS62823 -> 0V8  (0V8_MIPI via bead)
  U6 PG  (R27 100k pull-up to K230_PWR) ----> U8 EN  TPS63802 -> 3V3
                                          `-> U10 EN TPS63802 -> 1V8  (1V8_DDR, 1V8_MIPI via beads)
  U10 PG (R28 100k pull-up to K230_PWR, net PG_1V8) -> U7 EN TPS62823 -> 1V1
  U7 PG  (R30 100k pull-up to 1V8, C23 100n) -> K230 RSTN, also pulled low by Q4 when
         BL616 IO00 (K230_RSTN) is high.

Datasheet numbers used (files in ~/k230d-hw):
  TPS62823 (SLVSDV8): EN rising 0.9 V typ; soft start begins ~250 us after EN, output
    rise ~1 ms (tSS 1.25 ms); PG hi-Z ~100 us after regulation, 96 % threshold; when EN
    is low PG is driven low and the output is ACTIVELY DISCHARGED, IDIS 75 mA min.
  TPS63802 (SLVSEU9D): EN threshold 1.1 V; Tdelay 321 us EN->first switching; Tramp
    224 us to PG (measured with the datasheet's own output cap; scaled here by C);
    PG hi-Z above 95 %, low below 90 %; NO output discharge (ISD into VOUT +-0.5 uA), so
    a disabled rail decays only through whatever load is still on it.
  K230 Hardware Design Guide v1.7, "Power-Up Sequence": VDD0P8_CORE must be powered up
    earlier than VDD1P8 and VDDIO3P3_0..5; AVDD0P8_MIPI earlier than AVDD1P8_MIPI;
    AVDD1P8_RTC not later than AVDD1P8_LDO. Nothing is said about power-down.

Rail capacitance is the nominal sum from the schematic (DC bias derating ignored, so
real decays are somewhat faster). Off-state rail loads are NOT known; they are swept.

Usage:  python3 power_sequence.py            # full report
"""
import math

DT = 5e-6

# nominal capacitance per rail, sum of schematic values (see report header)
C = {'0V8': 46.2e-6, '1V1': 22.8e-6, '1V8': 22.8e-6, '3V3': 24.3e-6}
VT = {'0V8': 0.798, '1V1': 1.100, '1V8': 1.775, '3V3': 3.308}   # from FB dividers


class Tps62823:
    """Buck with EN delay, soft-start ramp, delayed PG and active output discharge."""
    def __init__(self, rail):
        self.rail, self.v, self.on_t, self.pg, self.pg_t = rail, 0.0, None, False, None

    def step(self, t, en_v, r_load):
        vt = VT[self.rail]
        if en_v > 0.9 and self.on_t is None: self.on_t = t
        if en_v < 0.7: self.on_t = None
        if self.on_t is not None and t - self.on_t > 250e-6:
            self.v = min(vt, self.v + vt / 1.0e-3 * DT)          # ~1 ms controlled rise
        else:
            i = (75e-3 if self.on_t is None else 0.0) + self.v / r_load
            self.v = max(0.0, self.v - i / C[self.rail] * DT)
        good = self.on_t is not None and self.v >= 0.96 * vt
        if good and self.pg_t is None: self.pg_t = t
        if not good: self.pg_t = None
        self.pg = good and t - self.pg_t >= 100e-6
        return self.v


class Tps63802:
    """Buck-boost: 321 us enable delay, fast current-limited ramp, no output discharge."""
    def __init__(self, rail):
        self.rail, self.v, self.on_t, self.pg = rail, 0.0, None, False

    def step(self, t, en_v, r_load, i_backfeed=0.0):
        vt = VT[self.rail]
        if en_v > 1.1 and self.on_t is None: self.on_t = t
        if en_v < 1.0: self.on_t = None
        if self.on_t is not None and t - self.on_t > 321e-6:
            # datasheet Tramp (224 us) assumes its own 2 x 22 uF output; scale linearly with C.
            ramp = 224e-6 * C[self.rail] / 44e-6
            self.v = min(vt, self.v + vt / ramp * DT)
        else:
            i = self.v / r_load - i_backfeed(self.v) if callable(i_backfeed) else self.v / r_load
            self.v = max(0.0, self.v - i / C[self.rail] * DT)
        if self.on_t is not None and self.v >= 0.95 * vt: self.pg = True
        elif self.v < 0.90 * vt or self.on_t is None: self.pg = False
        return self.v


def run(events, t_end, r_off_1v8, r_off_3v3, backfeed_pins=0, bl616_holds_reset_ms=None,
        record=False):
    """events: list of (time_s, K230_PWR level). Returns trace summary dict."""
    u6, u7 = Tps62823('0V8'), Tps62823('1V1')
    u8, u10 = Tps63802('3V3'), Tps63802('1V8')
    k230_pwr, rstn = 0.0, 0.0
    ev = sorted(events)
    # back-feed: BL616 pins driven to 3.3 V into 3V3 through 10 k pull-ups (R34/R36/R43/R44/
    # R57/R65/R70) while the rail itself is off
    bf = (lambda v: backfeed_pins * max(0.0, 3.3 - v) / 10e3) if backfeed_pins else 0.0
    trace, marks = [], {}
    t, k = 0.0, 0
    tau_rstn = 100e3 * 100e-9
    while t < t_end:
        while k < len(ev) and ev[k][0] <= t:
            k230_pwr = 3.3 if ev[k][1] else 0.0; k += 1
        v08 = u6.step(t, k230_pwr, 1e9)
        pg08 = k230_pwr if u6.pg else 0.0                 # open drain, R27 to K230_PWR
        v33 = u8.step(t, pg08, r_off_3v3, bf)
        v18 = u10.step(t, pg08, r_off_1v8)
        pg18 = k230_pwr if u10.pg else 0.0                # R28 to K230_PWR
        v11 = u7.step(t, pg18, 1e9)
        hold = bl616_holds_reset_ms is not None and (t - (ev[0][0] if ev else 0)) < bl616_holds_reset_ms * 1e-3
        if u7.pg and not hold:
            rstn += (v18 - rstn) * DT / tau_rstn           # R30 100k / C23 100n towards 1V8
        else:
            rstn = 0.0
        for name, v in (('0V8', v08), ('1V1', v11), ('1V8', v18), ('3V3', v33)):
            for frac in (0.1, 0.9):
                key = (name, frac)
                if key not in marks and v >= frac * VT[name]: marks[key] = t
        if 'RSTN' not in marks and rstn >= 0.7 * 1.8: marks['RSTN'] = t
        if record and int(t / DT) % 20 == 0:
            trace.append((t, k230_pwr, v08, v18, v33, v11, rstn))
        t += DT
    return {'marks': marks, 'trace': trace, 'v': {'0V8': u6.v, '1V1': u7.v, '1V8': u10.v, '3V3': u8.v}}


def ms(x): return '%7.3f ms' % (x * 1e3) if x is not None else '   never  '


def powerup_report():
    r = run([(0.0, 1)], 0.030, 36e3, 66e3, record=True)
    m = r['marks']
    print('== 1. Cold power-up, K230_PWR goes high at t = 0 ==')
    for name in ('0V8', '3V3', '1V8', '1V1'):
        print('   %-4s 10 %% at %s   90 %% at %s' % (name, ms(m.get((name, .1))), ms(m.get((name, .9)))))
    print('   RSTN crosses 1.26 V (0.7 x 1V8) at %s' % ms(m.get('RSTN')))
    ok1 = m[('0V8', .9)] < min(m[('1V8', .1)], m[('3V3', .1)])
    ok4 = m['RSTN'] > max(m[(n, .9)] for n in VT)
    print('   rule "0V8 before 1V8 and 3V3 IO":        %s  (0V8 at 90 %% %.0f us before the first IO rail starts)'
          % ('PASS' if ok1 else 'FAIL', (min(m[('1V8', .1)], m[('3V3', .1)]) - m[('0V8', .9)]) * 1e6))
    print('   rule "0V8_MIPI before 1V8_MIPI":          PASS  (beads from 0V8 and 1V8, inherit the above)')
    print('   rule "1V8_RTC not later than 1V8_LDO":    PASS  (both on the 1V8 net)')
    print('   reset released after all rails at 90 %%:  %s  (%.1f ms margin, set by R30 x C23 = 10 ms)'
          % ('PASS' if ok4 else 'FAIL', (m['RSTN'] - max(m[(n, .9)] for n in VT)) * 1e3))
    return r


def powerdown_report():
    print('\n== 2. Power-down, K230_PWR low at t = 0 (all ENs fall together: R27/R28 pull up to K230_PWR) ==')
    for label, r18, r33 in (('light off-load (1V8 50 uA, 3V3 50 uA)', 36e3, 66e3),
                            ('typical (1V8 0.5 mA, 3V3 0.3 mA)', 3.6e3, 11e3),
                            ('with 1 k bleed on 1V8 and 3V3', 36e3 * 1e3 / (36e3 + 1e3), 66e3 * 1e3 / (66e3 + 1e3))):
        # time for each rail to fall below 10 % after the off edge, analytic for the RC rails
        t08 = VT['0V8'] / (75e-3 / C['0V8'])
        t11 = VT['1V1'] / (75e-3 / C['1V1'])
        t18 = r18 * C['1V8'] * math.log(10)
        t33 = r33 * C['3V3'] * math.log(10)
        print('   %-40s 0V8 %s  1V1 %s  1V8 %s  3V3 %s' % (label, ms(t08), ms(t11), ms(t18), ms(t33)))
    print('   -> the core (0V8) is gone in ~0.5 ms while 1V8 and 3V3 linger; the IO rails outlive the core.')


def powercycle_report():
    print('\n== 3. Power-cycle: off for T, then on again. Does 0V8 still come up first? ==')
    print('   PASS means 1V8 < 10 % and 3V3 < 10 % at the moment 0V8 starts rising again.')
    gaps = (0.01, 0.1, 0.3, 1.0, 2.0, 5.0)
    print('   %-44s ' % 'scenario' + ' '.join('%6.2fs' % g for g in gaps))
    scen = (('light off-load, no bleed', 36e3, 66e3, 0),
            ('typical off-load, no bleed', 3.6e3, 11e3, 0),
            ('1 k bleed on 1V8 and 3V3', 1e3 * 36e3 / 37e3, 1e3 * 66e3 / 67e3, 0),
            ('1 k bleed, BL616 UART TX left high', 1e3 * 36e3 / 37e3, 1e3 * 66e3 / 67e3, 1),
            ('1 k bleed, UART + 5 SDIO pins left high', 1e3 * 36e3 / 37e3, 1e3 * 66e3 / 67e3, 6),
            ('no bleed, UART TX left high', 36e3, 66e3, 1))
    for label, r18, r33, bfp in scen:
        row = []
        for g in gaps:
            # analytic decay is enough here: after the off edge 1V8/3V3 are plain RC (+ back-feed)
            v18 = VT['1V8'] * math.exp(-g / (r18 * C['1V8']))
            if bfp:
                # Thevenin: rail load r33 vs bfp x 10 k to 3.3 V
                rp = 10e3 / bfp
                vinf = 3.3 * r33 / (r33 + rp); tau = (r33 * rp / (r33 + rp)) * C['3V3']
                v33 = vinf + (VT['3V3'] - vinf) * math.exp(-g / tau)
            else:
                v33 = VT['3V3'] * math.exp(-g / (r33 * C['3V3']))
            ok = v18 < 0.1 * VT['1V8'] and v33 < 0.1 * VT['3V3']
            row.append('  PASS ' if ok else '  FAIL ')
        print('   %-44s ' % label + ''.join(row))


def backfeed_report():
    print('\n== 4. K230 "off" but BL616 pins driven high into the 10 k pull-ups to 3V3 ==')
    for pins in (1, 2, 7):
        for r33, lab in ((66e3, 'light load'), (11e3, 'typ load'), (1e3 * 66e3 / 67e3, '1 k bleed')):
            rp = 10e3 / pins
            v = 3.3 * r33 / (r33 + rp); i = (3.3 - v) / rp
            print('   %d pin(s) high, %-10s -> 3V3 rail sits at %.2f V, costing %.2f mA from 3V3_SLEEP'
                  % (pins, lab, v, i * 1e3))
    print('   -> with the UART TX idling high the "off" K230 IO banks, cameras and CH340X sit at up to 2.9 V,')
    print('      the idle budget grows by 0.04-0.3 mA per pin (more with a bleed resistor), and the next')
    print('      power-up starts with 3V3 already up. Bleed resistors cannot fix this; the firmware must park')
    print('      those pins (analog/input, no pull) before K230_PWR goes low and keep them parked until 3V3 is up.')


if __name__ == '__main__':
    powerup_report()
    powerdown_report()
    powercycle_report()
    backfeed_report()
