#!/usr/bin/env python3
"""test135: the app's screens and today's UX rules (DESIGN.md 5.3.4, 5.3.13, 5.3.14; user 2026-11-07): LEAKCAMs /
LEAKCAM setup / Change Wi-Fi, the three-lines menu, the status block, the next check-in in every status, the pending
list, Setup for new devices only, the leak alert, the leak panel, the photo and video links. The phone on adb, the
server on the Pi; the app as a throwaway phone with a fake board (the person's list untouched), alerts quiet and
acknowledged at once. Every user-facing step is bounded at 15 s (5.3.4).
    tests/test135-app-screens/run.py
"""
import html
import json
import os
import re
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run   # noqa: E402
from board import log   # noqa: E402
from phone import SERVER, ack_alerts, adb, as_board, board_id, firmware, ready, screen_xml   # noqa: E402

PKG = "com.leakcam.setup"
USER_S = 15                     # the most a person waits (DESIGN.md 5.3.4)
PHONE = secrets.token_hex(8)    # the throwaway phone: no boards of its own
FAKE = "02" + secrets.token_hex(5).upper()   # a board that only checks in from here
REAL = board_id() or "7C3E8262B149"          # the bench board, for the Setup-skips-known case
checks, steps = [], {}


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


# --- the server --------------------------------------------------------------------------------------------------
def http(method, path, body=None, ctype="application/x-www-form-urlencoded"):
    req = urllib.request.Request(SERVER + path, data=body, method=method, headers={"Content-Type": ctype} if body else {})
    try:
        with urllib.request.urlopen(req, timeout=10) as f:
            data = f.read()
            return f.status, json.loads(data) if data[:1] in (b"{", b"[") else data
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def checkin(dev, **form):
    """a check-in as the board makes it (POST /v1/poll); the answer"""
    form.setdefault("ssid", "TESTNET")
    st, body = as_board(dev, f"/v1/poll?id={dev}", urllib.parse.urlencode(form).encode())   # mTLS, as the board
    return json.loads(body) if body[:1] in (b"{", b"[") else body


def register(dev):
    http("GET", f"/v1/devices/{dev}/pin?phone={PHONE}")


def unregister(dev):
    http("DELETE", f"/v1/devices/{dev}/phones/{PHONE}")


def device(dev):
    return http("GET", f"/v1/devices/{dev}")[1]


# --- the phone ---------------------------------------------------------------------------------------------------
def dump():
    """the screen's views: text, desc, res (id), hint, enabled, checked, bounds (x1, y1, x2, y2)"""
    xml = screen_xml()                  # 300 ms idle wait, not uiautomator dump's 10 s (the spinner never idles)
    nodes = []
    for m in re.finditer(r"<node ([^>]*?)/?>", xml):
        # a text holding double quotes comes in single quotes; entities (&#10; &quot;) decoded
        a = {k: html.unescape(v1 or v2) for k, v1, v2 in re.findall(r"""([\w-]+)=(?:"([^"]*)"|'([^']*)')""", m.group(1))}
        b = list(map(int, re.findall(r"\d+", a.get("bounds", "[0,0][0,0]"))))
        nodes.append({"text": a.get("text", ""), "desc": a.get("content-desc", ""), "hint": a.get("hint", ""),
                      "res": a.get("resource-id", "").split("/")[-1], "enabled": a.get("enabled") == "true",
                      "checked": a.get("checked") == "true", "bounds": b})
    return nodes


def dump_until(pred, secs=5):
    """the screen read again until pred(nodes) holds (a view drawn, a text shown), at most secs: in place of a fixed
    sleep before each dump (user 2026-11-09: "no need to wait"); the last nodes either way"""
    t0 = time.time()
    while True:
        n = dump()
        if pred(n) or time.time() - t0 > secs:
            return n
        time.sleep(0.1)


def find(nodes, **kw):
    for n in nodes:
        if all((n[k].startswith(v[:-1]) if v.endswith("*") else n[k] == v) for k, v in kw.items()):
            return n
    return None


def tap(n):
    x1, y1, x2, y2 = n["bounds"]
    adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))


def tap_on(secs=5, **kw):
    """the view found (waiting up to secs) and tapped; it, or None"""
    t0 = time.time()
    while time.time() - t0 < secs:
        n = find(dump(), **kw)
        if n:
            tap(n)
            return n
        time.sleep(0.3)
    return None


def logs():
    return adb("logcat", "-d", "-s", "LeakcamSetup:I", "LeakcamAlert:I", "LeakcamPush:I", "LeakcamGallery:I", secs=30)


def wait_log(pattern, secs=USER_S):
    """seconds until the app logged pattern (since the last logcat -c), None if not within secs"""
    t0 = time.time()
    while time.time() - t0 < secs:
        if re.search(pattern, logs()):
            return time.time() - t0
        time.sleep(0.3)
    return None


def step(name, secs):
    steps[name] = secs
    return secs


def start():
    adb("shell", "am", "force-stop", PKG)
    adb("logcat", "-c")
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity")


def status_text(nodes):
    return (find(nodes, res="status_text") or {}).get("text", "")


def menu_items():
    """the three-lines menu's items (opened, read, closed)"""
    tap_on(desc="Menu")
    menu = lambda n: [x["text"] for x in n if x["text"] in ("LEAKCAMs", "Setup") and x["res"] != "screen_title"]
    items = menu(dump_until(lambda n: len(menu(n)) == 2, 3))
    adb("shell", "input", "keyevent", "KEYCODE_BACK")
    dump_until(lambda n: not menu(n), 3)
    return items


r = Run(__file__)
why = ready(r.notes)
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
adb("shell", "settings", "put", "global", "leakcam_phone_id", PHONE)
log(f"phone {PHONE}, fake board {FAKE}, real board {REAL}")
try:
    # 1. no boards -> Setup at once
    start()
    t = wait_log(r"screen setup")
    check("1: no boards -> the setup screen at once", t is not None and step("empty -> setup", t) < USER_S,
          f"{t:.1f} s" if t else "not shown")
    n = dump_until(lambda n: find(n, res="screen_title", text="LEAKCAM setup"))
    check("1: title LEAKCAM setup", find(n, res="screen_title", text="LEAKCAM setup"))
    check("1: the status block's spinner line says it looks", find(n, res="busy_text", text="Looking for LEAKCAMs nearby…"),
          (find(n, res="busy_text") or {}).get("text", "no spinner line"))
    ssid = find(n, res="ssid") or {}
    check("1: the Wi-Fi fields blank", ssid.get("text") in ("", "Wi-Fi name (SSID)"), ssid.get("text"))
    lg = logs()
    check("1: Bluetooth scan and cable watch on", "scan on" in lg and "usb watch on" in lg)
    # the real board advertising makes step 3 conclusive; only the setup firmware advertises (the full one has no BLE):
    # no 12 s wait for an advert that cannot come (2026-11-08)
    real_seen = firmware() == "setup" and wait_log(rf"found {REAL}", 12) is not None
    check("1: the menu holds only Setup", menu_items() == ["Setup"])

    # 2. a board -> LEAKCAMs
    register(FAKE)
    checkin(FAKE, chg="1", bat="4100")
    start()
    t = wait_log(r"screen status")
    check("2: with a board -> LEAKCAMs", t is not None, f"{t:.1f} s" if t else "not shown")
    t = wait_log(rf"row {FAKE}")
    step("status list", t or USER_S)
    n = dump()
    check("2: title LEAKCAMs", find(n, res="screen_title", text="LEAKCAMs"))
    check("2: the row shows serial · SSID", find(n, text=f"{FAKE} · TESTNET"))
    check("2: the status text says the next check-in", "next check-in" in status_text(n), status_text(n))
    row_photo = (find(n, res="take_photo") or {}).get("bounds")
    adb("logcat", "-c")
    wait_log(r"mine [0-9A-F]{12}")   # the next presence refresh (3 s on the bench): a scan would have started by now
    lg = logs()
    check("2: no Bluetooth scan, no cable watch on LEAKCAMs", "scan on" not in lg and "usb watch on" not in lg)
    check("2: the menu holds LEAKCAMs and Setup", menu_items() == ["LEAKCAMs", "Setup"])

    # 3. Setup skips a known board
    register(REAL)
    start()
    wait_log(r"screen status")
    tap_on(desc="Menu")
    tap_on(text="Setup")
    t = wait_log(r"screen setup")
    adb("logcat", "-c")
    seen_known = wait_log(rf"found {REAL}", 12) if real_seen else None
    check("3: Setup skips a board set up from this phone" + ("" if real_seen else " (not conclusive: the board did not "
          "advertise in step 1)"), seen_known is None and not find(dump(), text=REAL))
    unregister(REAL)

    # 4. Change Wi-Fi: its own screen, through the server
    tap_on(desc="Menu")
    tap_on(text="LEAKCAMs")
    wait_log(r"screen status")
    adb("logcat", "-c")
    tap_on(desc="more actions*")
    tap_on(text="Change Wi-Fi")
    t = wait_log(rf"screen change {FAKE}")
    check("4: Change Wi-Fi opens its own screen", t is not None)
    n = dump_until(lambda n: find(n, res="screen_title", text="Change Wi-Fi") and find(n, res="ssid", text="TESTNET"))
    check("4: title Change Wi-Fi", find(n, res="screen_title", text="Change Wi-Fi"))
    check("4: the SSID filled in", (find(n, res="ssid") or {}).get("text") == "TESTNET", (find(n, res="ssid") or {}).get("text"))
    pw = find(n, res="password") or {}
    check("4: the password as dots", "••••••••" in (pw.get("hint", "") + pw.get("text", "")), pw.get("hint") or pw.get("text"))
    check("4: no search on Change Wi-Fi", "scan on" not in logs() and not find(n, res="busy_text", text="Looking*"))
    tap_on(res="send")
    st = status_text(dump_until(lambda n: "password hidden" in status_text(n)))
    check("4: the same network without its password refused", "password hidden" in st, st)
    tap_on(res="ssid")
    adb("shell", "input", "keyevent", "KEYCODE_MOVE_END")
    for _ in range(8):
        adb("shell", "input", "keyevent", "KEYCODE_DEL")
    adb("shell", "input", "text", "TESTNET2")
    tap_on(res="password")
    adb("shell", "input", "text", "testpass123")        # a made-up passphrase: nothing real on the command line
    t0 = time.time()
    tap_on(res="send")
    t = wait_log(r"screen status")
    check("4: sent through the server, back on LEAKCAMs", t is not None and step("change Wi-Fi", time.time() - t0) < USER_S,
          f"{time.time() - t0:.1f} s")
    st = status_text(dump_until(lambda n: "next check-in" in status_text(n)))
    check("4: the note names the next check-in", re.search(r'takes "TESTNET2" at its next check-in \((about|any moment)', st), st)

    # 5. photo and video: queued once each, the server's order, delivered one per check-in
    t0 = time.time()
    tap_on(res="take_photo")
    wait_log(rf"capture photo {FAKE}")
    tap_on(res="take_video")
    wait_log(rf"capture video {FAKE}")
    step("take photo + video", time.time() - t0)
    n = dump_until(lambda n: "3. Take a 15 s video" in (find(n, res="pending_list") or {}).get("text", ""))
    plist = (find(n, res="pending_list") or {}).get("text", "")
    check("5: the pending list in order: Wi-Fi, photo, video",
          re.search(r'1\. Change Wi-Fi to "TESTNET2".*\n2\. Take a photo.*\n3\. Take a 15 s video', plist), plist)
    check("5: photo and video buttons off while they wait", not (find(n, res="take_photo") or {}).get("enabled", True)
          and not (find(n, res="take_video") or {}).get("enabled", True))
    check("5: the action row did not move with the longer status", (find(n, res="take_photo") or {}).get("bounds") == row_photo,
          f"{row_photo} -> {(find(n, res='take_photo') or {}).get('bounds')}")
    pend = [p["kind"] for p in device(FAKE).get("pending", [])]
    check("5: the server queued one of each", pend == ["wifi", "photo", "video"], pend)
    a1, a2, a3 = checkin(FAKE), checkin(FAKE, wchg="4"), checkin(FAKE)
    check("5: one per check-in: Wi-Fi, then the photo, then the video",
          "wifi" in a1 and "capture" not in a1 and a2.get("capture", {}).get("kind") == "photo"
          and a3.get("capture", {}).get("kind") == "video", f"{list(a1)} / {a2.get('capture')} / {a3.get('capture')}")

    # 6. the leak alert
    adb("logcat", "-c")
    leak = checkin(FAKE, probe="800")
    ids = leak.get("capture", {}).get("ids", [])
    t = wait_log(rf"alert {FAKE} shown")
    check("6: a wet probe -> the alert", t is not None and step("leak -> alert", t) < USER_S, f"{t:.1f} s" if t else "none")
    nt = adb("shell", "dumpsys", "notification", "--noredact", secs=30)
    rec = next((b for b in nt.split("NotificationRecord(") if f"pkg={PKG}" in b[:200] and "leak_alarm" in b[:400]), "")
    check("6: the alert names the probe, no signal", "probe" in rec and "signal" not in rec.lower(),
          re.search(r"android\.text=\S+ \(([^)]*)", rec).group(1)[:80] if re.search(r"android\.text=\S+ \(([^)]*)", rec) else "no text")
    if ids:
        http("POST", f"/v1/media/{ids[0]}", open(os.path.join(os.path.dirname(__file__), "sample.jpg"), "rb").read(), "image/jpeg")
    t = wait_log(rf"alert {FAKE} shown with its picture")
    check("6: the first picture embedded in the alert", t is not None and step("picture -> alert", t) < USER_S,
          f"{t:.1f} s" if t else "none")
    ack_alerts()

    # 7. the leak panel: Not a leak
    adb("shell", "am", "start", "-n", f"{PKG}/.MainActivity", "--es", "id", FAKE, "--es", "media", ",".join(ids),
        "--es", "title", "Leak", "-f", "0x20000000")
    t0 = time.time()
    tap_on(text="NOT A LEAK", secs=8)
    t = wait_log(rf"not a leak {FAKE}: cleared")
    check("7: Not a leak clears it at the server", t is not None and device(FAKE).get("leak") is None,
          f"{time.time() - t0:.1f} s")
    step("not a leak", time.time() - t0)
    st = status_text(dump_until(lambda n: "watches again" in status_text(n)))
    check("7: the note says it watches again from its next check-in", re.search(r"watches again from its next check-in \(", st), st)
    checkin(FAKE, probe="3000")                           # dried: re-armed
    adb("logcat", "-c")
    checkin(FAKE, probe="3000", job="changed", jago="10")
    t = wait_log(rf"alert {FAKE} shown")
    nt = adb("shell", "dumpsys", "notification", "--noredact", secs=30)
    ack_alerts()
    rec = next((b for b in nt.split("NotificationRecord(") if f"pkg={PKG}" in b[:200] and "leak_alarm" in b[:400]), "")
    check("6: the camera inference -> Possible leak, inference", t is not None and "Possible leak" in rec and "inference" in rec)
    http("POST", f"/v1/devices/{FAKE}/clear")

    # 8. the links
    for mid in ids[1:]:
        ext = "jpg" if "-p" in mid else "mp4"
        http("POST", f"/v1/media/{mid}", open(os.path.join(os.path.dirname(__file__), f"sample.{ext}"), "rb").read(),
             "image/jpeg" if ext == "jpg" else "video/mp4")
    start()
    wait_log(r"screen status")
    n = dump_until(lambda n: sum(bool(re.match(r"(Photo|Video), ", x["text"])) for x in n) >= len(ids))
    links = [x for x in n if re.match(r"(Photo|Video), ", x["text"])]
    check("8: the photos and videos listed as links", find(n, text="Photos and videos") and len(links) >= len(ids), len(links))
    if links:
        t0 = time.time()
        tap(next(x for x in links if x["text"].startswith("Photo")))
        opened = None
        while time.time() - t0 < USER_S and opened is None:
            top = re.search(r"topResumedActivity=\S+ \S+ (\S+)", adb("shell", "dumpsys", "activity", "activities", secs=30))
            if top and not top.group(1).startswith(PKG):
                opened = top.group(1)
            time.sleep(0.2)
        check("8: a link opens the file alone in the phone's viewer", opened, opened or "the app stayed on top")
        step("open a link", time.time() - t0)
        adb("shell", "input", "keyevent", "KEYCODE_BACK")

    # 9. the last board deleted -> Setup
    start()
    wait_log(r"screen status")
    adb("logcat", "-c")
    tap_on(desc="more actions*")
    tap_on(text="Delete")
    tap_on(res="button1")
    t = wait_log(r"screen setup")
    check("9: the last board deleted -> the setup screen", t is not None, f"{t:.1f} s" if t else "not shown")
finally:
    ack_alerts()
    adb("shell", "settings", "delete", "global", "leakcam_phone_id")
    unregister(REAL)
    unregister(FAKE)
    adb("shell", "am", "force-stop", PKG)
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
if steps:
    worst = max(steps, key=steps.get)
    r.set("Speed / rate against the top", f"slowest user-facing step: {worst} {steps[worst]:.1f} s (the most a person "
          f"waits: {USER_S} s); " + ", ".join(f"{k} {v:.1f} s" for k, v in steps.items()))
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
