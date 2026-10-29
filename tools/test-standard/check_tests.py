#!/usr/bin/env python3
"""The LEAKCAM test README standard: check it, bring READMEs up to it, and refuse a run that leaves gaps.

  check_tests.py            every tests/test*/README.md: sections, metric rows, MISSING count per test
  check_tests.py --fix      add missing sections and metric rows as **MISSING**, turn the old placeholders
                            ("to fill", "not measured yet", ...) into **MISSING** (the hint after them is kept)
  check_tests.py testNNN    after a run of that test: exit 1 while its Metrics still hold a **MISSING**
  check_tests.py --index    rewrite the test table of tests/README.md from every README's title and Status line

Standard (user rule, 2026-10-24: all tests the same, missing metrics marked, none left after the next run; layout
user, 2026-10-29: the README holds only what is current, history apart, product lessons in a skill):
- README.md, sections in order: Setup, Needs, Steps, Pass, Result, Metrics, Boot time, Battery energy. A
  "Status:" line under the title says the current verdict and its date. Result holds only the findings that hold
  now (and what is still open); Metrics only current values. Nothing superseded, no run log, no obstacles.
- The Metrics table carries every row of REQUIRED (variant names match; any other row is the test's own speed /
  rate row). A value is measured, or `n/a: <reason>` when the metric does not apply to that test, or **MISSING**.
- A run of a test fills every **MISSING** of its Metrics (skill leakcam-test-metrics); `check_tests.py testNNN`
  says which are left.
- HISTORY.md: earlier runs, superseded findings and every obstacle (user rule, 2026-10-24: document obstacles),
  newest first, under "## Runs" and "## Obstacles". An obstacle that matters for the product build also becomes a
  rule in skill leakcam-product-build.
"""
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tests")
MISSING = "**MISSING**"
SECTIONS = ["Setup", "Needs", "Steps", "Pass", "Result", "Metrics", "Boot time", "Battery energy"]
SECTION_HINT = {
    "Setup": "hookup, firmware / image, and the probes named in Metrics",
    "Needs": "the kernel options / drivers, SDK components, libraries and programs this test uses",
    "Steps": "the steps of the test",
    "Pass": "the pass criterion",
    "Result": "the current findings only (history: HISTORY.md)",
    "Metrics": "the full block of skill leakcam-test-metrics",
    "Boot time": "the boot time of this test's image and its share",
    "Battery energy": "current -> power -> energy per operation and per day",
}
# (canonical row name, pattern a row name may match, top / budget, note)
REQUIRED = [
    ("Boot, power-on -> READY (median / max of 5)", r"^Boot\b", "target (test022)", "longest step"),
    ("This part's boot share", r"^This part's boot share", "-", "driver / mount / probe"),
    ("Test run time", r"^Test run time", "-", "setup separately"),
    ("Speed / rate against the top", None, "SPEEDS.txt", "% of the top"),
    ("Current idle / during (min / avg / peak)", r"^Current", "floor (test031)", "USB-C 1 VBUS meter"),
    ("Voltage under load (min)", r"^Voltage under load", "cutoff 3.10 V (bug-004)", "probe points named"),
    ("Power, energy per operation", r"^Power, energy", "-", ""),
    ("Per day", r"^Per day", "ENERGY.txt line", "n per day"),
    ("Temperature die / board (start / peak / end)", r"[Tt]emperature", "K230 limit (datasheet)", "chip_temp, AHT20"),
    ("Memory BL616 / K230 (start / min / end), leak", r"^Memory", "no leak; budget (DESIGN.md)",
     "BL616 `mem`, K230 `free`; mem_leak.py over >= 8 cycles"),
    ("Image (sizes)", r"^Image", "minimal image (rule)", "the image this run used"),
    ("Compile flags", r"^Compile flags", "smallest code (rule)", ""),
    ("Features over the minimal image", r"^Features over the minimal image", "test008 floor", "bytes, boot ms, RAM, current"),
    ("Optimised build (-O3, LTO, -march)", r"^Optimised build", "the faster one stays", ""),
]
# old placeholders -> MISSING (case-insensitive, at the start of a table cell or a line)
OLD = re.compile(r"(?i)\b(to fill|not measured yet|not measured: run before the metrics rule|not recorded yet|"
                 r"not written at the time|to measure)\b")


def sections(text):
    return [h.strip() for h in re.findall(r"^## ([^\n(]+)", text, re.M)]


def metrics_block(text):
    m = re.search(r"^## Metrics[^\n]*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    return m


def metric_rows(block):
    rows = []
    for l in block.splitlines():
        if l.startswith("| ") and not l.startswith("| Metric") and not l.startswith("|---"):
            cells = [c.strip() for c in l.strip().strip("|").split("|")]
            rows.append((l, cells))
    return rows


def missing_rows(rows):
    names = [c[0] for _, c in rows]
    used = set()
    absent = []
    for canon, pat, _, _ in REQUIRED:
        if pat is None:
            continue
        hit = [i for i, n in enumerate(names) if re.search(pat, n)]
        if hit:
            used.update(hit)
        else:
            absent.append(canon)
    others = [i for i in range(len(names)) if i not in used]
    if not others:
        absent.insert(3, REQUIRED[3][0])          # no test-specific row: the generic speed row is missing
    return absent


def fix_text(text):
    changed = []
    have = sections(text)
    for sec in SECTIONS:
        if sec not in have:
            text = text.rstrip("\n") + f"\n\n## {sec}\n\n{MISSING}: {SECTION_HINT[sec]}.\n"
            changed.append(f"section {sec}")
    m = metrics_block(text)
    rows = metric_rows(m.group(1))
    absent = missing_rows(rows)
    if absent:
        add = "\n".join(f"| {name} | {MISSING} | {top} | {note} |" for name, _, top, note in REQUIRED if name in absent)
        last = rows[-1][0] if rows else None
        if last:
            text = text.replace(last, last + "\n" + add, 1)
        else:
            body = m.group(1)
            table = "| Metric | Value | Top / budget | Note |\n|---|---|---|---|\n" + add + "\n"
            text = text.replace(body, body.rstrip("\n") + "\n\n" + table + "\n", 1)
        changed += [f"row {a}" for a in absent]
    new = []
    for line in text.split("\n"):
        if OLD.search(line) and MISSING not in line:
            if line.startswith("|"):
                cells = line.split("|")
                cells = [OLD.sub(MISSING, c, count=1) if OLD.search(c) else c for c in cells]
                line = "|".join(cells)
            else:
                line = OLD.sub(MISSING, line, count=1)
            changed.append("placeholder")
        new.append(line)
    text = "\n".join(new)
    ordered = reorder(text)
    if ordered != text:
        changed.append("section order")
    return ordered, changed


def reorder(text):
    """sections in the standard order; any other section stays after the one it followed"""
    parts = re.split(r"(?m)^(?=## )", text)
    head, body = parts[0], parts[1:]
    name = lambda part: re.match(r"## ([^\n(]+)", part).group(1).strip()
    known = {name(p): p for p in body if name(p) in SECTIONS}
    out, extras_after = [], {}
    prev = None
    for p in body:
        if name(p) in SECTIONS:
            prev = name(p)
        else:
            extras_after.setdefault(prev, []).append(p)
    for sec in [None] + SECTIONS:
        if sec is not None and sec in known:
            out.append(known[sec])
        out += extras_after.get(sec, [])
    out = [p if p.endswith("\n\n") else p.rstrip("\n") + "\n\n" for p in out]
    return head + "".join(out).rstrip("\n") + "\n"


def metrics_missing(text):
    m = metrics_block(text)
    if not m:
        return ["the Metrics section"]
    out = [c[0] for _, c in metric_rows(m.group(1)) if any(MISSING in x or OLD.search(x) for x in c[1:2])]
    return out + missing_rows(metric_rows(m.group(1)))


def history_ok(readme):
    """HISTORY.md next to the README, with its Runs and Obstacles sections"""
    p = os.path.join(os.path.dirname(readme), "HISTORY.md")
    if not os.path.exists(p):
        return False
    h = open(p).read()
    return "## Runs" in h and "## Obstacles" in h


def index():
    """the table between "| Test | Date | Result |" and the blank line after it, from each README's Status line"""
    rows, high = [], []
    for f in sorted(glob.glob(os.path.join(ROOT, "test*", "README.md"))):
        t = open(f).read()
        d = os.path.basename(os.path.dirname(f))
        title = re.match(r"# (test(\d+)):\s*(.*)", t)
        st = re.search(r"^Status:\s*(.*)$", t, re.M)
        s = st.group(1).strip() if st else f"{MISSING}: no Status line"
        m = re.match(r"(\*\*[^*]+\*\*)\s*\(([^)]*)\)\s*:?\s*(.*)", s)
        date, res = (m.group(2), m.group(1) + (": " + m.group(3) if m.group(3) else "")) if m else ("", s)
        (high if int(title.group(2)) >= 100 else rows).append(
            f"| [{title.group(1)} {title.group(3)}]({d}/README.md) | {date} | {res} |")
    table = ("| Test | Date | Result |\n|---|---|---|\n" + "\n".join(rows) +
             "\n| **High level (100+): the product's whole flow** | | |\n" + "\n".join(high) + "\n")
    p = os.path.join(ROOT, "README.md")
    text = open(p).read()
    text = re.sub(r"(?ms)^\| Test \| Date \| Result \|\n.*?\n\n", lambda _: table + "\n", text, count=1)
    open(p, "w").write(text)
    print(f"tests/README.md: {len(rows) + len(high)} tests indexed")


def main():
    files = sorted(glob.glob(os.path.join(ROOT, "test*", "README.md")))
    args = sys.argv[1:]
    if args == ["--index"]:
        return index()
    if args and args[0] not in ("--fix",):
        want = [f for f in files if os.path.basename(os.path.dirname(f)).startswith(args[0])]
        if not want:
            sys.exit(f"no test {args[0]}")
        text = open(want[0]).read()
        left = metrics_missing(text)
        name = os.path.basename(os.path.dirname(want[0]))
        bad = False
        if left:
            print(f"{name}: {len(left)} metric(s) still MISSING after this run - fill them or write `n/a: <reason>`:")
            for r in left:
                print(f"  - {r}")
            bad = True
        if not history_ok(want[0]):
            print(f"{name}: HISTORY.md missing or without its Runs / Obstacles sections")
            bad = True
        if bad:
            sys.exit(1)
        print(f"{name}: every metric filled, history kept")
        return
    total = 0
    for f in files:
        text = open(f).read()
        if args == ["--fix"]:
            text, changed = fix_text(text)
            if changed:
                open(f, "w").write(text)
        secs = [s for s in SECTIONS if s not in sections(text)]
        left = metrics_missing(text)
        total += len(left)
        name = os.path.basename(os.path.dirname(f))
        hist = "ok" if history_ok(f) else "MISSING"
        print(f"{name:45s} sections missing {len(secs)}  metrics MISSING {len(left):2d}  history {hist}")
    print(f"{len(files)} tests, {total} metric values MISSING")


if __name__ == "__main__":
    main()
