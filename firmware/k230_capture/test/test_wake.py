#!/usr/bin/env python3
"""Host test of the wake algorithm: leakcam_wake (host build, cameras from test/wake_stub.c)
against mock_server.py, with this script in the agent's place (answers "wifi" on stdin).

  test/test_wake.py <leakcam_wake host binary> <its STATE_DIR>
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
wake = os.path.abspath(sys.argv[1])
state = sys.argv[2]                                                 # the binary's STATE_DIR
out = tempfile.mkdtemp(prefix="leakcam_mock_")
fails = 0


def check(cond, msg):
    global fails
    if not cond:
        fails += 1
        print("FAIL: " + msg)


srv = subprocess.Popen([sys.executable, os.path.join(HERE, "..", "mock_server.py"), "--port", "0",
                        "--dir", out], stdout=subprocess.PIPE, text=True)
port = re.search(r"port (\d+)", srv.stdout.readline()).group(1)


def run(reason, scene, answer="wifi=ok", probe_mv="1650", rh="45.5", t="21.3"):
    """one wake: (stdout lines, was Wi-Fi asked for, sleep seconds). The sensor values arrive as
    the agent passes them from WAKE; rh None = no AHT20 sample"""
    env = dict(os.environ, LEAKCAM_TEST_SCENE=scene, LEAKCAM_PROBE_MV=probe_mv)
    env.pop("LEAKCAM_RH", None)
    env.pop("LEAKCAM_T", None)
    if rh is not None:
        env.update(LEAKCAM_RH=rh, LEAKCAM_T=t)
    p = subprocess.Popen([wake, reason], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, env=env)
    lines, asked = [], False
    for line in p.stdout:
        lines.append(line.strip())
        if line.strip() == "wifi":
            asked = True
            p.stdin.write(answer + "\n")
            p.stdin.flush()
    p.wait()
    sleep = [int(l[6:]) for l in lines if l.startswith("sleep=")]
    check(p.returncode == 0 and len(sleep) == 1, f"{reason}/{scene}: exit {p.returncode}, {lines}")
    return lines, asked, sleep[0] if sleep else None


def files(pattern):
    return sorted(glob.glob(os.path.join(out, pattern)))


shutil.rmtree(state, ignore_errors=True)
os.makedirs(state)

# no server configured: nothing to report to, so no Wi-Fi either
lines, asked, sleep = run("rtc", "dry")
check(not asked and sleep == 3600, f"without server: asked={asked} sleep={sleep}")

with open(os.path.join(state, "server"), "w") as f:
    f.write(f"127.0.0.1 {port}\n")

# first wake: no history yet -> new -> Wi-Fi -> server says no leak -> stored
lines, asked, sleep = run("rtc", "dry")
check(asked and sleep == 21600 and "cam 0: first frame" in lines, f"first wake: {lines}")
check(len(files("check-*-cam0.pgm")) == 1 and len(files("check-*-cam1.pgm")) == 1, "check upload")
check(os.path.getsize(files("check-*-cam0.pgm")[0]) == 15 + 320 * 240, "PGM size")
check(os.path.exists(os.path.join(state, "cam0.hist.0")) or os.path.exists(os.path.join(state, "cam0.hist.1")),
      "history view not stored")
check(len(glob.glob(os.path.join(state, "hist0", "*.K"))) == 1, "keyframe not stored")

# same scene: sleep at once, no Wi-Fi, nothing sent
lines, asked, sleep = run("rtc", "dry")
check(not asked and sleep == 21600 and "cam 0: same" in lines, f"same scene: {lines}")
check(len(files("check-*")) == 2, "nothing may be sent for an unchanged scene")

# same scene, but the sensors alarm: reported whatever the cameras see
nchecks = len(files("check-*"))
lines, asked, sleep = run("humid", "dry", rh="87.0")
check(asked and sleep == 21600 and "cam 0: same" in lines, f"humidity alarm, same scene: {lines}")
lines, asked, sleep = run("rtc", "dry", probe_mv="700", rh=None)
check(asked and sleep == 600 and "leak reported" in lines, f"probe wet, same scene: {lines}")
lines, asked, sleep = run("leak", "dry")
check(asked and sleep == 600 and "leak reported" in lines, f"probe wake, same scene: {lines}")
check(len(files("check-*")) == nchecks + 2 * 3, "every sensor alarm must reach the server")
for v in files("video-*"):
    os.remove(v)

# puddle, BL616 probe wet: server says leak -> 5 s video from both cameras, nothing stored
lines, asked, sleep = run("leak", "wet")
check(asked and sleep == 600 and "leak reported" in lines, f"leak: {lines}")
check([os.path.getsize(v) for v in files("video-*")] == [800, 800], f"video uploads {files('video-*')}")
check(len(glob.glob(os.path.join(state, "hist0", "*"))) == 1, "a reported leak must not be stored")

# still wet, Wi-Fi refused (no credentials): retry in an hour, nothing sent
lines, asked, sleep = run("rtc", "wet", "wifi=fail,2")
check(asked and sleep == 3600, f"no Wi-Fi: {lines}")
check(len(files("check-*")) == nchecks + 2 * 4, "nothing may be sent without Wi-Fi")

# wet, server says no leak (routine wake): the change is stored as a delta, then it is "same"
lines, asked, sleep = run("rtc", "wet")
check(asked and sleep == 21600, f"no leak: {lines}")
check(len(glob.glob(os.path.join(state, "hist0", "*.D"))) == 1, "delta not stored")
lines, asked, sleep = run("rtc", "wet")
check(not asked and sleep == 21600 and "cam 0: same" in lines, f"after storing: {lines}")

# server gone: not reported, retry in an hour
srv.terminate()
srv.wait()
lines, asked, sleep = run("rtc", "dry")
check(asked and sleep == 3600, f"server down: {lines}")

shutil.rmtree(out, ignore_errors=True)
print(f"wake: FAIL ({fails})" if fails else
      "wake: same scene sleeps, new scene asks Wi-Fi, leak -> video, no leak -> history, "
      "sensor alarm reported on a same scene, no Wi-Fi / no server -> retry")
sys.exit(1 if fails else 0)
