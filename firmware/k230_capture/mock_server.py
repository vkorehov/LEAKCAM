#!/usr/bin/env python3
"""Mock LEAKCAM server: what leakcam_wake and leakcam_stream talk to until the real one exists.

  mock_server.py [--port 8000] [--dir mock_out] [--leak probe|always|never]
                 [--nn <kmodel> --thr <cam0>,<cam1>]

POST /v1/check?reason=<r>&probe_mv=<mV>&bat_mv=<mV>&rh=<%RH>&t=<C>&nn=<CRC>&thr=<t0>,<t1>
               &cam<i>=<first|same|changed|light>,<distance>[&sample=1]
                            body: binary PGMs back to back, per camera its frame, then its
                            history view where the state is changed or light. Saved as
                            check-<n>-cam<i>.pgm, check-<n>-cam<i>-view.pgm and the query as
                            check-<n>.json: the pairs a teacher labels for the next change net
                            (firmware/NN.txt). Answers {"leak":true|false}: with --leak probe
                            (default) a leak is what the BL616's probes say: the reason "leak",
                            or the probe node below 825 mV (wet). With --nn, the answer adds
                            "nn":"<CRC>","thr":[t0,t1] when the board's net or thresholds differ.
GET /v1/nn/<CRC>            the --nn kmodel (CRC-32, 8 hex digits); each download leaves a file
                            nn-get-<n>.
The leak video is not sent here: leakcam_stream publishes it live over RTMP to port 1935 of the
same host, where the real server runs MediaMTX (for a bench: `ffmpeg -listen 1 -i
rtmp://0.0.0.0:1935/leakcam/cam0`, FFmpeg 7.1+).

On the device, /sdcard/leakcam/server holds "<host> <port>" of this server.
"""
import argparse
import json
import os
import re
import threading
import zlib
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

    def do_GET(self):
        m = re.fullmatch(r"/v1/nn/([0-9a-f]{8})", urlparse(self.path).path)
        if not (m and nn_model and m.group(1) == nn_crc):
            self.send_error(404)
            return
        n = next_n()
        open(os.path.join(args.dir, f"nn-get-{n}"), "w").close()
        print(f"nn {n}: kmodel {nn_crc} sent, {len(nn_model)} bytes", flush=True)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(nn_model)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(nn_model)

    def read_body(self):
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
            cams, k = [], 0
            while f"cam{len(cams)}" in q:
                i = len(cams)
                state, dist = q[f"cam{i}"][0].split(",")
                names = [f"check-{n}-cam{i}.pgm"] + ([f"check-{n}-cam{i}-view.pgm"] if state in ("changed", "light") else [])
                for name in names:
                    with open(os.path.join(args.dir, name), "wb") as f:
                        f.write(pgms[k])
                    k += 1
                cams.append(f"{state}/{dist}")
            with open(os.path.join(args.dir, f"check-{n}.json"), "w") as f:
                json.dump({key: v[0] for key, v in q.items()}, f)
            wet = "leak" in reason.split("+") or 0 <= mv < 825
            leak = args.leak == "always" or (args.leak == "probe" and wet)
            print(f"check {n}: reason={reason} probe={mv} mV bat={q['bat_mv'][0]} mV "
                  f"rh={q['rh'][0]} t={q['t'][0]} cameras={' '.join(cams)} nn={q['nn'][0]} "
                  f"{'sample ' if 'sample' in q else ''}-> leak={leak}", flush=True)
            answer = {"leak": leak}
            if nn_model and (q["nn"][0] != nn_crc or q["thr"][0] != args.thr):
                answer.update(nn=nn_crc, thr=[float(t) for t in args.thr.split(",")])
            self.reply(answer)
        else:
            self.send_error(404)

    def log_message(self, fmt, *a):
        pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--dir", default="mock_out")
    ap.add_argument("--leak", choices=["probe", "always", "never"], default="probe")
    ap.add_argument("--nn", help="change net kmodel to offer to the boards")
    ap.add_argument("--thr", default="0.44,0.44", help="its thresholds, camera 0 and 1, two decimals")
    args = ap.parse_args()
    nn_model = open(args.nn, "rb").read() if args.nn else None
    nn_crc = f"{zlib.crc32(nn_model):08x}" if nn_model else None
    os.makedirs(args.dir, exist_ok=True)
    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"mock server on port {srv.server_address[1]}, files in {args.dir}", flush=True)
    srv.serve_forever()
