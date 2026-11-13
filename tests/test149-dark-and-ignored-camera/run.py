#!/usr/bin/env python3
"""test149: a camera that sees nothing reads "dark" (an unreliable capture, not a leak), and the person's two answers
on the status screen: Tune focus, or Ignore camera, which the K230 keeps in its flash and skips (DESIGN.md 5.3.18;
user 2026-11-13). On USB power at the bench, no hand: the dark camera simulated on the K230 console
(/sdcard/leakcam/sim `cam<N>=dark`), checks asked on the BL616 console (`leak check`), the app driven on the phone.
    tests/test149-dark-and-ignored-camera/run.py
Steps: 1 both cameras dark: `dark,dark`, the histories untouched, no leak at the server; 2 the status screen names it
and offers Tune focus and Ignore camera; 3 Ignore camera tapped and confirmed: the server has it, the board takes it at
its next check-in, the K230 keeps it in /sdcard/leakcam/ignore; 4 the ignored camera with an object in view (it would
read `changed`): `ignored`, no leak, and the check is faster; 5 "Use camera again" from the ⋮ menu: the K230's file 0,
the camera `new` (its old history gone); 6 the board's memory, no TCP connection left.
"""
import atexit
import html
import json
import os
import re
import sys
import time
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run, bl   # noqa: E402
from board import K230_PORT, log, openp   # noqa: E402
from phone import SERVER, adb, awake, board_id, clear_leak, firmware, phone_clean, register, screen_xml   # noqa: E402

DIR = "/sdcard/leakcam"
APP = "com.leakcam.setup"
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


def clean(s):
    return re.sub(r"\x1b\[[0-9;]*m", "", s)


def idle(secs=90):
    """no capture of the board's own running (a leak's pictures and video, the ignore hand-over)"""
    t = time.time()
    while "one running" in clean(bl("capture", 2)) and time.time() - t < secs:
        time.sleep(2)


def leak_check(secs=30):
    """`leak check` on the BL616: (the two cameras' verdicts, ms CHECK -> DONE) or (None, what came)"""
    idle()
    out = clean(bl("leak check", secs, r"leak: DONE [^\n]*\n"))
    m = re.search(r"leak: DONE (\S+) (\d+) ms after the ask", out)
    return (",".join(m.group(1).split(",")[:2]), int(m.group(2))) if m else (None, out.strip().splitlines()[-1:])


def device():
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{dev}", timeout=5) as f:
        return json.loads(f.read())


def at_server(job, secs=30):
    """the server has this job (its two verdicts) from a check-in; seconds, or None"""
    t = time.time()
    while time.time() - t < secs:
        if ",".join(device().get("metrics", {}).get("job", "").split(",")[:2]) == job:
            return time.time() - t
        bl("presence now", 3)
        time.sleep(1)
    return None


def put_ignore(cam, on):
    req = urllib.request.Request(f"{SERVER}/v1/devices/{dev}/cameras/{cam}", json.dumps({"ignore": on}).encode(),
                                 {"Content-Type": "application/json"}, method="PUT")
    urllib.request.urlopen(req, timeout=5).read()


def scene(*tokens):
    for t in tokens:
        r.k(f"echo {t} {DIR}/sim", 2)


def k_ignore():
    """the K230's stored mask of ignored cameras; 0 with no file (never ignored: written only when it changes)"""
    m = re.search(r"^(\d+)\s*$", clean(r.k(f"cat {DIR}/ignore", 2)).split("ignore", 1)[-1], re.M)
    return int(m.group(1)) if m else 0


def taken(mask, secs=75):
    """the board took the ignore setting (its next check-in's capture of kind ignore) and the K230 stored it"""
    t = time.time()
    while time.time() - t < secs:
        bl("presence now", 3)
        idle(30)
        if k_ignore() == mask:
            return time.time() - t
        time.sleep(2)
    return None


def nodes():
    out = []
    for m in re.finditer(r"<node ([^>]*?)/?>", screen_xml()):
        a = {k: html.unescape(v1 or v2) for k, v1, v2 in re.findall(r"""([\w-]+)=(?:"([^"]*)"|'([^']*)')""", m.group(1))}
        out.append({"text": a.get("text", ""), "res": a.get("resource-id", "").split("/")[-1],
                    "bounds": list(map(int, re.findall(r"\d+", a.get("bounds", "[0,0][0,0]"))))})
    return out


def find(secs=8, **kw):
    """a view by res or by the start of its text, waited for; None"""
    t = time.time()
    while True:
        for n in nodes():
            if all(n[k].lower().startswith(v.lower()) if k == "text" else n[k] == v for k, v in kw.items()):
                return n
        if time.time() - t > secs:
            return None
        time.sleep(0.2)


def tap(n):
    x1, y1, x2, y2 = n["bounds"]
    adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))


def open_app():
    awake()
    adb("shell", "am", "force-stop", APP)
    adb("shell", "am", "start", "-n", f"{APP}/.MainActivity")


def bl_free():
    m = re.search(r"free (\d+) largest \d+ min (\d+)", bl("mem", 1.5))
    return (int(m.group(1)), int(m.group(2))) if m else None


r = Run(__file__)
dev = board_id()
if firmware() != "full":
    r.finish("not run: the board is not in the full firmware", False, switch_off=False)
    sys.exit(1)
r.fd = openp(K230_PORT)
register(dev)


def restore():
    """on every way out: no simulated scene, no camera ignored (at the server and on the K230), no open leak, the phone
    clean (a run stopped between its steps must not leave a camera silently off watch)"""
    try:
        r.k(f"rm {DIR}/sim", 2)
        if any(device().get("thresholds", {}).get("ignore", [])) or k_ignore():
            for cam in (0, 1):
                put_ignore(cam, False)
            taken(0)
        clear_leak(dev)
        phone_clean(dev)
        adb("shell", "input", "keyevent", "KEYCODE_HOME")
    except Exception as e:                 # noqa: BLE001
        log(f"restore: {e!r}")


atexit.register(restore)
mem0 = bl_free()
idle()
for cam in (0, 1):
    put_ignore(cam, False)
check("start: no camera ignored on the K230", taken(0) is not None, f"mask {k_ignore()}")
clear_leak(dev)

# 1. both cameras dark
r.k(f"rm {DIR}/sim", 2)
scene("lit=led")
leak_check()                              # histories of this lighting, whatever the bench shows
bl("presence now", 5)                     # ... and what it shows may read changed: that leak cleared before the steps
time.sleep(3)
clear_leak(dev)
hist0 = re.findall(r"cam[01]\.hist\s+\d+", r.k(f"ls {DIR}", 2))
scene("cam0=dark", "cam1=dark")
res, ms_dark = leak_check()
check("both cameras dark: DONE dark,dark", res == "dark,dark", f"{res} in {ms_dark} ms")
check("dark: the histories untouched", re.findall(r"cam[01]\.hist\s+\d+", r.k(f"ls {DIR}", 2)) == hist0 and
      len(hist0) == 2, str(hist0))
got = at_server("dark,dark")
check("dark: at the server within 30 s", got is not None, f"{got:.0f} s" if got is not None else "not in 30 s")
check("dark: no leak opened", not device().get("leak"), str(device().get("leak")))

# 2. the status screen: what is wrong, the two answers
open_app()
what = find(res="focus_what")
check("the status screen names it: Bottom camera sees nothing", what and what["text"].startswith("Bottom camera sees nothing"),
      what["text"] if what else "no flag row")
check("... and offers Tune focus and Ignore camera", find(res="tune_focus", secs=2) and find(res="ignore_cam", secs=2))

# 3. Ignore camera: asked first, then at the server, then on the board
t0 = time.time()
n = find(res="ignore_cam", secs=2)
if n:
    tap(n)
ask = find(text="Ignore the bottom camera?")
check("Ignore camera asks first", ask is not None, "")
yes = find(text="Ignore it", secs=3)
if yes:
    tap(yes)
t = time.time()
while time.time() - t < 10 and not device().get("thresholds", {}).get("ignore", [False])[0]:
    time.sleep(0.3)
check("confirmed: the server has camera 1 ignored within 10 s of the tap",
      device().get("thresholds", {}).get("ignore") == [True, False], f"{time.time() - t0:.1f} s after the first tap")
what = find(res="focus_what", secs=6)
check("the flag moves on to the next camera that needs the person (the top one, dark too)",
      what and what["text"].startswith("Top camera"), what["text"] if what else "no flag row")
got = taken(1)
check("the board takes it at its next check-in: the K230's file holds mask 1", got is not None,
      f"{got:.0f} s" if got is not None else f"mask {k_ignore()}")

# 4. the ignored camera is not judged: an object in its view would read changed
put_ignore(1, True)
check("camera 2 ignored too (the API): the K230's file holds mask 3", taken(3) is not None, f"mask {k_ignore()}")
scene("cam0=object", "cam1=object")
res, ms_ign = leak_check()
check("both ignored, objects in view: DONE ignored,ignored", res == "ignored,ignored", f"{res} in {ms_ign} ms")
check("ignored: the check is not slower than the dark one", isinstance(ms_ign, int) and isinstance(ms_dark, int) and
      ms_ign <= ms_dark + 300, f"{ms_ign} ms against {ms_dark} ms")
got = at_server("ignored,ignored")
check("ignored: at the server, no leak, nothing more waiting for the board",
      got is not None and not device().get("leak") and not [p for p in device().get("pending", []) if p["kind"] == "ignore"],
      f"leak {device().get('leak')}, pending {device().get('pending')}")

# 5. used again, from the board's menu
open_app()
more = find(res="more")
if more:
    tap(more)
again = find(text="Use the bottom camera again", secs=4)
check("the ⋮ menu offers Use the bottom camera again", again is not None, "")
if again:
    tap(again)
t = time.time()
while time.time() - t < 10 and device().get("thresholds", {}).get("ignore", [True])[0]:
    time.sleep(0.3)
check("tapped: the server has camera 1 in use again", device().get("thresholds", {}).get("ignore") == [False, True],
      str(device().get("thresholds", {}).get("ignore")))
put_ignore(1, False)
check("both in use again: the K230's file holds mask 0", taken(0) is not None, f"mask {k_ignore()}")
scene("cam0=same", "cam1=same")
res, ms = leak_check()
# new: the old history gone; a camera the bench really leaves in the dark reads dark (no history written either)
check("used again: both judged again, from a new history", res is not None and
      all(v in ("new", "dark") for v in res.split(",")) and "new" in res, f"{res} in {ms} ms")

# 6. the board after it
mem1 = bl_free()
lwip = " | ".join(re.findall(r"^lwip: [^\r\n]*", clean(bl("mem", 2)), re.M))
check("the BL616 holds no TCP connection after", "lwip: tcp" not in lwip, lwip)
check("the BL616's free heap does not shrink", mem0 and mem1 and mem1[0] >= mem0[0] - 1024, f"{mem0} -> {mem1}")
r.set("Speed / rate against the top", f"check: dark,dark {ms_dark} ms, ignored,ignored {ms_ign} ms")
r.set("Memory BL616", f"BL616 free {mem0} -> {mem1} (free, min); {lwip}")
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
