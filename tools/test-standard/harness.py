"""Unattended test runs that fill the standard metrics (check_tests.py) of a test README - only cells that still say
**MISSING**; readings already recorded (the user's meter readings above all) are never overwritten (user, 2026-10-25:
"no need to repeat manual measurements if taken, only missing").

A test's run.py:

    from harness import Run
    r = Run(__file__)                   # reads the README: which metric rows are still MISSING
    fd = r.boot()                       # K230 cold boot from NAND, timed (pt_run -> SPL -> OpenSBI -> RT-Smart -> msh)
    r.sample("idle")                    # die / board temperature, VBAT, BL616 mem, K230 free
    r.meter("msh idle")                 # asks for the USB meter only while the current row is MISSING
    ... the test's own operation, r.sample() after each step ...
    r.set("speed", "...")               # the test's own rows (key or a regex of the row name)
    r.finish("result text", ok=True)    # fills MISSING cells, the run + obstacles into HISTORY.md, switches off

Keys: boot share runtime speed current voltage energy perday temp memory image flags features optimised, or a regex.
Values the harness fills by itself when still MISSING: boot, runtime, temp, memory, voltage (VBAT on the K230 ADC,
test023 calibration), image (the kdimg / BL616 binary sizes). A key set to None stays MISSING.

Meter readings: only with LEAKCAM_METER=ask (otherwise only readings cached the same day are used); the runner prints ">>> METER <state>" and waits up to METER_WAIT_S for a line in the FIFO
~/.leakcam_meter (`echo 0.24 > ~/.leakcam_meter`, amps); no answer leaves the row MISSING.
"""
import datetime
import glob
import json
import os
import re
import select
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "power-bringup"))
sys.path.insert(0, HERE)
from board import bl, cmd, k230, log   # noqa: E402
from check_tests import MISSING, REQUIRED, metric_rows, metrics_block   # noqa: E402
from mem_leak import leak_verdict   # noqa: E402

KDIMG = os.path.expanduser("~/k230_rtos_sdk/output/k230d_rtos_leakcam_defconfig/"
                           "LEAKCAM_K230D_rtsmart_local_nncase_v2.11.0.kdimg")
BL616_BIN = glob.glob(os.path.join(REPO, "tools/power-bringup/bl616_powertest/build/build_out/bl616_powertest_bl616.bin"))
METER_FIFO = os.path.expanduser("~/.leakcam_meter")
METER_WAIT_S = 600
METER_CACHE = os.path.expanduser("~/.leakcam_meter_cache.json")
BAT_FULL_SCALE_MV = 5329          # test023 calibration (attic/firmware/k230_agent/leakcam_agent.c)
# the flags of the builds on the board (2026-10-25): ~/k230_rtos_sdk rtconfig.py (LEAKCAM: kernel opt),
# attic/firmware/k230_capture/rtsmart/Makefile (LEAKCAM_OPT), bouffalo_sdk cmake/compiler_flags.cmake (no
# CONFIG_GCC_OPTIMISE_LEVEL in the test firmware's defconfig)
FLAGS = ("K230 kernel -O1 -g, -ffunction/-fdata-sections, --gc-sections; programs -Os -march=rv64imafdcv, sections + "
         "--gc-sections, strip, no LTO; BL616 test firmware -Os -g (SDK Kconfig default), no LTO")
KEYS = {"boot": 0, "share": 1, "runtime": 2, "speed": 3, "current": 4, "voltage": 5, "energy": 6, "perday": 7,
        "temp": 8, "memory": 9, "image": 10, "flags": 11, "features": 12, "optimised": 13}
MILESTONES = [("SPL", b"U-Boot SPL"), ("OpenSBI", b"OpenSBI"), ("RT-Smart", b"Thread Smart"),
              ("mounted", b"/dev/shm file system"), ("msh", b"msh />")]


class Run:
    def __init__(self, run_py):
        self.dir = os.path.dirname(os.path.abspath(run_py))
        self.name = os.path.basename(self.dir)
        self.readme = os.path.join(self.dir, "README.md")
        self.date = datetime.datetime.utcnow().strftime("%Y-%m-%d")
        self.t0 = time.time()
        self.values, self.samples, self.boots, self.notes, self.obstacles = {}, [], [], [], []
        self.leak_from = 0                      # first sample of the repeated phase (the leak verdict's window)
        self.fd, self.leak, self.console = None, False, ""
        # every K230 console exchange with its time: a failure's evidence survives the run (test027's 11-minute
        # stall of 2026-10-30 left nothing to look at)
        self.clog = open(os.path.join(self.dir, "console.log"), "w", buffering=1)
        text = open(self.readme).read()
        self.rows = metric_rows(metrics_block(text).group(1))
        self.missing = [c[0] for _, c in self.rows if MISSING in c[1]]
        log(f"{self.name}: {len(self.missing)} metric rows MISSING: {', '.join(self.missing) or 'none'}")

    # --- which row a key means ---------------------------------------------------------------------------------
    def row(self, key):
        names = [c[0] for _, c in self.rows]
        if key in KEYS:
            pat = REQUIRED[KEYS[key]][1]
            if pat is None:                       # the test's own speed row: the one no other pattern claims
                taken = {i for i, n in enumerate(names) for _, p, _, _ in REQUIRED if p and re.search(p, n)}
                free = [n for i, n in enumerate(names) if i not in taken]
                return free[0] if free else None
        else:
            pat = key
        hit = [n for n in names if re.search(pat, n)]
        return hit[0] if hit else None

    def need(self, key):
        return self.row(key) in self.missing

    def set(self, key, value):
        self.values[key] = value

    # --- board ---------------------------------------------------------------------------------------------------
    def boot(self, boot1="low", until=b"msh />", secs=20, expect_msh=True, after_on=None):
        """K230 off -> on -> reset released, timed by the console milestones; returns the console fd. after_on runs
        with the rails up and the K230 still in reset (the Wi-Fi bridge needs the 3V3: test105 started it with the
        rails off and it was refused, 2026-10-30)"""
        bl("pt_off", 2)
        time.sleep(1)
        bl(f"pt_boot1 {boot1}")
        bl("pt_on", 2)
        if after_on:
            after_on()
        if self.fd is None:
            self.fd = k230()
        cmd(self.fd, None, 0.3)
        t0 = time.time()
        bl("pt_run", 0.1)
        buf, marks = b"", {}
        while time.time() - t0 < secs:
            if select.select([self.fd], [], [], 0.05)[0]:
                buf += os.read(self.fd, 65536)
                for name, pat in MILESTONES:
                    if name not in marks and pat in buf:
                        marks[name] = time.time() - t0
            if until in buf and "msh" in marks:
                break
        if expect_msh:
            self.boots.append(marks)
        self.clog_write("boot", buf.decode(errors="replace"))
        log("boot", " ".join(f"{k} {v:.2f}" for k, v in marks.items()) or "NO CONSOLE")
        if expect_msh and "msh" not in marks:
            self.obstacle(f"K230 cold boot did not reach msh within {secs} s (milestones {marks})")
        time.sleep(1.5)                           # the app's start-up lines
        cmd(self.fd, None, 0.5)
        return self.fd

    def k(self, c, secs=3, until=r"msh />"):
        """one K230 console command; everything read is also kept in self.console, so a background program's lines
        that arrive while the sampler runs its own commands are not lost (test025, 2026-10-25)"""
        t1 = time.time()
        out = cmd(self.fd, c, secs, until)
        self.console += out
        self.clog_write(f"> {c!r} ({time.time() - t1:.2f} s)", out)
        return out

    def clog_write(self, head, text):
        t = time.time() - self.t0
        self.clog.write(f"{t:9.3f} {head}\n" + "".join(f"{t:9.3f}   {line}\n" for line in text.splitlines() if line.strip()))

    def off(self):
        bl("pt_off", 2)

    def sample(self, label):
        s = {"label": label, "t": time.time() - self.t0}
        st = bl("pt_status", 2)
        m = re.search(r"AHT20\s*: [\d.]+ %RH, (-?[\d.]+) C", st)
        s["board"] = float(m.group(1)) if m else None
        m = re.search(r"free (\d+) largest (\d+) min (\d+) blocks (\d+) used (\d+) tasks (\d+) deleted (\d+)",
                      bl("mem", 1.5))
        s["bl_mem"] = tuple(int(x) for x in m.groups()) if m else None
        if self.fd is not None and "K230_PWR    : HIGH" in st:
            m = re.search(r"chip temperature: (-?[\d.]+)", self.k("chip_temp"))
            s["die"] = float(m.group(1)) if m else None
            m = re.search(r"heap\s+(\d+)\s+(\d+)\s+(\d+)", self.k("free"))
            s["k_free"] = (int(m.group(3)), int(m.group(2))) if m else None
            self.k("adc probe adc", 2)
            raws = [int(x, 16) for _ in range(8) for x in re.findall(r"value is 0x([0-9A-Fa-f]+)", self.k("adc read 1", 2))]
            s["vbat"] = round(sum(raws) / len(raws) * BAT_FULL_SCALE_MV / 4095) if raws else None
        self.samples.append(s)
        log("sample", label, {k: v for k, v in s.items() if k not in ("label", "t")})
        return s

    def meter(self, state, force=False, busy=None):
        """one USB VBUS meter reading (amps) for `state`, asked only while the current row is MISSING (or force);
        busy: a function run over and over in a thread until the reading arrives (keeps a short load going while
        the user reads the meter);
        a state read earlier the same day by any test is reused from METER_CACHE (user, 2026-10-25: never ask a
        reading twice)"""
        if not (force or self.need("current")):
            return None
        ask = os.environ.get("LEAKCAM_METER") == "ask"
        cache = {}
        if os.path.exists(METER_CACHE):
            cache = json.load(open(METER_CACHE))
        hit = cache.get(f"{self.date} {state}")
        if hit:
            a, src = hit
            note = f"meter {state}: {a:.3f} A (read in {src})"
        elif not ask:
            log(f"meter {state}: not asked (LEAKCAM_METER=ask to be prompted)")
            return None
        else:
            if not os.path.exists(METER_FIFO):
                os.mkfifo(METER_FIFO, 0o600)
            stop = threading.Event()
            th = None
            if busy:
                def loop():
                    while not stop.is_set():
                        busy()
                th = threading.Thread(target=loop, daemon=True)
                th.start()
            log(f">>> METER {self.name}: {state} - reading in amps to {METER_FIFO}")
            fd = os.open(METER_FIFO, os.O_RDONLY | os.O_NONBLOCK)
            t0, got = time.time(), b""
            try:
                while time.time() - t0 < METER_WAIT_S and b"\n" not in got:
                    if select.select([fd], [], [], 1)[0]:
                        got += os.read(fd, 100)
            finally:
                os.close(fd)
                stop.set()
                if th:
                    th.join(120)
            try:
                a = float(got.split()[0])
            except (IndexError, ValueError):
                log(f"no meter reading for {state}")
                return None
            cache[f"{self.date} {state}"] = (a, self.name)
            json.dump(cache, open(METER_CACHE, "w"), indent=0)
            note = f"meter {state}: {a:.3f} A"
            log(f"meter {state}: {a:.3f} A")
        self.notes.append(note)
        self.samples.append({"label": f"meter {state}", "t": time.time() - self.t0, "amps": a})
        return a

    def energy(self, amps, seconds, per_day=None, idle_amps=None, what="operation"):
        """P = I x VBAT (at USB the linear charger passes the board's current: never x 5 V, test102), E = P x t,
        Q = I x t; sets the Power and Per day rows (only where still MISSING)"""
        if amps is None:
            return
        vb = [s["vbat"] for s in self.samples if s.get("vbat")]
        v = min(vb) / 1000 if vb else 3.8
        own = amps - (idle_amps or 0)
        e = (f"{amps * 1000:.0f} mA x {v:.2f} V = {amps * v * 1000:.0f} mW; per {what} ({seconds:.2f} s) "
             f"{amps * v * seconds:.2f} J, {amps * seconds / 3.6 * 1000:.0f} uAh")
        if idle_amps is not None:
            e += f"; over idle ({idle_amps * 1000:.0f} mA) {own * v * seconds:.2f} J"
        self.set("energy", e + f" ({self.date})")
        if per_day:
            mah = amps * seconds / 3.6 * per_day        # A x s / 3.6 = mAh
            self.set("perday", f"{per_day} x {what}: {mah:.2f} mAh/day, {mah * 30 / 2000 * 100:.2f} % of 2000 mAh per month "
                               f"({self.date})")

    def obstacle(self, text):
        self.obstacles.append(text)
        log("OBSTACLE", text)

    # --- the standard values from the samples ------------------------------------------------------------------
    def derived(self):
        d = {}
        ms = [b["msh"] for b in self.boots if "msh" in b]
        if ms:
            last = self.boots[-1]
            steps = list(last.items())
            gaps = [(steps[i][0], steps[i][1] - (steps[i - 1][1] if i else 0)) for i in range(len(steps))]
            longest = max(gaps, key=lambda g: g[1])
            ms.sort()
            d["boot"] = (f"pt_run -> msh {ms[len(ms) // 2]:.2f} / {ms[-1]:.2f} s (median / max of {len(ms)}; "
                         f"{', '.join(f'{k} {v:.2f}' for k, v in steps)}); longest step to {longest[0]} "
                         f"{longest[1]:.2f} s ({self.date}, run.py)")
        d["runtime"] = f"{time.time() - self.t0:.0f} s automated (run.py, {self.date}), no setup"
        die = [s["die"] for s in self.samples if s.get("die") is not None]
        brd = [s["board"] for s in self.samples if s.get("board") is not None]
        if die or brd:
            f = lambda v: f"{v[0]:.1f} / {max(v):.1f} / {v[-1]:.1f} C" if v else "n/a: K230 off"
            d["temp"] = f"die {f(die)}; board {f(brd)} (AHT20) ({self.date})"
        bm = [s["bl_mem"] for s in self.samples if s.get("bl_mem")]
        km = [s["k_free"] for s in self.samples if s.get("k_free")]
        # the leak verdict only over the repeated phase: programs starting take memory once (test105, 2026-10-30)
        steady = self.samples[self.leak_from:]
        lbm = [s["bl_mem"] for s in steady if s.get("bl_mem")]
        lkm = [s["k_free"] for s in steady if s.get("k_free")]
        if bm or km:
            parts = []
            if bm:
                parts.append(f"BL616 free {bm[0][0]} / min {min(b[2] for b in bm)} / {bm[-1][0]} B, blocks "
                             f"{bm[0][3]} -> {bm[-1][3]}, deleted {max(b[6] for b in bm)}")
            if km:
                kv = [k[0] for k in km]
                parts.append(f"K230 heap available {kv[0]} B steady" if len(set(kv)) == 1 else
                             f"K230 heap available {kv[0]} / {min(kv)} / {kv[-1]} B")
            if max(len(bm), len(km)) < 8:
                parts.append(f"leak: n/a, {max(len(bm), len(km))} snapshots (mem_leak.py needs >= 8 cycles)")
            for chip, series in (("BL616", [{"free": b[0], "blocks": b[3], "deleted": b[6]} for b in lbm]),
                                 ("K230", [{"free": k[0]} for k in lkm])):
                if len(series) >= 8:
                    leak, why = leak_verdict(series)
                    parts.append(f"{chip} {why.split(':')[0]} (mem_leak.py, {len(series)} snapshots)")
                    if leak:
                        self.leak = True
            d["memory"] = "; ".join(parts) + f" ({self.date})"
        vb = [s["vbat"] for s in self.samples if s.get("vbat")]
        if vb:
            d["voltage"] = (f"VBAT {min(vb) / 1000:.2f} V min (K230 ADC ch 1, test023 calibration, {len(vb)} samples; "
                            f"on USB: the charger rail, not the cell) ({self.date})")
        amps = [s["amps"] for s in self.samples if s.get("amps") is not None]
        if amps:
            d["current"] = f"{min(amps):.3f} / {sum(amps) / len(amps):.3f} / {max(amps):.3f} A at 5 V (USB-C 1 VBUS meter; " + \
                "; ".join(n[6:] for n in self.notes if n.startswith("meter ")) + f") ({self.date})"
        img = []
        if self.boots and os.path.exists(KDIMG):
            img.append(f"K230 kdimg {os.path.getsize(KDIMG)} B")
        if BL616_BIN:
            img.append(f"BL616 test firmware {os.path.getsize(BL616_BIN[0])} B")
        d["flags"] = FLAGS + f" ({self.date})"
        if img:
            d["image"] = ", ".join(img) + f" ({self.date}, on the board at the run)"
        return d

    # --- write back ----------------------------------------------------------------------------------------------
    def history(self, run, obstacles):
        """the run and its obstacles into HISTORY.md, newest first; the README keeps only what is current (its
        Result and Status are written by hand from the run)"""
        p = os.path.join(self.dir, "HISTORY.md")
        h = open(p).read() if os.path.exists(p) else (f"# {self.name}: history\n\nEarlier runs, superseded findings "
                                                      "and obstacles, newest first. Current state: README.md.\n\n"
                                                      "## Runs\n\n## Obstacles\n")
        h = h.replace("## Runs\n", "## Runs\n" + run + "\n", 1)
        if obstacles:
            h = h.replace("## Obstacles\n", "## Obstacles\n\n" + "\n".join(obstacles) + "\n", 1)
        open(p, "w").write(h)

    def finish(self, result, ok=True, switch_off=True):
        if switch_off:
            self.off()
        vals = self.derived()
        vals.update({k: v for k, v in self.values.items()})
        if self.leak:
            ok = False
            self.obstacle("memory leak (mem_leak.py verdict in the Memory row): a leak fails the test")
        text = open(self.readme).read()
        filled = []
        for key, v in vals.items():
            if v is None:
                continue
            name = self.row(key)
            if name is None or name not in self.missing:
                continue
            for line, cells in self.rows:
                if cells[0] == name and MISSING in cells[1]:
                    parts = line.split("|")
                    parts[2] = f" {v} "
                    text = text.replace(line, "|".join(parts), 1)
                    filled.append(name)
        run = (f"\n### Automated run {self.date} (run.py, {time.time() - self.t0:.0f} s): "
               f"{'pass' if ok else '**FAIL**'}\n\n{result.strip()}\n")
        if self.notes:
            run += "\n" + "\n".join(f"- {n}" for n in self.notes) + "\n"
        open(self.readme, "w").write(text.rstrip("\n") + "\n")
        self.history(run, [f"- {o} ({self.date}, automated run)" for o in self.obstacles])
        left = [n for n in self.missing if n not in filled]
        log(f"{self.name}: {'PASS' if ok else 'FAIL'}; filled {len(filled)}: {', '.join(filled) or '-'}; "
            f"still MISSING {len(left)}: {', '.join(left) or '-'}; run in HISTORY.md - README Status/Result by hand")
        return ok
