#!/usr/bin/env python3
"""test048 automated: every metric in every check-in, the steps without a hand (1, 5, 6 reboot, 8, 9 without the AP
off, 10, 11, 13 without a battery change, 14, 15). The K230 session is the setup relay (network erased, USB-C 1 on the
bench: the bench answers as the phone over the cable, tools/test-runner/cable.py), which also gives test046 step 7 and
test047 steps 1, 2, 3, 7 their console lines. Steps 2-4 (room meter, finger, wet probe), 6's EN tap / watchdog /
battery change, 7 and 12 need a hand or a test firmware: not here. Never prints the network or the PIN.
    tests/test048-checkin-metrics/run.py
"""
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import BL_PORT, K230_PORT, log, openp, wifi_credentials   # noqa: E402
from phone import SERVER, board_id, creds, erase, firmware, to_firmware, want   # noqa: E402
import cable   # noqa: E402

PI = "pi@192.168.1.159"
HISTORY = "leakcam-server/metrics.jsonl"
BASE = ("t rh probe tdie vdd up boots rst crash free minfree blocks rssi ch scan join dhcp fails sw usbw ks kfail kon "
        "klast kms kago").split()
K230 = "kver kup ktdie kheap kheapmin kpages ksago".split()
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def samples(dev, since):
    """the server history's check-ins of dev after since (UTC ISO), oldest first"""
    out = subprocess.run(["ssh", PI, f"tail -n 400 {HISTORY}"], capture_output=True, text=True, timeout=30).stdout
    got = []
    for ln in out.splitlines():
        try:
            s = json.loads(ln)
        except ValueError:
            continue
        if s.get("device") == dev and s.get("at", "") > since:
            got.append(s)
    return got


def utc():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def next_checkin(dev, since, image=None, secs=150, now=True):
    """the first check-in after since (of that image), None if not in secs. now: the full firmware asked for it
    (`presence now`, every 10 s while none came) instead of waiting out its 60 s interval (2026-11-07: test048 spent
    most of its time waiting for check-ins); False where the test is about the board's own timing"""
    t0, asked = time.time(), 0.0
    while time.time() - t0 < secs:
        for s in samples(dev, since):
            if image is None or s.get("image") == image:
                return s
        if now and image != "setup" and time.time() - asked > 10:
            bl("presence now", 1)
            asked = time.time()
        time.sleep(1)
    return None


class Console(threading.Thread):
    """everything a port prints while the thread runs (no commands): the BL616's k230: lines, the K230's log"""
    def __init__(self, port):
        super().__init__(daemon=True)
        self.port, self.text, self.stop = port, "", False

    def run(self):
        import select
        fd = None
        while not self.stop:
            if fd is None:
                try:
                    fd = openp(self.port)
                except OSError:
                    time.sleep(0.5)
                    continue
            if select.select([fd], [], [], 0.2)[0]:
                try:
                    self.text += os.read(fd, 65536).decode(errors="replace").replace("\r", "")
                except OSError:
                    os.close(fd)
                    fd = None
        if fd is not None:
            os.close(fd)


r = Run(__file__)
dev = board_id()
if firmware() != "full" or creds()[0] is None:
    r.finish(f"not run: needs the full firmware with a network stored ({firmware()})", False)
    sys.exit(1)
t_run = time.time()

# 1. the form: the console's metrics and the server's
out = bl("metrics", 3, r"metrics: [^\n]*\n")
m = re.search(r"metrics: (\S*)", out)
form = m.group(1) if m else ""
keys = dict(kv.split("=", 1) for kv in form.strip("&").split("&") if "=" in kv)
check("metrics on the console: every BL616 key", all(k in keys for k in BASE),
      "missing " + " ".join(k for k in BASE if k not in keys) if not all(k in keys for k in BASE) else f"{len(keys)} keys")
check("form size within the 512 B buffer (with ssid and version)", len(form) + 80 <= 512, f"{len(form)} B + ~80")
t0 = time.time()
bl("metrics", 3, r"metrics: [^\n]*\n")
r.set("Sensor read", f"<= {(time.time() - t0) * 1000:.0f} ms (the whole `metrics` command over the console, sampling "
      "and printing)")
r.set("Form size", f"{len(form)} B metrics + ssid/version ({len(form) + 80} B with ~80 B prefix) of 512")
s1 = next_checkin(dev, utc())
check("a check-in reaches the server with the metrics", s1 is not None)
if s1 is None:
    r.finish("no check-in within 150 s", False)
    sys.exit(1)
mt = s1["metrics"]
check("the server keeps every BL616 key, integers as numbers",
      all(k in mt for k in BASE) and all(isinstance(mt[k], int) for k in BASE if k != "rst"),
      " ".join(k for k in BASE if k not in mt))
d = json.loads(urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5).read())
check("GET /v1/devices/{id} gives the last check-in's metrics", d.get("metrics", {}).get("up") is not None)

# 5. (and the sensors' ranges) the BL616 die and supply, the AHT20, the dry probe
check("tdie 25-60 C", 250 <= mt["tdie"] <= 600, f"{mt['tdie'] / 10} C")
check("vdd 3200-3400 mV", 3200 <= mt["vdd"] <= 3400, f"{mt['vdd']} mV")
check("AHT20 read (rh != -1), plausible", mt["rh"] >= 0 and 0 < mt["t"] < 600, f"t {mt['t'] / 10} C, rh {mt['rh'] / 10} %")
check("probe dry 1600-1700 mV", 1600 <= mt["probe"] <= 1700, f"{mt['probe']} mV")

# 9. Wi-Fi of this check-in
check("rssi -90..-20, ch 1-13, scan+join+dhcp > 0",
      -90 <= mt["rssi"] <= -20 and 1 <= mt["ch"] <= 13 and mt["scan"] + mt["join"] + mt["dhcp"] > 0,
      f"rssi {mt['rssi']}, ch {mt['ch']}, scan {mt['scan']} join {mt['join']} dhcp {mt['dhcp']} ms, fails {mt['fails']}")

# 6. uptime, boots, reset cause
s2 = next_checkin(dev, s1["at"])
check("up grows between check-ins", s2 and s2["metrics"]["up"] > mt["up"],
      f"{mt['up']} -> {s2['metrics']['up'] if s2 else '?'} s")
boots = (s2 or s1)["metrics"]["boots"]
t = utc()
bl("reboot", 1)
s3 = next_checkin(dev, t, "full")
check("reboot: boots +1, up small, rst set", s3 and s3["metrics"]["boots"] == boots + 1 and s3["metrics"]["up"] < 30
      and s3["metrics"]["rst"] != "00",
      f"boots {boots} -> {s3['metrics']['boots']}, up {s3['metrics']['up']} s, rst {s3['metrics']['rst']}" if s3 else "none")

# 10. counters: the switch to setup and back
t = utc()
to_firmware("setup", "switch_to_setup wake", 60)
back = to_firmware("full", None, 240)     # a phone's wake: the setup firmware goes back after 120 s
check("switch_to_setup wake and back on USB power", back is not None, f"{back:.0f} s" if back else "not back")
# on USB power the K230 runs in both firmwares (DESIGN.md 5.3.11): the full firmware's session ended by the switch and
# the setup firmware's relay ended by the switch back both reach the full firmware's first check-in, with both switches
check("no check-in from the wake's setup firmware", not next_checkin(dev, t, "setup", 5))
sf = next_checkin(dev, t, "full")
fm = sf["metrics"] if sf else {}
check("the full firmware's first check-in: sw 2, ks 2, klast 1 (both sessions ended by the switches)",
      fm.get("sw") == 2 and fm.get("ks") == 2 and fm.get("klast") == 1,
      " ".join(f"{k} {fm.get(k)}" for k in ("sw", "ks", "klast", "kms", "kon", "kfail")))
sn = next_checkin(dev, sf["at"] if sf else utc(), "full")
check("the one after: sw 0, ks 0", sn and sn["metrics"]["sw"] == 0 and sn["metrics"]["ks"] == 0,
      f"sw {sn['metrics']['sw']}, ks {sn['metrics']['ks']}" if sn else "none")

# 11. the K230: a relay session (network erased, the bench on USB-C 1), held 3 min, then the setup over the cable
log("board: network erased: the setup firmware starts the K230 relay")
bl_con, k_con = Console(BL_PORT), Console(K230_PORT)
erase()
bl("reboot", 1)
k_con.start()
up = cable.relay(90)
bl_con.start()
check("the relay's CDC device on USB-C 1", up is not None, f"{up:.1f} s" if up else "not in 90 s")
if up is None:
    r.finish("no relay", False)
    sys.exit(1)
t_st = time.time()                # two STATUS lines give the period (their uptimes): no fixed 3 min wait
while len(re.findall(r"k230: STATUS ", bl_con.text)) < 2 and time.time() - t_st < 135:
    time.sleep(1)
statuses = re.findall(r"k230: STATUS ([^\n]*)", bl_con.text)
log("BL616 STATUS lines:", " | ".join(statuses[-4:]))
ups = [int(u) for u in re.findall(r"k230: STATUS (\d+),", bl_con.text)]
gaps = [b - a for a, b in zip(ups, ups[1:])]
check("STATUS every 60 s in the relay (two lines 60 +- 2 s apart by their uptimes)",
      len(gaps) >= 1 and all(58 <= g <= 62 for g in gaps), f"{len(statuses)} lines, gaps {gaps} s")
check("STATUS carries version, die, heap, pages", any(len(s.split(",")) >= 6 for s in statuses),
      statuses[-1][:90] if statuses else "none")
ver = re.search(r"version (\S+)", k_con.text)
ver = ver.group(1) if ver else None
check("the K230 log names its version", ver is not None, ver or "no `version` line")
t_setup = utc()
st, secs = cable.setup()
check("the setup over the cable: SAVED", st == 4, f"state {st}, {secs}")
time.sleep(6)                     # the K230 off 2-3 s after SAVED, then the switch: logged before the port goes
k_con.stop = bl_con.stop = True   # one reader per port: bl() below opens the console itself
bl_con.join()
k_con.join()
back = to_firmware("full", None, 90)
here = os.path.dirname(os.path.abspath(__file__))
for name, con in (("relay_bl616.log", bl_con), ("relay_k230.log", k_con)):   # test046 step 7, test047 steps 1-3
    with open(os.path.join(here, name), "w") as f:
        f.write(con.text)
check("the board on the full firmware with the network", back is not None and creds() == want(*wifi_credentials()))
off = re.search(r"k230: off \(([^)]*)\)", bl_con.text)
check("the relay ends with the K230 off", off is not None, off.group(1) if off else "no off line")
sc = next_checkin(dev, t_setup, "setup", 5)
check("the credential check-in (setup): ks 0 (the session still on)", sc and sc["metrics"]["ks"] == 0,
      f"ks {sc['metrics']['ks']}" if sc else "none")
sf = next_checkin(dev, t_setup, "full")
fm = sf["metrics"] if sf else {}
check("the full firmware's first check-in: ks 1, klast 1, kms > 0, sw 1",
      fm.get("ks") == 1 and fm.get("klast") == 1 and fm.get("kms", 0) > 0 and fm.get("sw") == 1,
      " ".join(f"{k} {fm.get(k)}" for k in ("ks", "klast", "kms", "kon", "kfail", "sw")))
check("... every K230 key, kver = the K230 log's version", all(k in fm for k in K230) and fm.get("kver") == ver,
      f"kver {fm.get('kver')}, log {ver}")
check("... K230 values plausible", fm.get("kup", -1) >= 0 and 200 <= fm.get("ktdie", 0) <= 900 and fm.get("kheap", 0) > 0
      and fm.get("kheapmin", 0) > 0 and fm.get("kpages", 0) > 0,
      " ".join(f"{k} {fm.get(k)}" for k in ("kup", "ktdie", "kheap", "kheapmin", "kpages", "ksago", "kago")))
# on USB the full firmware's own K230 session sends its first STATUS before the first check-in (DESIGN.md 5.3.11):
# ksago is that one's age in this boot; the relay session ended in the other boot: kago -1
check("... kago -1 (the session ended in another boot), ksago the full firmware's own STATUS (0-30 s)",
      fm.get("kago") == -1 and 0 <= fm.get("ksago", -1) <= 30, f"ksago {fm.get('ksago')}, kago {fm.get('kago')}")

# 13. survival: a reset keeps the K230 fields and boots counts on (on USB the K230 starts again with the full
# firmware: its STATUS may refresh kup before the first check-in, DESIGN.md 5.3.11)
t = utc()
bl("reboot", 1)
sr = next_checkin(dev, t, "full")
rm = sr["metrics"] if sr else {}
check("after a reset: kver kept, boots +1, ks 0",
      rm.get("kver") == fm.get("kver") and rm.get("boots") == fm.get("boots", -9) + 1 and rm.get("ks") == 0,
      f"kver {rm.get('kver')}, boots {fm.get('boots')} -> {rm.get('boots')}, ks {rm.get('ks')}")
# the leak check's result (DESIGN.md 5.3.12): the K230's job checks at its start, its DONE in the check-ins after it
sj = next_checkin(dev, sr["at"] if sr else utc(), "full")
jm = sj["metrics"] if sj else {}
check("the leak check's result in the check-in: job <cam0>,<cam1>, jago >= 0",
      re.fullmatch(r"(same|light|changed|new),(same|light|changed|new)", str(jm.get("job"))) is not None
      and isinstance(jm.get("jago"), int) and jm["jago"] >= 0, f"job {jm.get('job')}, jago {jm.get('jago')}")

# 14. the server
req = urllib.request.Request(f"{SERVER}/v1/poll?id=000000000048", data=b"ssid=t048&zz=1&t=215", method="POST")
urllib.request.urlopen(req, timeout=5).read()
d = json.loads(urllib.request.urlopen(f"{SERVER}/v1/devices/000000000048", timeout=5).read())
check("an unknown key is dropped, a known one kept", "zz" not in d["metrics"] and d["metrics"].get("t") == 215,
      json.dumps(d["metrics"]))
try:
    urllib.request.urlopen(f"{SERVER}/v1/poll?id=000000000048", timeout=5)
    code = 200
except urllib.error.HTTPError as e:
    code = e.code
check("a GET check-in is refused (405)", code == 405, str(code))
hist = samples(dev, time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t_run)))
check("every check-in is a history line", len(hist) >= 8, f"{len(hist)} lines in this run")

# 8. heap over the run (the full firmware's check-ins)
fh = [s["metrics"] for s in hist if s.get("image") == "full"]
frees = [x["free"] for x in fh]
mins = [x["minfree"] for x in fh]
check("BL616 heap steady over the run (free within 4 KB after the first)",
      frees and max(frees[1:] or frees) - min(frees[1:] or frees) <= 4096,
      f"free {min(frees)}..{max(frees)}, minfree {min(mins)}..{max(mins)} over {len(fh)} check-ins" if frees else "")
r.set("memory", f"free {min(frees)}..{max(frees)} B, minfree {min(mins)} B over {len(fh)} check-ins; "
      f"K230 heap {fm.get('kheap')} B, min {fm.get('kheapmin')} B, pages {fm.get('kpages')} B" if frees else None)
r.set("current", "n/a: on USB power, not metered")

failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
