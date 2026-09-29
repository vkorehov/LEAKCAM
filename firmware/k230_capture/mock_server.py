#!/usr/bin/env python3
"""Mock LEAKCAM server: what leakcam_wake and leakcam_stream talk to until the real one exists.

  mock_server.py [--port 8000] [--dir mock_out] [--leak probe|always|never]

POST /v1/check?reason=<r>&probe_mv=<mV>&bat_mv=<mV>&rh=<%RH>&t=<C>
                            body: one binary PGM per camera, back to back. Saved as
                            check-<n>-cam<i>.pgm. Answers {"leak":true|false}: with --leak
                            probe (default) a leak is what the BL616's probes say: the
                            reason "leak", or the probe node below 825 mV (wet).
POST /v1/video?cam=<N>      chunked FLV (H.264 + G.711 mu-law 8 kHz), saved as video-<n>-cam<N>.flv.
                            Answers {"bytes":<received>}.

On the device, /sdcard/leakcam/server holds "<host> <port>" of this server.
"""
import argparse
import json
import os
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

counter = 0
lock = threading.Lock()


def next_n():
    global counter
    with lock:
        counter += 1
        return counter


def split_pgms(body):
    """binary PGMs back to back -> list of whole PGM files"""
    out, i = [], 0
    while i < len(body):
        m = re.match(rb"P5\s+(\d+)\s+(\d+)\s+(\d+)\s", body[i:])
        if not m:
            break
        end = i + m.end() + int(m.group(1)) * int(m.group(2))
        out.append(body[i:end])
        i = end
    return out


class Handler(BaseHTTPRequestHandler):
    def reply(self, obj):
        data = json.dumps(obj, separators=(",", ":")).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def read_body(self):
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            data = bytearray()
            while True:
                size = int(self.rfile.readline().split(b";")[0], 16)
                if size == 0:
                    self.rfile.readline()
                    return bytes(data)
                data += self.rfile.read(size)
                self.rfile.readline()
        return self.rfile.read(int(self.headers.get("Content-Length", 0)))

    def do_POST(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        body = self.read_body()
        n = next_n()
        if url.path == "/v1/check":
            reason = q.get("reason", [""])[0]
            mv = int(q["probe_mv"][0])
            pgms = split_pgms(body)
            for i, p in enumerate(pgms):
                with open(os.path.join(args.dir, f"check-{n}-cam{i}.pgm"), "wb") as f:
                    f.write(p)
            wet = "leak" in reason.split("+") or 0 <= mv < 825
            leak = args.leak == "always" or (args.leak == "probe" and wet)
            print(f"check {n}: reason={reason} probe={mv} mV bat={q['bat_mv'][0]} mV "
                  f"rh={q['rh'][0]} t={q['t'][0]} cameras={len(pgms)} -> leak={leak}", flush=True)
            self.reply({"leak": leak})
        elif url.path == "/v1/video":
            cam = q.get("cam", ["0"])[0]
            with open(os.path.join(args.dir, f"video-{n}-cam{cam}.flv"), "wb") as f:
                f.write(body)
            print(f"video {n}: cam{cam} {len(body)} bytes", flush=True)
            self.reply({"bytes": len(body)})
        else:
            self.send_error(404)

    def log_message(self, fmt, *a):
        pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--dir", default="mock_out")
    ap.add_argument("--leak", choices=["probe", "always", "never"], default="probe")
    args = ap.parse_args()
    os.makedirs(args.dir, exist_ok=True)
    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"mock server on port {srv.server_address[1]}, files in {args.dir}", flush=True)
    srv.serve_forever()
