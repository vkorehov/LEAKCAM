#!/bin/sh
# Run a python script from this directory in the amd64 python:3.11-slim image, under
# qemu-user 11.1 (the host's binfmt qemu 8.2.2 crashes starting the .NET 7 runtime nncase needs).
# firmware/ is mounted, so the script can write the kmodel into ../k230_capture.
# Usage: ./run_x86.sh compile_embed.py
HERE=$(cd "$(dirname "$0")" && pwd)
exec docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE/..":/fw -w /fw/k230_nn \
  -e PYTHONPATH=/fw/k230_nn/pyenv -e DOTNET_ROOT=/fw/k230_nn/dotnet \
  -e DOTNET_EnableWriteXorExecute=0 -e DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 \
  -e NNCASE_PLUGIN_PATH=/fw/k230_nn/pyenv/nncase/modules -e PATH=/fw/k230_nn/pyenv:/usr/local/bin:/usr/bin:/bin -e K230_SCMODEL_PATH=/fw/k230_nn/pyenv/nncase.simulator.k230.sc \
  --entrypoint /fw/k230_nn/qemu11/usr/bin/qemu-x86_64 python:3.11-slim /usr/local/bin/python3 -u "$@"
