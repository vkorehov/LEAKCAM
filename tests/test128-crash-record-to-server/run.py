"""test128: crash records reach the server over Wi-Fi (no phone). Steps of README.md:

    python3 run.py [steps]        default "1 2 3 5 7"; the pair flashed, the full firmware running, USB-C 1 in

1 exception in the full firmware -> its check-in after the reset sends it; 2 the same for a watchdog hang; 3 server
stopped at that check-in -> kept on the board, sent once it is back, one copy; 5 a crash, then the switch to setup
before any check-in -> the setup firmware's API step sends it (`switch_to_full usb`: back to the full firmware); 7 two
crashes before a send -> count 2. Each server copy is decoded with crash_decode.py (the kept ELF of its build).
"""
import importlib.util
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "power-bringup"))
sys.path.insert(0, os.path.join(REPO, "tools", "test-runner"))
spec = importlib.util.spec_from_file_location("t126", os.path.join(REPO, "tests/test126-firmware-slot-switch/run.py"))
t126 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t126)
from board import BL_PORT   # noqa: E402
from phone import SERVER, board_id   # noqa: E402

ask, log, which, switch = t126.ask, t126.log, t126.which, t126.switch
SEND_WAIT_S = 60          # the full firmware checks in 5 s after its start; the check itself a few seconds
fails = []
DEV = board_id()          # from the USB serial number: read once, the port is gone during every reset


def pi(c):
    return subprocess.run(["ssh", "pi@192.168.1.159", c], capture_output=True, text=True, timeout=30)


def crashes():
    with urllib.request.urlopen(f"{SERVER}/v1/devices/{DEV}/crashes", timeout=5) as f:
        return json.loads(f.read())


def rec():
    """the board's record line, or None"""
    o = ask("crash", 3, r"crash: no record|crash #[^\n]*")
    m = re.search(r"crash #[^\r\n]*", o)
    return m.group(0) if m else None


def reset_by(cmd):
    """a crash command; back when the console answers again"""
    t0 = time.time()
    ask(cmd, 1)
    gone = False
    while time.time() - t0 < 40:
        if not os.path.exists(BL_PORT):
            gone = True
        elif gone and t126.slot():
            return time.time() - t0
        time.sleep(0.2)
    return None


def wait_sent(before, secs=SEND_WAIT_S):
    """seconds until the server has a new record and the board cleared its own, or None"""
    t0 = time.time()
    while time.time() - t0 < secs:
        if len(crashes()) > before and rec() is None:
            return time.time() - t0
        time.sleep(0.5)
    return None


def decode(c):
    r = subprocess.run([sys.executable, os.path.join(REPO, "tools/power-bringup/crash_decode.py")], input=c["record"],
                       capture_output=True, text=True)
    m = re.search(r"^\s+mepc 0x[0-9a-f]+\s+(\S+)", r.stdout, re.M)
    return m.group(1) if m else (r.stderr.strip() or "no decode")


def check(name, ok, detail=""):
    log(("PASS " if ok else "FAIL ") + name, detail)
    if not ok:
        fails.append(name)


def crash_and_send(cmd, kind, step):
    n0 = len(crashes())
    back = reset_by(cmd)
    log(f"step {step}: `{cmd}`, console back in {back:.1f} s" if back else f"step {step}: no console after `{cmd}`")
    secs = wait_sent(n0)
    c = crashes()[-1] if len(crashes()) > n0 else None
    check(f"step {step}: {kind} at the server, cleared on the board", secs is not None and c and kind in c["record"],
          f"{secs:.1f} s after the console was back; {c['build']} #{c['count']}; decoded mepc: {decode(c)}"
          if secs is not None and c else f"server {len(crashes())} records, board: {rec()}")


def main():
    steps = sys.argv[1].split() if len(sys.argv) > 1 else ["1", "2", "3", "5", "7"]
    if which() == "setup":
        switch("full")
    if which() != "full":
        sys.exit("not in the full firmware")
    if rec():
        ask("crash clear", 2)
    if "1" in steps:
        crash_and_send("crash test", "exception", 1)
    if "2" in steps:
        crash_and_send("crash hang", "watchdog hang", 2)
    if "3" in steps:
        n0 = len(crashes())
        pi("sudo systemctl stop leakcam-server")
        log("step 3: server stopped")
        try:
            reset_by("crash test")
            t0 = time.time()                     # its check-in at 5 s fails at the API: polled, not 25 s fixed
            while time.time() - t0 < 40 and not re.search(r"missed [1-9]", ask("presence", 3, r"missed \d+")):
                time.sleep(0.3)
            kept = rec()
            check("step 3: kept on the board while the server is down", kept is not None, kept or "")
        finally:
            pi("sudo systemctl start leakcam-server")
            log("step 3: server started")
        ask("presence now", 1)                  # a check-in now, not at the next interval (it waited 57 s)
        t0 = time.time()
        while time.time() - t0 < 90 and rec() is not None:
            time.sleep(0.5)
        got = [c for c in crashes()[n0:]]
        check("step 3: sent at the check-in once the server is back, one copy", rec() is None and len(got) == 1,
              f"{time.time() - t0:.1f} s after the server was back; {len(got)} new on the server")
    if "5" in steps:
        # the server down across the crash and the switch: neither the full firmware's check-in nor the setup
        # firmware's first check (it checks the stored network at its start on USB power, test127) can take it; with
        # the server back the setup firmware's next start reaches the API first: its check sends the full firmware's
        # record, then it hands over to the full firmware
        n0 = len(crashes())
        pi("sudo systemctl stop leakcam-server")
        try:
            reset_by("crash test")
            t0 = time.time()
            ask("switch_to_setup lost", 1)
            while time.time() - t0 < 60 and which() != "setup":
                time.sleep(0.3)
            at = time.time() - t0
            # its check, read as state (`switch_to_full`): after `switch_to_setup lost` there is none ("no check"),
            # so the old wait for a "failed its check" line always sat its full 15 s; waited only while one runs
            o = "check running"
            while time.time() - t0 < at + 15 and "check running" in o:
                o = ask("switch_to_full", 1, r"switch_to_full: [^\n]*\n")
                if "check running" in o:
                    time.sleep(0.3)
            o = (re.search(r"switch_to_full: [^\r\n]*", o) or re.search("", "")).group(0)
            log("step 5: crashed in the full firmware; in the setup firmware after", f"{at:.1f} s;",
                o, f"({time.time() - t0:.1f} s)")
            kept = rec() if which() == "setup" else None
            log("step 5: the setup firmware holds", kept)
        finally:
            pi("sudo systemctl start leakcam-server")
        reset_by("reboot")                       # the setup firmware starts again: its check reaches the API now
        secs = wait_sent(n0, 90)
        c = crashes()[-1] if len(crashes()) > n0 else None
        check("step 5: the setup firmware's check sent the full firmware's record", kept is not None and
              c is not None and c["build"].startswith("full "),
              f"{secs:.1f} s; {c['build']} #{c['count']}, decoded mepc: {decode(c)}" if c and secs else f"board {rec()}")
        t0 = time.time()
        while time.time() - t0 < 60 and which() != "full":
            time.sleep(0.3)
        check("step 5: then the hand-over to the full firmware", which() == "full", f"{time.time() - t0:.0f} s")
    if "7" in steps:
        if which() == "setup":
            switch("full")
        n0 = len(crashes())
        pi("sudo systemctl stop leakcam-server")
        try:
            reset_by("crash test")
            reset_by("crash test")
            log("step 7: two crashes, board:", rec())
        finally:
            pi("sudo systemctl start leakcam-server")
        ask("presence now", 1)
        t0 = time.time()
        while time.time() - t0 < 90 and rec() is not None:
            time.sleep(0.5)
        got = crashes()[n0:]
        check("step 7: one record with count 2", len(got) == 1 and got[0]["count"] == 2,
              f"{[(c['build'], c['count']) for c in got]}")
    log("PASS" if not fails else f"FAIL ({len(fails)}): {fails}")


if __name__ == "__main__":
    main()
