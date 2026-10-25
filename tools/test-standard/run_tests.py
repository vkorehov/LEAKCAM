#!/usr/bin/env python3
"""Run the automated LEAKCAM tests one after another and fill their MISSING metrics (harness.py).

  run_tests.py                 every tests/test*/run.py, in number order
  run_tests.py test016 test017 only these (prefix match), in this order
  run_tests.py --list          which tests have a run.py, and what each still misses

Each run.py runs alone (a fresh process: a hang or crash in one cannot take the others with it), its output goes to
the terminal and to tests/<test>/run.log (overwritten each run). A run that exceeds its TIMEOUT_S is killed and
the board switched off. At the end: check_tests.py for each test run, and a summary.
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
    for r in runs:
        d = os.path.dirname(r)
        name = os.path.basename(d)
        print(f"\n===== {name} =====", flush=True)
        t0 = time.time()
        with open(os.path.join(d, "run.log"), "w") as logf:
            p = subprocess.Popen([sys.executable, "-u", r], cwd=d, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True)
            try:
                for line in p.stdout:
                    sys.stdout.write(line)
                    logf.write(line)
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
    print("\n===== summary =====")
    print("\n".join(summary))


if __name__ == "__main__":
    main()
