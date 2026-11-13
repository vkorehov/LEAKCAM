#!/usr/bin/env python3
"""A board's video upload, mocked from a saved recording: the H.265 (Annex-B) split into access units and the Opus
packets (Ogg), sent as the K230 sends them (k230_app/media.c): one chunked POST /v1/media/<id>, frames of
[u32 BE length (| 1<<31 for audio)][u64 BE us][payload], 15 pictures a second, a 40 ms packet each 40 ms.
    mock_board_video.py <server> <id> <video.h265> <audio.opus> [fps]
The inputs from a stored MP4 (tests/test148): ffmpeg -i x.mp4 -map 0:v -c copy -bsf:v hevc_mp4toannexb -f hevc v.h265;
ffmpeg -i x.mp4 -map 0:a -c copy a.opus. Used 2026-11-13 with a local server (adb reverse tcp:8090 tcp:8090: the
phone reaches it over USB, no Wi-Fi) to check the phone plays the live HLS and the MP4 (H.265 2592x1936 + Opus).
"""
import http.client
import struct
import sys
import time
import urllib.parse

server, mid, vpath, apath = sys.argv[1:5]
fps = float(sys.argv[5]) if len(sys.argv) > 5 else 15.0


def access_units(raw):
    nalus = [p.rstrip(b"\0") for p in raw.split(b"\0\0\1") if p.rstrip(b"\0")]
    aus, au, vcl_in = [], [], False
    for n in nalus:
        t = (n[0] >> 1) & 0x3f
        vcl = t < 32
        if (vcl and n[2] & 0x80 and vcl_in) or (not vcl and vcl_in):
            aus.append(au)
            au, vcl_in = [], False
        au.append(n)
        vcl_in = vcl_in or vcl
    if au:
        aus.append(au)
    return [b"".join(b"\0\0\0\1" + n for n in a) for a in aus]


def opus_packets(raw):
    pkts, cur, i = [], b"", 0
    while i + 27 <= len(raw) and raw[i:i + 4] == b"OggS":
        nseg = raw[i + 26]
        lace = raw[i + 27:i + 27 + nseg]
        p = i + 27 + nseg
        for l in lace:
            cur += raw[p:p + l]
            p += l
            if l < 255:
                pkts.append(cur)
                cur = b""
        i = p
    return pkts[2:]                                   # OpusHead, OpusTags


aus = access_units(open(vpath, "rb").read())
pkts = opus_packets(open(apath, "rb").read())
t0 = 1_000_000_000
frames = [(t0 + int(k * 1e6 / fps), False, a) for k, a in enumerate(aus)]
frames += [(t0 + 120_000 + k * 40_000, True, p) for k, p in enumerate(pkts)]
frames.sort(key=lambda f: f[0])
u = urllib.parse.urlparse(server)
c = http.client.HTTPConnection(u.hostname, u.port, timeout=30)
c.putrequest("POST", f"/v1/media/{mid}")
c.putheader("Content-Type", "video/x-leakcam-h265")
c.putheader("X-Fps", str(int(fps)))
c.putheader("X-Audio", "opus/16000/1")
c.putheader("X-Taken", str(int(time.time() * 1000)))
c.putheader("Transfer-Encoding", "chunked")
c.endheaders()
start = time.time()
for us, audio, data in frames:
    wait = start + (us - t0) / 1e6 - time.time()
    if wait > 0:
        time.sleep(wait)                              # live: as the cameras make them
    body = struct.pack(">IQ", len(data) | (0x80000000 if audio else 0), us) + data
    c.send(b"%x\r\n" % len(body) + body + b"\r\n")
c.send(b"0\r\n\r\n")
r = c.getresponse()
print(r.status, r.read().decode().strip(), f"{len(aus)} pictures, {len(pkts)} audio packets in {time.time() - start:.1f} s")
