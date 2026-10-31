#!/usr/bin/env python3
"""Run the automated LEAKCAM tests one after another and fill their MISSING metrics (harness.py).

  run_tests.py                 every tests/test*/run.py, in number order
  run_tests.py test016 test017 only these (prefix match), in this order
  run_tests.py --list          which tests have a run.py, and what each still misses

Each run.py runs alone (a fresh process: a hang or crash in one cannot take the others with it), its output goes to
the terminal and to tests/<test>/run.log (overwritten each run). A run that exceeds its TIMEOUT_S is killed and
the board switched off. Between tests the BL616 is rebooted after its state is logged (reset_bl616): a state left by
one test (a Wi-Fi bridge that stayed associated after pt_off and went deaf overnight, 2026-10-30) must not reach the
next. At the end: check_tests.py for each test run, and a summary.
"""
import glob
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.abspath(os.path.join(HERE, "..", "..", "tests"))
sys.path.insert(0, os.path.join(HERE, "..", "power-bringup"))
TIMEOUT_S = 3600


def reset_bl616():
    """Log the BL616's heap, Wi-Fi state and Wi-Fi event counts (the evidence of a state a test left), then reboot it
    and wait for its shell: every test starts on a fresh BL616."""
    from board import bl, BL_PORT
    clean = lambda t: re.sub(r"\x1b\[[0-9;]*m", "", t)
    try:
        bl("pt_off", 2)
        state = [l.strip() for c in ("mem", "pt_wifi status", "pt_trace counts") for l in clean(bl(c, 2)).splitlines()
                 if re.match(r"(mem: |pt_wifi status: |trace counts: )", l.strip())]
        print("BL616 before reset: " + " | ".join(state), flush=True)
        bl("reboot", 0.5)
    except Exception as e:                  # the port vanished: say so, the next test's boot() waits for it
        print(f"BL616 reset: {e!r}", flush=True)
    for _ in range(60):
        time.sleep(1)
        if os.path.exists(BL_PORT):
            time.sleep(2)
            return
    print("BL616 reset: the port did not come back in 60 s", flush=True)


def crash_check(when):
    """The BL616's crash record (firmware/bl616/crash_rec.c) before the batch and after every test (user 2026-10-31:
    always check it): a record is decoded, logged and cleared, and stops the batch (stop on any issue). A firmware
    without the recorder stops it too: no test runs where a hang would leave no backtrace."""
    from board import bl
    out = re.sub(r"\x1b\[[0-9;]*m", "", bl("crash", 2))
    if "crash: no record" in out:
        print(f"crash record {when}: none", flush=True)
        return True
    if "crash #" not in out:
        print(f"CRASH RECORDER MISSING in the BL616 firmware ({when}): flash the test firmware with crash_rec.c",
              flush=True)
        return False
    dec = subprocess.run([sys.executable, os.path.join(HERE, "..", "power-bringup", "crash_decode.py")], input=out,
                         capture_output=True, text=True).stdout
    print(f"CRASH RECORD {when}:\n{dec}", flush=True)
    bl("crash clear", 1)
    return False


def main():
    args = sys.argv[1:]
    runs = sorted(glob.glob(os.path.join(TESTS, "test*", "run.py")))
    if args and args[0] == "--list":
        for r in runs:
            d = os.path.dirname(r)
            out = subprocess.run([sys.executable, os.path.join(HERE, "check_tests.py"), os.path.basename(d)[:7]],
                                 capture_output=True, text=True).stdout
            print(f"{os.path.basename(d):40s} {len(re.findall(r'^  - ', out, re.M))} MISSING")
        return
    if args:
        # in the order given: a flash (test019) must run before the tests that need its image
        runs = [r for a in args for r in runs if os.path.basename(os.path.dirname(r)).startswith(a)]
    summary = []
    if not crash_check("before the batch"):
        print("batch not started: see the crash record above")
        return
    for i, r in enumerate(runs):
        d = os.path.dirname(r)
        name = os.path.basename(d)
        if i:
            reset_bl616()
        print(f"\n===== {name} =====", flush=True)
        t0 = time.time()
        with open(os.path.join(d, "run.log"), "w") as logf:
            p = subprocess.Popen([sys.executable, "-u", r], cwd=d, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True)
            try:
                for line in p.stdout:
                    sys.stdout.write(line)
                    sys.stdout.flush()      # the meter prompt must show at once in a redirected batch log
                    logf.write(line)
                    logf.flush()
                    if time.time() - t0 > TIMEOUT_S:
                        raise subprocess.TimeoutExpired(r, TIMEOUT_S)
                rc = p.wait()
            except subprocess.TimeoutExpired:
                p.kill()
                rc = "timeout"
                from board import bl
                bl("pt_off", 2)
        chk = subprocess.run([sys.executable, os.path.join(HERE, "check_tests.py"), name[:7]],
                             capture_output=True, text=True).stdout.strip()
        summary.append(f"{name:40s} rc={rc} {time.time() - t0:5.0f} s  {chk.splitlines()[0] if chk else ''}")
        if not crash_check(f"after {name}"):
            summary.append(f"{name:40s} BL616 CRASH RECORD: batch stopped (see above)")
            break
    print("\n===== summary =====")
    print("\n".join(summary))


if __name__ == "__main__":
    main()
