"""ffprobe/ffmpeg check of the FLV test_flv wrote: H.264 160x120 plus G.711 mu-law 8 kHz mono,
both decoding without errors."""
import json
import subprocess
import sys

flv = sys.argv[1]
probe = json.loads(subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-of", "json", flv],
                                  capture_output=True, text=True, check=True).stdout)
kinds = {s["codec_type"]: s for s in probe["streams"]}
v, a = kinds.get("video"), kinds.get("audio")
ok = (v and v["codec_name"] == "h264" and v["width"] == 160 and v["height"] == 120 and
      a and a["codec_name"] == "pcm_mulaw" and a["sample_rate"] == "8000" and a["channels"] == 1)
dec = subprocess.run(["ffmpeg", "-v", "error", "-i", flv, "-f", "null", "-"], capture_output=True, text=True)
frames = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
                         "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", flv],
                        capture_output=True, text=True).stdout.strip()
if not ok or dec.returncode or dec.stderr.strip() or frames != "3":
    print(f"flv: FAIL streams={probe['streams']} decode={dec.stderr.strip()!r} frames={frames}")
    sys.exit(1)
print("flv: mu-law reference values, H.264 + G.711 8 kHz in one FLV, ffprobe/ffmpeg read and decode it")
