#!/usr/bin/env python3
"""test133: the app's leak alert (DESIGN.md 5.3.13), the app's most important use case: full screen over the lock screen,
the alarm sound repeating until acknowledged, through silent mode and Do Not Disturb; acknowledging ends it. The
trigger is the debug build's TEST_ALERT broadcast (the server's push comes with FCM); the phone on adb, no board, no
hand. Every user-facing step is bounded at 15 s (5.3.4).
    tests/test133-leak-alert/run.py
"""
import os
import re
import sys
import time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "test-runner"))
from harness import Run   # noqa: E402
from board import log   # noqa: E402
from phone import ack_alerts, adb, ready   # noqa: E402

PKG = "com.leakcam.setup"
ID = "7C3E8262B149"
USER_S = 15                     # the most a person waits (DESIGN.md 5.3.4)
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    log("CHECK", "pass" if ok else "FAIL", name, detail)


RING_S = 0.5                    # a test alert rings at most this long (user: "even 0.5 sec"); checked from the logs after
# trigger -> acknowledged: the ring plus adb's broadcast round trip over Wi-Fi: 1.09 s typical, 2.35 s and 2.52 s
# seen (the phone busy, an app just reinstalled; 2026-11-09): 3 s (user: "increase timing a bit")
ACK_S = 3.0
# quiet nights (user 2026-11-07: "i want quiet night"): the alert posts on the debug quiet channel (phone.ready sets
# it) and the sound check is skipped; `run.py --loud` checks the alarm sound too
LOUD = "--loud" in sys.argv


def stamp(s):
    """a phone log time "MM-DD HH:MM:SS.mmm" (logcat) or "MM-DD HH:MM:SS:mmm" (dumpsys audio) as seconds in the year"""
    m = re.match(r"(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)[.:](\d{3})", s)
    mo, d, h, mi, se, ms = map(int, m.groups())
    return (((mo * 31 + d) * 24 + h) * 60 + mi) * 60 + se + ms / 1000


def notification():
    """our alert's record in dumpsys notification, "" when there is none"""
    out = adb("shell", "dumpsys", "notification", "--noredact", secs=20)
    recs = [b for b in out.split("NotificationRecord(") if f"pkg={PKG}" in b[:200] and "channel=leak_alarm" in b]
    return recs[0][:3000] if recs else ""


def one_alert(case, locked=True):
    """trigger, acknowledge RING_S later, then the evidence from the logs: the alert posted, its full screen (on a
    locked phone), an alarm-usage player started, the acknowledgement; the seconds trigger -> screen"""
    if locked:
        adb("shell", "input", "keyevent", "KEYCODE_SLEEP")
        t1 = time.time()                    # the screen off: polled, not a fixed 1 s
        while time.time() - t1 < 3 and "mWakefulness=Awake" in adb("shell", "dumpsys", "power", secs=10):
            time.sleep(0.1)
    adb("logcat", "-c")
    t_phone = adb("shell", "date '+%m-%d %H:%M:%S.%3N'").strip()   # one shell argument: the device shell splits
    # -f 32 (FLAG_INCLUDE_STOPPED_PACKAGES): an app force-stopped by an earlier test gets it too (test135 ends so)
    adb("shell", "am", "broadcast", "-f", "32", "-a", f"{PKG}.TEST_ALERT", "-p", PKG, "--es", "id", ID, "--es", "why", "probe wet",
        "--es", "ssid", "VK", "--es", "rssi", "-52")
    time.sleep(RING_S)
    ack_alerts()
    t0 = stamp(t_phone)
    logs = adb("logcat", "-d", "-s", "LeakcamAlert:I", secs=20)
    at = {k: stamp(m.group(1)) - t0 for k, pat in (("shown", r"shown"), ("screen", r"alert screen"), ("ack", r"acknowledged"))
          for m in [re.search(rf"^(\d\d-\d\d \S+) .*LeakcamAlert: [^\n]*{pat}", logs, re.M)] if m}
    check(f"{case}: the alert posted", "shown" in at, f"{at['shown']:.2f} s after the trigger" if "shown" in at else "no")
    if locked:
        check(f"{case}: its full screen opened over the lock screen within {USER_S} s",
              "screen" in at and at["screen"] < USER_S, f"{at['screen']:.2f} s" if "screen" in at else "not opened")
    if LOUD:
        plays = [stamp(m.group(1)) - t0 for m in re.finditer(r"^(\d\d-\d\d \S+) player piid:\d+ new AudioAttributes:"
                                                            r"AudioAttributes: usage=USAGE_ALARM", adb("shell", "dumpsys", "audio", secs=20), re.M)]
        check(f"{case}: the alarm-usage sound started", any(p >= -0.5 for p in plays),
              f"{max(plays):.2f} s" if plays else "no USAGE_ALARM player")
    check(f"{case}: acknowledged within {ACK_S:.2f} s, nothing left", "ack" in at and at["ack"] - at.get("shown", 0)
          <= ACK_S and not notification(), f"rang {at['ack'] - at.get('shown', 0):.2f} s" if "ack" in at else "no ack")
    return at.get("screen")


r = Run(__file__)
why = ready(r.notes)
adb("shell", "settings", "put", "global", "leakcam_quiet", "0" if LOUD else "1")
if why:
    r.obstacle(why)
    r.finish(f"not run: {why.split(' (')[0]}.", False, switch_off=False)
    sys.exit(1)
s1 = s2 = s3 = None
try:
    s1 = one_alert("locked")
    # the channel exists after the first alert: high importance, bypassing DND (the person's DND access granted)
    ch = adb("shell", "dumpsys", "notification", secs=20)
    leak = re.search(r"NotificationChannel\{[^}]*mId='leak_alarm_dnd'[^}]*\}", ch)   # with DND access the app's channel is leak_alarm_dnd
    check("the channel: importance high, bypasses Do Not Disturb, alarm sound",
          leak is not None and "mImportance=4" in leak.group(0) and "mBypassDnd=true" in leak.group(0)
          and "USAGE_ALARM" in leak.group(0), leak.group(0)[:160] if leak else "no channel")
    adb("shell", "cmd", "audio", "set-ringer-mode", "SILENT")
    s2 = one_alert("silent mode")
    adb("shell", "cmd", "audio", "set-ringer-mode", "NORMAL")
    # the Do Not Disturb switch people use is priority mode (zen 1), which an alert channel bypasses; total silence
    # (`set_dnd on`, zen 2) blocks alarms too, for every app (Android, not ours)
    adb("shell", "cmd", "notification", "set_dnd", "priority")
    s3 = one_alert("Do Not Disturb")
finally:
    ack_alerts()
    adb("shell", "cmd", "notification", "set_dnd", "off")
    adb("shell", "cmd", "audio", "set-ringer-mode", "NORMAL")
times = [s for s in (s1, s2, s3) if s is not None]
r.set("Test run time", f"{time.time() - r.t0:.0f} s automated")
if times:
    r.set("Speed / rate against the top", f"trigger -> full screen {min(times):.1f}-{max(times):.1f} s (the most a "
          f"person waits: {USER_S} s)")
failed = [c for c in checks if not c[1]]
summary = f"{len(checks) - len(failed)}/{len(checks)} checks pass."
if failed:
    summary += " Failed: " + "; ".join(f"{n} ({d})" if d else n for n, _, d in failed) + "."
r.finish(summary, not failed, switch_off=False)
sys.exit(1 if failed else 0)
