#!/usr/bin/env python3
"""Run the automated LEAKCAM tests one after another and fill their MISSING metrics (harness.py).

  run_tests.py                 every tests/test*/run.py, in number order
  run_tests.py test016 test017 only these (prefix match), in this order
  run_tests.py --list          which tests have a run.py, and what each still misses
  run_tests.py --changed REF   only the tests a change touched: a file changed since REF (git, the working tree too)
                               named in a test's README "## Needs" section, or the test's own folder
  run_tests.py --stress [...]  the stress runs, separate from the regular ones: each run.py that takes --stress gets
                               it (its repeats, soak length); STRESS_ONLY tests run only here
  run_tests.py --wedge-probe ...  after every test also a sleep probe (test139 A1000, ~1.5 min): the BLE core still
                               advertises asleep; a wedge stops the batch naming the test before it
  run_tests.py --essential     the essential suite (tests/README.md "Essential suite"): the server's Go tests, then
                               ESSENTIAL in its order; run after every product, app or server change (user 2026-11-08)

Regular runs are one pass each (a repeat loop or a soak belongs to --stress): the whole set has to stay short enough
to rerun after every change (user, 2026-11-07).

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
# the essential suite (user 2026-11-08: "essential tests, fast, no stability testing, skip the basic hardware tests"):
# automatic only (no meter), the product pair flashed, the board on the full firmware with its network,
# USB-C 1 on the bench, the phone on adb; one pass each (repeats are --stress). Why each one: tests/README.md
# Since 2026-11-09 the hand steps too (user: "add also manual tests to essential now, since we can run hands free"):
# the relay box plugs, unplugs and moves USB-C 1 to the phone (phone.hand(), skill leakcam-bench-relays). Not yet:
# test132 (the leak probe wet/dry has no relay), the 0xx link tests.
ESSENTIAL = ["test135", "test133", "test134", "test108", "test128", "test126", "test138", "test106", "test111",
             "test117", "test129", "test106 --battery", "test117 --battery", "test129 --battery", "test116",
             "test113"]


def reset_bl616():
    """Log the BL616's heap (and on the power test firmware its Wi-Fi state and event counts: the evidence of a state a
    test left), then reboot it and wait for its shell's answer: every test starts on a fresh BL616. No fixed waits
    (user 2026-11-09: "minimal waiting time"): every read returns on its answer; the power test firmware's commands are
    sent only to it (on the product each waited its whole timeout: 14.8 s per transition)"""
    from board import bl, bl_back
    clean = lambda t: re.sub(r"\x1b\[[0-9;]*m", "", t)
    try:
        product = re.search(r"leakcam-build (full|setup)", clean(bl("slot", 2, r"leakcam-build \S+|not found")))
        cmds = ("mem",) if product else ("pt_off", "mem", "pt_wifi status", "pt_trace counts")
        state = [l.strip() for c in cmds
                 for l in clean(bl(c, 2, r"(mem: free|pt_wifi status:|trace counts:)[^\n]*\n|not found")).splitlines()
                 if re.match(r"(mem: |pt_wifi status: |trace counts: )", l.strip())]
        print("BL616 before reset: " + " | ".join(state), flush=True)
        bl("reboot", 0.2)
    except Exception as e:                  # the port vanished: say so, the next test's boot() waits for it
        print(f"BL616 reset: {e!r}", flush=True)
    if bl_back(60) is None:
        print("BL616 reset: the shell did not answer in 60 s", flush=True)

def product_start():
    """the essential suite's starting state before every test: the full firmware with the network stored. A test that
    stopped halfway (test110, 2026-11-08) left the setup firmware, and every next test found the wrong one. Stored:
    back with `slot switch`; another network or none: set up over the cable, the PC as the phone (cable.py)"""
    from board import wifi_credentials
    from phone import board_id, clear_leak, creds, firmware, relay, to_firmware, want
    from board import BL_PORT, bl
    # USB-C 1 really on the bench: VBUS too, not only the data lines (a test stopped after its unplug left VBUS off,
    # the console up, and the next test's setup firmware slept on "USB", 2026-11-09)
    if not os.path.exists(BL_PORT) or "(USB power)" not in bl("battery", 2, r"USB power|on battery|not found"):
        relay([set()])
        t0 = time.time()
        while time.time() - t0 < 20 and not os.path.exists(BL_PORT):
            time.sleep(0.2)
        print("suite start state: USB-C 1 plugged back by the relay box", flush=True)
    # no simulated leak left on the K230 (test137's cam0=object stayed: every check a leak, 2026-11-09): the scene
    # and the histories removed (the next check writes them new), then no open leak at the server (its 1-min wake mode
    # overrides every interval a test sets)
    from board import K230_PORT, cmd, openp
    if os.path.exists(K230_PORT):
        fd = openp(K230_PORT)
        for f in ("sim", "cam0.hist", "cam1.hist"):
            cmd(fd, f"rm /sdcard/leakcam/{f}", 1, r"msh />")
        os.close(fd)
    clear_leak(board_id())
    fw = firmware()
    real = creds() == want(*wifi_credentials())   # the bench network, not any: test110's decoy stayed stored (2026-11-08)
    if fw == "full" and real:
        return
    if real:
        t = to_firmware("full", "slot switch", secs=60)
        print(f"suite start state: {fw} -> full {'in %.0f s' % t if t else 'FAILED'}", flush=True)
        return
    import cable
    t0 = time.time()          # the setup firmware's K230 cable session comes up after its boot (the batch stopped on
    while time.time() - t0 < 30 and not os.path.exists(cable.PORT):   # a missing port, 2026-11-09)
        time.sleep(0.2)
    try:
        state = cable.setup()
    except OSError as e:
        print(f"suite start state: the cable setup failed ({e!r}): the next test starts from {fw}", flush=True)
        return
    t = to_firmware("full", secs=60)
    print(f"suite start state: not the bench network on {fw}: set up over the cable {state}, full "
          f"{'in %.0f s' % t if t else 'FAILED'}", flush=True)


def wedge_check(when):
    """No BLE core wedge (skill bl616-ble-low-power 5) after each test: the setup firmware's detector writes WEDGE into
    the retained evlog, which both firmwares keep; a wedge stops the batch and names the test just run (2026-11-08: a
    wedge met the day after, its cause lost among the runs; user 2026-11-09: "transition between test maybe?").
    --wedge-probe adds a sleep probe after every test (test139 A1000: erase, unplug, found asleep, the network back),
    since the parked form shows only once the setup firmware sleeps"""
    from phone import wedge
    try:
        why = wedge()
    except Exception as e:                  # no console: the next check sees it
        print(f"wedge check {when}: {e!r}", flush=True)
        return True
    print(f"wedge check {when}: {why or 'none'}", flush=True)
    return why is None


def crash_check(when):
    """The BL616's crash record (firmware/bl616_common/crash_rec.c) before the batch and after every test (user 2026-10-31:
    always check it): a record is decoded, logged and cleared, and stops the batch (stop on any issue). A firmware
    without the recorder stops it too: no test runs where a hang would leave no backtrace."""
    from board import bl
    out = ""
    for _ in range(3):          # its own answer; the presence check-ins' Wi-Fi logs can drown one read (2026-11-01)
        out = re.sub(r"\x1b\[[0-9;]*m", "", bl("crash", 3, r"crash: no record|crash #"))
        if "crash: no record" in out or "crash #" in out:
            break
    if "crash: no record" in out:
        print(f"crash record {when}: none", flush=True)
        return True
    if "crash #" not in out:
        print(f"CRASH RECORDER MISSING in the BL616 firmware ({when}): flash the test firmware with crash_rec.c",
              flush=True)
        return False
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "power-bringup", "crash_decode.py")], input=out,
                       capture_output=True, text=True)
    dec = r.stdout + r.stderr       # the decoder names a missing ELF on stderr: the record's build is in it
    print(f"CRASH RECORD {when}:\n{dec}", flush=True)
    bl("crash clear", 1)
    return False


# names every test folder has: a change of one test's copy says nothing about the others
GENERIC = {"README.md", "HISTORY.md", "DESIGN.md", "run.py", "run.log", "console.log", "CMakeLists.txt", "Makefile", "SPEEDS.txt",
           "ENERGY.txt"}


def changed_tests(runs, ref):
    """the runs whose README "## Needs" names a file changed since ref (by its base name), or whose folder changed"""
    repo = os.path.dirname(TESTS)
    names = subprocess.run(["git", "-C", repo, "diff", "--name-only", ref], capture_output=True, text=True,
                           check=True).stdout.split()
    names += subprocess.run(["git", "-C", repo, "ls-files", "--others", "--exclude-standard"], capture_output=True,
                            text=True).stdout.split()
    picked = []
    for r in runs:
        d = os.path.dirname(r)
        try:
            readme = open(os.path.join(d, "README.md")).read()
        except FileNotFoundError:
            readme = ""
        needs = readme.split("## Needs", 1)[1] if "## Needs" in readme else ""
        rel = os.path.relpath(d, repo) + "/"
        if any(n.startswith(rel) and not n.endswith(("README.md", "HISTORY.md", ".log", ".txt")) for n in names) or \
                any(re.search(rf"(?<![\w.]){re.escape(os.path.basename(n))}(?![\w])", needs) for n in names
                    if "." in os.path.basename(n) and os.path.basename(n) not in GENERIC and not n.endswith(".log")
                    and not n.startswith("tools/")):   # the product's code only: a shared test tool named it everywhere
            picked.append(r)
    return picked


def main():
    args = sys.argv[1:]
    essential = False
    runs = sorted(glob.glob(os.path.join(TESTS, "test*", "run.py")))
    stress = "--stress" in args
    probe = "--wedge-probe" in args
    args = [a for a in args if a not in ("--stress", "--wedge-probe")]
    takes_stress = {r: "--stress" in open(r).read() for r in runs}
    stress_only = {r: "STRESS_ONLY = True" in open(r).read() for r in runs}
    if args and args[0] == "--list":
        for r in runs:
            d = os.path.dirname(r)
            out = subprocess.run([sys.executable, os.path.join(HERE, "check_tests.py"), os.path.basename(d)[:7]],
                                 capture_output=True, text=True).stdout
            print(f"{os.path.basename(d):40s} {len(re.findall(r'^  - ', out, re.M))} MISSING")
        return
    if args[:1] == ["--essential"]:
        repo = os.path.dirname(TESTS)
        import shutil   # a non-login shell (a background run) has no /usr/local/go/bin on PATH (2026-11-09)
        gobin = shutil.which("go") or "/usr/local/go/bin/go"
        go = subprocess.run([gobin, "test", "./..."], cwd=os.path.join(repo, "server"), capture_output=True, text=True)
        print("server Go tests:", (go.stdout + go.stderr).strip().splitlines()[-1:], flush=True)
        if go.returncode:
            print(go.stdout + go.stderr)
            print("suite not started: the server's tests fail")
            return
        args = ESSENTIAL
        essential = True
    if args[:1] == ["--changed"]:
        runs = changed_tests(runs, args[1] if len(args) > 1 else "HEAD")
        extra = {}
    elif args:
        # in the order given: a flash (test019) must run before the tests that need its image
        # an entry may carry the run's arguments ("test106 --battery"): the same test twice, once per variant
        picked = [(r, a.split()[1:]) for a in args for r in runs
                  if os.path.basename(os.path.dirname(r)).startswith(a.split()[0])]
        runs, extra = [r for r, _ in picked], [x for _, x in picked]
    else:
        extra = {}
    jobs = [(r, extra[i] if extra else []) for i, r in enumerate(runs)]
    jobs = [(r, x) for r, x in jobs if ((takes_stress[r] or stress_only[r]) if stress else not stress_only[r])]
    print("tests:", " ".join(os.path.basename(os.path.dirname(r))[:7] + "".join(" " + a for a in x) for r, x in jobs)
          or "none", flush=True)
    summary = []
    from phone import firmware
    product = firmware() in ("full", "setup")
    if not wedge_check("before the batch"):
        print("batch not started: the BLE core is wedged (power-cycle the board)")
        return
    if not crash_check("before the batch"):
        print("batch not started: see the crash record above")
        return
    for i, (r, x) in enumerate(jobs):
        d = os.path.dirname(r)
        name = os.path.basename(d)
        if i:
            reset_bl616()
        if essential or product:    # the product pair on the board: every test from the suite's start state
            product_start()
        print(f"\n===== {name}{''.join(' ' + a for a in x)} =====", flush=True)
        t0 = time.time()
        with open(os.path.join(d, "run.log"), "w") as logf:
            p = subprocess.Popen([sys.executable, "-u", r] + x + (["--stress"] if stress and takes_stress[r] else []), cwd=d, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
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
        summary.append(f"{(name + ''.join(' ' + a for a in x)):40s} rc={rc} {time.time() - t0:5.0f} s  {chk.splitlines()[0] if chk else ''}")
        if not crash_check(f"after {name}"):
            summary.append(f"{name:40s} BL616 CRASH RECORD: batch stopped (see above)")
            break
        if probe and not name.startswith("test139"):
            pr = subprocess.run([sys.executable, "-u", os.path.join(TESTS, "test139-replug-stability", "run.py"),
                                 "A1000"], capture_output=True, text=True)
            print(f"wedge probe after {name}: {'pass' if pr.returncode == 0 else 'FAIL'}", flush=True)
            if pr.returncode:
                print(pr.stdout[-3000:], flush=True)
        if not wedge_check(f"after {name}") or (probe and pr.returncode):
            summary.append(f"{name:40s} BLE CORE WEDGE after this test: batch stopped (see above)")
            break
    print("\n===== summary =====")
    print("\n".join(summary))


if __name__ == "__main__":
    main()
