# LEAKCAM change net (K230D KPU)

One small net, running on the device: it decides whether a change imgdiff found is a change of
the scene or only of the light. Light only: the wake sleeps without Wi-Fi. Anything else goes to
the server, which makes the leak / no-leak decision (`/v1/check`).

Design rationale and the retraining plan: [../NN.txt](../NN.txt).

## The net

- MobileNetV2-0.35 with its ImageNet weights, cut at `block_13_expand_relu`: stride 16, 192
  channels, 98.5 k parameters, **69 MMAC**. No training.
- Input: the reduced frame leakcam_wake already holds for imgdiff (320x240 luma, uint8 NHWC
  1x240x320x1). The net repeats luma to the 3 channels the weights expect and scales it to
  [-1, 1] itself, so nncase preprocessing is only a cast.
- Output: the 15x20x192 feature map, float.
- Decision: the distance between two frames is the maximum over the 300 cells of (1 - cosine)
  of their features. A small puddle changes 1-3 cells, and a pooled global vector would average
  that away. Below `CHANGE_THRESHOLD` (0.44) the change counts as light only.
- Quantisation: int16 PTQ (activations and weights), KLD calibration on 20 frames.
  `leakcam_change.kmodel` is 372 KB. On the check pairs the simulator's distance is within
  0.002 of TensorFlow. uint8 moves it by up to 0.1, which is too much against this threshold.

## On the device

`firmware/k230_capture/change.[ch]`, in `leakcam_wake`, per camera:

```
capture -> imgdiff vs the history view
  same                        -> not changed
  changed -> change net (history view, current frame)
      distance <  0.44        -> not changed (light only)
      distance >= 0.44        -> changed -> Wi-Fi -> server
      net failed (-1)         -> changed (reported)
```

- The reference is the history view, the same one imgdiff uses. It is embedded again at each
  wake, so nothing new is stored. A light-only frame does not enter the history, so a leak that
  grows is still measured against the dry floor.
- The net runs only when imgdiff saw a change: two inferences, then 300 dot products of 192 on
  the CPU. The kmodel is loaded on first use: the newest update the server sent
  (`/sdcard/leakcam/change.0|1`, with per-camera thresholds, `nnstore.h`), else the factory
  `/sdcard/app/leakcam_change.kmodel` with `CHANGE_THRESHOLD`. Update and field data:
  [../NN.txt](../NN.txt) section 3.
- The link uses the SDK's prebuilt nncase 2.11 runtime (`libs/mk/libnncase.mk`), with no
  OpenCV. It adds about 5 MB to the static `leakcam_wake`.
- The host test (`make test`) replaces the net with `test/change_stub.c`.

## Threshold and measured recall

`make_embed.py` sets the threshold to the largest distance of any "same" pair, so no lighting
or AGC pair crosses it. Pairs are built from `~/leakcam` and prepared as `imgdiff_reduce()`
prepares the K230 frame:

| Pair kind | n | max or p50 distance | above 0.44 |
|---|---|---|---|
| light: real dry frame vs itself at gain 0.6-1.5, gamma 0.7-1.4, noise | 24 | max 0.431 | 0 % |
| realdry: real dry captures minutes apart | 5 | max 0.348 | 0 % |
| wet: leak/dissolved vs its dry base | 72 | p50 0.441 | 50 % |
| wet + light change | 72 | p50 0.428 | 42 % |
| small wet, severity ≤ 0.25 | 8 | p50 0.262 | 25 % |
| wipe: a real change | 30 | p50 0.422 | 43 % |

About half of the synthetic wet pairs fall below the threshold and are not reported at that
wake. Two things limit the cost:
- the sensor alarms (probe, humidity) go to the server whatever the net says;
- a vetoed frame leaves the dry reference in place, so a spreading leak crosses the threshold
  on a later wake.

The pairs are Pi-camera photos and nano-banana edits, not LEAKCAM fisheye frames. Rerun
`make_embed.py` on LEAKCAM captures and set the threshold in `change.h` from its output.

## Files

| File | What it does |
|---|---|
| `make_embed.py` | host, TF: builds `data/embed_b1.tflite`, the pairs and their distances, prints the table above and the threshold |
| `compile_embed.py` | x86 container: tflite -> int16 `../k230_capture/leakcam_change.kmodel`, then simulator vs TF on the check pairs |
| `run_x86.sh` | runs a script in amd64 `python:3.11-slim` under `qemu11/.../qemu-x86_64`, with `pyenv/` and `dotnet/`; mounts `firmware/` |

`data/`, `build/`, `pyenv/`, `dotnet/`, `qemu11/` and `keras_home/` are generated and not in git.
The kmodel is in git, because the device build copies it.

## Toolchain

- PyPI has no linux-aarch64 wheel for nncase 2.11.0, so the compiler runs in an x86-64
  container.
- The compiler needs .NET 7. The host's binfmt qemu 8.2.2 crashes starting the .NET runtime.
  Debian's static qemu-user 11.1.1 works: it is unpacked into `qemu11/` and used as the
  container entrypoint.

```bash
cd firmware/k230_nn
# one-time x86 toolchain (about 260 MB)
docker pull --platform linux/amd64 python:3.11-slim
docker run --rm --platform linux/amd64 -v $PWD:/w python:3.11-slim \
  pip install -q --target /w/pyenv nncase==2.11.0 nncase-kpu==2.11.0 'numpy<2'
mkdir -p dotnet && curl -sSL https://builds.dotnet.microsoft.com/dotnet/Runtime/7.0.20/dotnet-runtime-7.0.20-linux-x64.tar.gz | tar xz -C dotnet
curl -sSLO http://deb.debian.org/debian/pool/main/q/qemu/qemu-user_11.1.1+ds-1_arm64.deb
mkdir -p qemu11 && dpkg-deb --fsys-tarfile qemu-user_11.1.1+ds-1_arm64.deb | tar -x -C qemu11 --wildcards '*/qemu-x86_64' && rm qemu-user_*.deb

# the net, its pairs and the threshold (TF 2.21 + OpenCV, about 3 min)
KERAS_HOME=$PWD/keras_home /home/vkorehovs/watermeter/.venv/bin/python make_embed.py
# the kmodel and its check against TF
./run_x86.sh compile_embed.py
```
