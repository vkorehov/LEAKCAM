"""test028: N random power cuts during plt_write; per cycle mount time, plt_check, commits before the cut."""
import os, sys, termios, time, select, random, re, subprocess
N = int(sys.argv[1]) if len(sys.argv) > 1 else 100
LOG = sys.argv[2] if len(sys.argv) > 2 else "plt_loop.csv"
def openp(path, k230):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    if k230:
        a[0] = a[1] = a[3] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[4] = a[5] = termios.B115200
    else:
        a[3] &= ~(termios.ICANON | termios.ECHO)
    termios.tcsetattr(fd, termios.TCSANOW, a); return fd
def cmd(fd, c, secs, until=None):
    os.write(fd, c.encode() + b"\r\n"); out = b""; t0 = time.time()
    while time.time() - t0 < secs:
        if select.select([fd], [], [], 0.05)[0]:
            try: out += os.read(fd, 65536)
            except OSError: break
        if until and re.search(until.encode(), out): break
    return out.decode(errors="replace")
def bl(c): 
    fd = openp("/dev/ttyACM0", False); r = cmd(fd, c, 1.5); os.close(fd); return r
K230_CONSOLE = "/dev/ttyACM1"   # the BL616's second USB port (link_mux, 2026-10-22); was the CH340 /dev/ttyUSB0
def k230_port(timeout=60):
    # the port stays while the K230 is power-cycled (it is the BL616's): no waiting for a re-enumeration
    t0 = time.time()
    while time.time() - t0 < timeout:
        try: return openp(K230_CONSOLE, False)
        except OSError: time.sleep(0.5)
    return None
new = not os.path.exists(LOG)
log = open(LOG, "a")
if new: log.write("cycle,time,env_init_s,app_init_s,last,checked,bad,free_kb,commits_before_cut,cut_after_s,note\n"); log.flush()
for i in range(1, N + 1):
    bl("pt_off"); time.sleep(1.5); bl("pt_on"); bl("pt_run")
    fd = k230_port()
    if fd is None:
        log.write(f"{i},{time.strftime('%H:%M:%S')},,,,,,,,,no console\n"); log.flush(); continue
    time.sleep(2)
    bt = cmd(fd, "boot_times", 5, r"s  now")
    env = re.search(r"([\d.]+) s  env init", bt); app = re.search(r"([\d.]+) s  app init", bt)
    ck = cmd(fd, "plt_check", 30, r"plt_check: last[^\n]*\n")
    m = re.search(r"last (-?\d+), (\d+) checked, (\d+) bad, free (\d+) KB", ck)
    bads = re.findall(r"seq \d+ BAD \(-?\d+\)", ck)
    cut = random.uniform(2, 20)
    os.write(fd, b"plt_write\r\n"); out = b""; t0 = time.time()
    while time.time() - t0 < cut:
        if select.select([fd], [], [], 0.05)[0]:
            try: out += os.read(fd, 65536)
            except OSError: break
    bl("pt_off"); os.close(fd)
    commits = out.count(b"plt: committed")
    note = ("mount/check failed" if not m else "") + (" " + ";".join(bads) if bads else "")
    log.write(f"{i},{time.strftime('%H:%M:%S')},{env.group(1) if env else ''},{app.group(1) if app else ''},"
              f"{m.group(1) if m else ''},{m.group(2) if m else ''},{m.group(3) if m else ''},{m.group(4) if m else ''},"
              f"{commits},{cut:.1f},{note.strip()}\n"); log.flush()
print("done", N)
