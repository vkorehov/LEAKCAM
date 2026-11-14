#!/usr/bin/env python3
"""test146 step 3a, without the board (user 2026-11-13: "you can mock saved h265 video streams", "send thru server"):
LEAKCAM Focus Adjustment on the bench phone end to end against a local server (this host, the phone through adb
reverse over USB: no Wi-Fi), the board played by tools/test-runner/mock_board_focus.py from the board's recorded
frames (mock_cam0.fv, mock_cam1.fv). The app built for it:
    cd android && ./gradlew assembleDebug assembleDebugAndroidTest -PleakcamServer=http://127.0.0.1:8090 \
        -Pandroid.aapt2FromMavenOverride=$HOME/.local/opt/aapt2-x86/aapt2
The screen (androidTest FocusScreen) opens as Tune focus does, the mock board joins 3 s later (its check-in), Camera 2
is tapped at 8 s, Camera 1 at 14 s, the screen left at 20 s. Checks: the live picture on each camera (a
screenshot's video area: detail, and the two cameras differ), the lens picture with the way to turn (green and red
arrows), each tap's command at the board and how fast, Stop when the screen is left, no decoder
error in the phone's log.
    tests/test146-focus-tuning/app_mock.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

from PIL import Image, ImageStat

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
DEV = "7C3E8262B149"
APK = os.path.join(ROOT, "android/app/build/outputs/apk/debug/app-debug.apk")
TEST_APK = os.path.join(ROOT, "android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk")
GO = "/usr/local/go/bin/go"
checks = []


def check(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    print(f"CHECK {'pass' if ok else 'FAIL'} {name} {detail}", flush=True)


def adb(*a, **k):
    return subprocess.run(["adb", *a], capture_output=True, text=True, **k).stdout


def shot(path):
    with open(path, "wb") as f:
        f.write(subprocess.run(["adb", "exec-out", "screencap", "-p"], capture_output=True).stdout)
    im = Image.open(path).convert("L")
    w, h = im.size
    area = im.crop((int(w * 0.05), int(h * 0.17), int(w * 0.95), int(h * 0.46)))   # the video under the buttons
    st = ImageStat.Stat(area)
    return st.mean[0], st.stddev[0], area


work = tempfile.mkdtemp(prefix="focus-mock-")
tls = os.path.join(work, "tls")
os.makedirs(tls)
subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes",
                "-keyout", f"{tls}/server.key", "-out", f"{tls}/server.crt", "-days", "2", "-subj", "/CN=localtest"],
               capture_output=True, check=True)
shutil.copy(f"{tls}/server.crt", f"{tls}/ca.crt")
subprocess.run([GO, "build", "-o", f"{work}/leakcam-server", "."], cwd=os.path.join(ROOT, "server"), check=True)
env = dict(os.environ, LEAKCAM_ADDR="127.0.0.1:8090", LEAKCAM_DEV_ADDR="127.0.0.1:18443", LEAKCAM_TLS=tls,
           **{f"LEAKCAM_{k}": f"{work}/{v}" for k, v in (("MEDIA", "media"), ("IMAGES", "images"), ("MODELS", "models"),
                                                           ("FCM", "fcm.json"), ("TOKENS", "tokens.json"),
                                                           ("PHONES", "phones.json"), ("THRESHOLDS", "thr.json"),
                                                           ("METRICS", "m.jsonl"), ("CRASHES", "c.jsonl"))})
srv = subprocess.Popen([f"{work}/leakcam-server"], env=env, stdout=open(f"{work}/server.log", "w"),
                       stderr=subprocess.STDOUT)
board = None
try:
    time.sleep(1.5)
    adb("reverse", "tcp:8090", "tcp:8090")
    for a in (APK, TEST_APK):
        out = adb("install", "-r", a)
        check(f"installed {os.path.basename(a)}", "Success" in out, out.strip()[-80:])
    adb("logcat", "-c")
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    t0 = time.time()
    inst = subprocess.Popen(["adb", "shell", "am", "instrument", "-w", "-e", "class", "com.leakcam.setup.FocusScreen",
                             "-e", "id", DEV, "-e", "taps", "8:2,14:1", "-e", "secs", "20",
                             "com.leakcam.setup.test/androidx.test.runner.AndroidJUnitRunner"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(3)                                   # the screen up, the session asked for: waiting for the board
    wait_mean, wait_sd, _ = shot(os.path.join(HERE, "app_mock_waiting.png"))
    board = subprocess.Popen([sys.executable, os.path.join(ROOT, "tools/test-runner/mock_board_focus.py"),
                              "http://127.0.0.1:8090", DEV, os.path.join(HERE, "mock_cam0.fv"),
                              os.path.join(HERE, "mock_cam1.fv"), "40"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, bufsize=1)
    shots, board_lines, n = {}, [], 0
    for line in board.stdout:                       # a screenshot 1.5 s after each camera the board shows
        board_lines.append(line)
        m = re.search(r"showing camera (\d)", line)
        if m:
            n += 1
            time.sleep(1.5)
            name = f"{n}_camera{int(m.group(1)) + 1}"
            shots[name] = shot(os.path.join(HERE, f"app_mock_{name}.png"))
    board.wait(30)
    board_out = "".join(board_lines)
    inst_out = inst.communicate(timeout=60)[0]
    check("the screen ran (FocusScreen)", "OK (1 test)" in inst_out, inst_out.strip()[-120:])
    print(board_out)
    check("before the board: the waiting state (a dark video area)", wait_mean < 20, f"mean {wait_mean:.0f}")
    names = sorted(shots)
    check("3 camera stretches shot (camera 1, 2, 1)", [x.split("_")[1] for x in names] ==
          ["camera1", "camera2", "camera1"], str(names))
    for name in names:
        mean, sd, _ = shots[name]
        check(f"{name}: a live picture in the video area", mean > 20 and sd > 12, f"mean {mean:.0f}, spread {sd:.0f}")
    for name in names:                              # the lens picture under the video: green and red arrows
        im = Image.open(os.path.join(HERE, f"app_mock_{name}.png")).convert("RGB")
        w, h = im.size
        px = list(im.crop((int(w * 0.3), int(h * 0.46), int(w * 0.7), int(h * 0.62))).getdata())
        green = sum(1 for r, g, b in px if g > 120 and r < 90 and b < 110)
        red = sum(1 for r, g, b in px if r > 170 and g < 90 and b < 90)
        check(f"{name}: the lens picture with a green and a red arrow", green > 300 and red > 100,
              f"green {green} px, red {red} px")
    if len(names) >= 3:
        a, b, c = (shots[x] for x in names[:3])
        check("Camera 2 shows another picture than Camera 1, Camera 1 the same again",
              abs(a[0] - b[0]) > 8 and abs(a[0] - c[0]) < 25, f"means {a[0]:.0f}, {b[0]:.0f}, {c[0]:.0f}")
    log = adb("logcat", "-d", "-s", "LeakcamFocus:*", "LeakcamFocusTest:*")
    taps = [int(m) for m in re.findall(r"tapped Camera \d[^\n]*? at (\d+) ms", log)]
    cmds = [(float(t), int(c)) for t, c in re.findall(r"^\s*([\d.]+) cmd cam (\d)", board_out, re.M)]
    shows = [float(ms) for ms in re.findall(r"showing camera \d \((\d+) ms after the command\)", board_out)]
    check("2 taps made", len(taps) == 2, str(taps))
    check("the board got the start camera and both switches (cam 0, 1, 0)", [c for _, c in cmds] == [0, 1, 0],
          str(cmds))
    check("each switch shown by the board within 100 ms of its command", shows and max(shows) <= 100, str(shows))
    check("Stop at the board when the screen was left", "cmd stop" in board_out, "")
    check("no decoder error on the phone", "decoder" not in log and "no H.265 decoder" not in log,
          log.strip()[-200:])
    srv_log = open(f"{work}/server.log").read()
    check("the server relayed the stream and ended it", "focus stream ended" in srv_log,
          srv_log.strip().splitlines()[-1][:120])
finally:
    if board and board.poll() is None:
        board.kill()
    srv.terminate()
    srv.wait(5)
    adb("reverse", "--remove", "tcp:8090")
    adb("shell", "input", "keyevent", "KEYCODE_HOME")
failed = [c for c in checks if not c[1]]
print(f"{len(checks) - len(failed)}/{len(checks)} checks pass" + ("" if not failed else ": FAIL " +
      "; ".join(n for n, _, _ in failed)))
sys.exit(1 if failed else 0)
