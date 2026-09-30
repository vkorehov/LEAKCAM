"""Runs test_rtmp against `ffmpeg -listen 1` as the RTMP server: the publish must be accepted and
the recording must hold H.265 160x120 and Opus mono that decode without errors. Needs FFmpeg 7.1
or later (Enhanced RTMP v2 audio): FFMPEG_BIN=<dir with ffmpeg and ffprobe>, else from PATH.
  test_rtmp.py <test_rtmp> <sample.h265> <out.flv>"""
import json
import os
import re
import socket
import subprocess
import sys
import time

prog, sample, out = sys.argv[1:4]
bindir = os.environ.get("FFMPEG_BIN", "")
ffmpeg, ffprobe = os.path.join(bindir, "ffmpeg"), os.path.join(bindir, "ffprobe")
ver = subprocess.run([ffmpeg, "-version"], capture_output=True, text=True).stdout
m = re.search(r"version n?(\d+)\.(\d+)", ver)
if not m or (int(m.group(1)), int(m.group(2))) < (7, 1):
    print(f"rtmp: FAIL needs FFmpeg 7.1+ for Enhanced RTMP v2 audio, found {ver.splitlines()[0] if ver else 'none'}"
          " (set FFMPEG_BIN)")
    sys.exit(1)
with socket.socket() as s:                          # a free port
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
srv = subprocess.Popen([ffmpeg, "-v", "error", "-y", "-listen", "1", "-i",
                        f"rtmp://127.0.0.1:{port}/leakcam/cam0", "-c", "copy", out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
time.sleep(1.5)                                     # ffmpeg listening (a probe connect would end it)
pub = subprocess.run([prog, sample, str(port)], capture_output=True, text=True)
err = srv.communicate(timeout=20)[1]

probe = json.loads(subprocess.run([ffprobe, "-v", "error", "-show_streams", "-of", "json", out],
                                  capture_output=True, text=True).stdout or '{"streams":[]}')
kinds = {s["codec_type"]: s for s in probe["streams"]}
v, a = kinds.get("video"), kinds.get("audio")
ok = (pub.returncode == 0 and v and v["codec_name"] == "hevc" and v["width"] == 160 and
      a and a["codec_name"] == "opus" and a["channels"] == 1)
dec = subprocess.run([ffmpeg, "-v", "error", "-i", out, "-f", "null", "-"], capture_output=True, text=True)
# ffmpeg's listener reports every publisher's end as an I/O error (its own client's too)
err = "\n".join(l for l in err.splitlines() if "Input/output error" not in l)
if not ok or err.strip() or dec.returncode or dec.stderr.strip():
    print(f"rtmp: FAIL publisher={pub.stdout.strip()} {pub.stderr.strip()} server={err.strip()!r} "
          f"streams={probe['streams']} decode={dec.stderr.strip()!r}")
    sys.exit(1)
print("rtmp: Enhanced RTMP v2 publish accepted by ffmpeg's RTMP server, H.265 + Opus received and "
      "decoded")
