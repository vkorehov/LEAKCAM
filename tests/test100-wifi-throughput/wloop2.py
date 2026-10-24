"""test100 leak + robustness loop: 20 K230 scans, 8 leave/join cycles over the bridge. Verdict from the BL616's
counters (pt_trace counts) and memory (mem per cycle), not from the K230 console text (it interleaves)."""
import sys, time, re
sys.argv = ["x"]
S = "/tmp/claude-1000/-home-vkorehovs-watermeter/486ca449-4ae4-48e6-9e99-447f0bcd9c13/scratchpad"
exec(open(f"{S}/soak.py").read().split("# 1. flash")[0])
sys.path.insert(0, os.path.expanduser("~/k230d-hw/LEAKCAM/tools/power-bringup"))
from mem_leak import parse_bl616, parse_k230, leak_verdict

ssid = psk = None
for l in open(os.path.expanduser("~/.leakcam_wifi")):
    k, _, v = l.strip().partition("=")
    if k == "ssid": ssid = v
    if k == "psk": psk = v
memlog = open(f"{S}/wloop2_mem.txt", "w")


def bl616_mem(tag):
    m = bl("mem", 1.5)
    memlog.write(f"--- {tag}\n{m}\n"); memlog.flush()
    x = parse_bl616(m)
    print(tag, x[-1] if x else "no mem line", flush=True)


def k230_free(tag):
    out = cmd(fd, "free", 3)
    memlog.write(f"--- k230 {tag}\n{out}\n"); memlog.flush()
    x = parse_k230(out)
    print("K230", tag, x[-1] if x else "no free line", flush=True)


bl("pt_off"); bl("pt_on"); bl("pt_wifi bridge", 3)
fd = k230(); bl("pt_run"); cmd(fd, None, 10)
print("bind:", re.findall(r"bl616_rescan: [^\n]*", cmd(fd, "bl616_rescan", 6, r"bl616_rescan: [^\n]*\n")), flush=True)
cmd(fd, None, 4)
k230_free("start")
bl616_mem("before")
for i in range(20):
    cmd(fd, "wifi scan", 15, r"msh />\s*$")
    if i % 5 == 4:
        bl616_mem(f"after scan {i + 1}")
for i in range(8):
    cmd(fd, "wifi disc", 40, r"msh />\s*$")          # the console is complete since the UART interrupt: wait for it
    t = time.time()
    out = cmd(fd, f"wifi join {ssid} {psk}", 40, r"msh />\s*$")
    tj = time.time() - t
    a = None
    for _ in range(15):                                 # DHCP after the link is up
        a = re.search(r"ip address: (\d+\.\d+\.\d+\.\d+)", cmd(fd, "ifconfig", 2))
        if a and a.group(1) != "0.0.0.0":
            break
        time.sleep(1)
    print(f"join {i + 1}: {tj:.1f} s, {'connect success' if 'connect success' in out else 'NO connect success'}, "
          f"ip {a.group(1) if a else '-'}", flush=True)
    bl616_mem(f"after join {i + 1}")
k230_free("end")
os.close(fd); time.sleep(1)
c = dict((k, int(v)) for k, v in re.findall(r"([0-9a-f]{2})=(\d+)", bl("pt_trace counts", 2)))
g = lambda k: c.get(k, 0)
print(f"counts: K230 scan requests {g('14')}, scans run {g('30')}, finished {g('31')}, items {g('32')}; "
      f"joins {g('12')}, connect called {g('62')}, returned {g('63')}, UP {g('52')}; leaves {g('13')}; "
      f"failed sends item {g('33')} done {g('34')}, leave without DISCONNECT {g('35')}, lost K230 msg {g('3e')}, "
      f"lost event {g('3f')}", flush=True)
ok = g('30') == g('31') and g('62') == g('63') and not any(g(k) for k in ('33', '34', '3e', '3f'))
print("BRIDGE:", "PASS" if ok else "FAIL", flush=True)
series = parse_bl616(open(f"{S}/wloop2_mem.txt").read())
print("MEMORY BL616 (scans):", leak_verdict(series[:5])[1], flush=True)
print("MEMORY BL616 (joins):", leak_verdict(series[5:], warmup=1)[1], flush=True)
