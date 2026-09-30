#!/usr/bin/env python3
"""Host test of the wake algorithm: leakcam_wake (host build, cameras from test/wake_stub.c)
against mock_server.py, with this script in the agent's place (answers "wifi" on stdin).

  test/test_wake.py <leakcam_wake host binary> <its STATE_DIR>
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zlib

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


# the factory kmodel (CHANGE_KMODEL of the host build) and the one the server offers
FACTORY, NEW = b"factory change net", b"retrained change net"
with open(state + ".kmodel", "wb") as f:
    f.write(FACTORY)
new_kmodel = os.path.join(out, "new.kmodel")
with open(new_kmodel, "wb") as f:
    f.write(NEW)


def start_server(outdir, thr):
    srv = subprocess.Popen([sys.executable, os.path.join(HERE, "..", "mock_server.py"), "--port", "0",
                            "--dir", outdir, "--nn", new_kmodel, "--thr", thr], stdout=subprocess.PIPE, text=True)
    return srv, re.search(r"port (\d+)", srv.stdout.readline()).group(1)


srv, port = start_server(out, "0.30,0.50")


def run(reason, scene, answer="wifi=ok", probe_mv="1650", rh="45.5", t="21.3", bat="3712", change="1"):
    """one wake: (stdout lines, was Wi-Fi asked for, sleep seconds). The sensor values arrive as
    the agent passes them from WAKE, always all four; change is the change net's distance"""
    env = dict(os.environ, LEAKCAM_TEST_SCENE=scene, LEAKCAM_PROBE_MV=probe_mv, LEAKCAM_RH=rh,
               LEAKCAM_T=t, LEAKCAM_BAT_MV=bat, LEAKCAM_TEST_CHANGE=change)
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


def reports():
    return len(files("check-*.json"))


def last_query():
    with open(max(files("check-*.json"), key=os.path.getmtime)) as f:
        return json.load(f)


def saved_nn():
    """the kmodel of the newest valid-looking slot, as the board stored it (32-byte header)"""
    slots = [os.path.join(state, f"change.{i}") for i in (0, 1)]
    slots = [p for p in slots if os.path.exists(p)]
    return open(max(slots, key=os.path.getmtime), "rb").read()[32:] if slots else None


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
check(any("battery 3712 mV" in l for l in lines), f"battery not taken from the agent: {lines}")
check(len(files("check-*-cam0.pgm")) == 1 and len(files("check-*-cam1.pgm")) == 1, "check upload")
check(os.path.getsize(files("check-*-cam0.pgm")[0]) == 15 + 320 * 240, "PGM size")
check(os.path.exists(os.path.join(state, "cam0.hist.0")) or os.path.exists(os.path.join(state, "cam0.hist.1")),
      "history view not stored")
check(len(glob.glob(os.path.join(state, "hist0", "*.K"))) == 1, "keyframe not stored")
q = last_query()
check(q["nn"] == f"{zlib.crc32(FACTORY):08x}" and q["thr"] == "0.44,0.44" and q["cam0"] == "first,-1.000",
      f"factory net reported: {q}")

# the server offered its net: downloaded once, checked, stored; used from the next wake on
check(len(files("nn-get-*")) == 1 and saved_nn() == NEW, f"net update: {lines}")
check(any("saved for the next wake" in l for l in lines), f"net update not logged: {lines}")

# same scene: sleep at once, no Wi-Fi, nothing sent
lines, asked, sleep = run("rtc", "dry")
check(not asked and sleep == 21600 and "cam 0: same" in lines, f"same scene: {lines}")
check(reports() == 1, "nothing may be sent for an unchanged scene")

# same scene, but the sensors alarm: reported whatever the cameras see
nchecks = reports()
lines, asked, sleep = run("humid", "dry", rh="87.0")
check(asked and sleep == 21600 and "cam 0: same" in lines, f"humidity alarm, same scene: {lines}")
lines, asked, sleep = run("rtc", "dry", probe_mv="700")
check(asked and sleep == 600 and "leak reported" in lines, f"probe wet, same scene: {lines}")
lines, asked, sleep = run("rtc+leak+humid", "dry", rh="88.0")
check(asked and sleep == 600 and "leak reported" in lines, f"combined reasons, same scene: {lines}")
lines, asked, sleep = run("leak", "dry")
check(asked and sleep == 600 and "leak reported" in lines, f"probe wake, same scene: {lines}")
check(reports() == nchecks + 4, "every sensor alarm must reach the server")
check(len(files("nn-get-*")) == 1 and last_query()["nn"] == f"{zlib.crc32(NEW):08x}"
      and last_query()["thr"] == "0.30,0.50", "the stored net must be used and not downloaded again")
stream_log = state + ".stream"                                      # test/fake_stream's calls
if os.path.exists(stream_log):
    os.remove(stream_log)

# imgdiff sees a change, the change net calls it light: sleep, no Wi-Fi, the view stays dry
lines, asked, sleep = run("rtc", "wet", change="0.1")
check(not asked and sleep == 21600 and "cam 0: change net 0.100" in lines and "cam 0: same" in lines,
      f"light only: {lines}")
check(reports() == nchecks + 4, "nothing may be sent for a change of light")
lines, asked, sleep = run("rtc", "wet", "wifi=fail,2", change="-1")
check(asked and sleep == 3600 and "cam 0: changed" in lines, f"change net failed, must report: {lines}")

# per-camera thresholds from the server: 0.4 is a change for camera 0 (0.30), light for 1 (0.50)
lines, asked, sleep = run("rtc", "wet", "wifi=fail,2", change="0.4")
check(asked and "cam 0: changed" in lines and "cam 1: same" in lines, f"per-camera thresholds: {lines}")

# piggyback: a sensor alarm reports anyway, and the pairs the net called light go along
lines, asked, sleep = run("humid", "wet", rh="87.0", change="0.1")
q = last_query()
check(asked and q["cam0"] == "light,0.100" and q["cam1"] == "light,0.100", f"vetoed pairs marked: {q}")
check(any(f.endswith(f"-cam1-view.pgm") for f in files("check-*-view.pgm")), "vetoed pair's view not sent")
check(len(glob.glob(os.path.join(state, "hist0", "*"))) == 1, "a light-only frame must not be stored")

# a light-only pair near the threshold (0.25 >= 0.30 - 0.1): reported as a sample, once a day
lines, asked, sleep = run("rtc", "wet", change="0.25")
check(asked and sleep == 21600 and "sample" in last_query(), f"sample: {lines}")
lines, asked, sleep = run("rtc", "wet", change="0.25")
check(not asked and sleep == 21600, f"a second sample the same day: {lines}")
nchecks += 3

# puddle, BL616 probe wet: server says leak -> 5 s video from both cameras, nothing stored
lines, asked, sleep = run("leak", "wet")
check(asked and sleep == 600 and "leak reported" in lines, f"leak: {lines}")
calls = open(stream_log).read().split("\n")[:-1] if os.path.exists(stream_log) else []
check(calls == ["127.0.0.1:1935 5"], f"live stream to the server's RTMP ingest, 5 s: {calls}")
check(len(glob.glob(os.path.join(state, "hist0", "*"))) == 1, "a reported leak must not be stored")

# a clip asked for on the server: streamed with the next report, leak or not, and only once
urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{port}/v1/video?s=12", data=b"", method="POST")).read()
lines, asked, sleep = run("humid", "dry", rh="87.0")
calls = open(stream_log).read().split("\n")[:-1] if os.path.exists(stream_log) else []
check(asked and sleep == 21600 and calls[1:] == ["127.0.0.1:1935 12"], f"requested clip, server's length: {lines} {calls}")
lines, asked, sleep = run("humid", "dry", rh="87.0")
calls = open(stream_log).read().split("\n")[:-1] if os.path.exists(stream_log) else []
check(len(calls) == 2, f"a request streams once: {lines} {calls}")
urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{port}/v1/video?s=1000", data=b"", method="POST")).read()
lines, asked, sleep = run("humid", "dry", rh="87.0")
calls = open(stream_log).read().split("\n")[:-1] if os.path.exists(stream_log) else []
check(calls[2:] == ["127.0.0.1:1935 300"], f"clip capped at 300 s: {calls}")
nchecks += 2

# still wet, Wi-Fi refused (no credentials): retry in an hour, nothing sent
lines, asked, sleep = run("rtc", "wet", "wifi=fail,2")
check(asked and sleep == 3600, f"no Wi-Fi: {lines}")
check(reports() == nchecks + 5, "nothing may be sent without Wi-Fi")

# wet, server says no leak (routine wake): the change is stored as a delta, then it is "same"
lines, asked, sleep = run("rtc", "wet")
check(asked and sleep == 21600, f"no leak: {lines}")
check(len(glob.glob(os.path.join(state, "hist0", "*.D"))) == 1, "delta not stored")
lines, asked, sleep = run("rtc", "wet")
check(not asked and sleep == 21600 and "cam 0: same" in lines, f"after storing: {lines}")

# a damaged update falls back to the factory net, and the server's offer repairs it
slot = max((os.path.join(state, f"change.{i}") for i in (0, 1) if os.path.exists(os.path.join(state, f"change.{i}"))),
           key=os.path.getmtime)
data = bytearray(open(slot, "rb").read())
data[-1] ^= 0xFF
open(slot, "wb").write(bytes(data))
lines, asked, sleep = run("humid", "wet", rh="87.0")
check(last_query()["nn"] == f"{zlib.crc32(FACTORY):08x}" and len(files("nn-get-*")) == 2 and saved_nn() == NEW,
      f"damaged update: {lines}")

# new thresholds for the same net: stored without a download
srv.terminate()
srv.wait()
out2 = tempfile.mkdtemp(prefix="leakcam_mock_")
srv, port = start_server(out2, "0.35,0.50")
with open(os.path.join(state, "server"), "w") as f:
    f.write(f"127.0.0.1 {port}\n")
lines, asked, sleep = run("humid", "wet", rh="87.0")
check(not glob.glob(os.path.join(out2, "nn-get-*")) and saved_nn() == NEW, f"thresholds only: {lines}")
lines, asked, sleep = run("humid", "wet", rh="87.0")
check(any("thresholds 0.35 0.50" in l for l in lines), f"new thresholds not used: {lines}")
shutil.rmtree(out2, ignore_errors=True)

# server gone: not reported, retry in an hour
srv.terminate()
srv.wait()
lines, asked, sleep = run("rtc", "dry")
check(asked and sleep == 3600, f"server down: {lines}")

shutil.rmtree(out, ignore_errors=True)
print(f"wake: FAIL ({fails})" if fails else
      "wake: same scene sleeps, light only sleeps (sampled once a day, sent along with alarms), net and "
      "thresholds updated from the server, the clip length from the server (capped), a requested clip streamed once, new scene asks Wi-Fi, leak -> video, no leak -> history, "
      "sensor alarm reported on a same scene, no Wi-Fi / no server -> retry")
sys.exit(1 if fails else 0)
