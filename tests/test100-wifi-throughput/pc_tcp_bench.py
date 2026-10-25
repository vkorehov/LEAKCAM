#!/usr/bin/env python3
"""test100 step 4 (2026-10-25): the LAN PC's end of the K230's RT-Thread `iperf` (a plain TCP stream, no iperf
protocol), so the PC needs no iperf install.
  pc_tcp_bench.py sink [port]              receive every connection (K230: iperf -c <pc> -t N), print Mbit/s per second
                                           and the total per connection
  pc_tcp_bench.py send <host> <secs> [port]  send to the K230's `iperf -s` for <secs>, print the total
Output also goes to pc_tcp_bench.log next to the script."""
import os, socket, sys, threading, time

LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pc_tcp_bench.log")


def log(s):
    line = time.strftime("%H:%M:%S ") + s
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\n")


def serve(conn, peer):
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    total, t0, tick, n = 0, time.time(), time.time(), 0
    while True:
        b = conn.recv(1 << 16)
        if not b:
            break
        total += len(b); n += len(b)
        if time.time() - tick >= 1:
            log(f"{peer[0]} {n * 8 / (time.time() - tick) / 1e6:.2f} Mbit/s")
            tick, n = time.time(), 0
    dt = time.time() - t0
    log(f"{peer[0]} done: {total} B in {dt:.2f} s = {total * 8 / dt / 1e6:.2f} Mbit/s")
    conn.close()


def sink(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port)); s.listen(4)
    log(f"sink on {port}")
    while True:
        c, peer = s.accept()
        log(f"{peer[0]} connected")
        threading.Thread(target=serve, args=(c, peer), daemon=True).start()


def send(host, secs, port):
    s = socket.create_connection((host, port), timeout=10)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
    buf, total, t0 = b"\0" * (1 << 16), 0, time.time()
    while time.time() - t0 < secs:
        total += s.send(buf)
    s.close()
    dt = time.time() - t0
    log(f"send {host} done: {total} B in {dt:.2f} s = {total * 8 / dt / 1e6:.2f} Mbit/s")


if sys.argv[1] == "sink":
    sink(int(sys.argv[2]) if len(sys.argv) > 2 else 5001)
else:
    send(sys.argv[2], float(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 5001)
