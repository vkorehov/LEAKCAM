#!/bin/sh
# Run a python script from this directory in the amd64 python:3.11-slim image, under
# qemu-user 11.1 (the host's binfmt qemu 8.2.2 crashes starting the .NET 7 runtime nncase needs).
# Usage: ./run_x86.sh compile_kmodel.py --variant f32
HERE=$(cd "$(dirname "$0")" && pwd)
exec docker run --rm --platform linux/amd64 -v "$HERE":/w -w /w \
  -e PYTHONPATH=/w/pyenv -e DOTNET_ROOT=/w/dotnet \
  -e DOTNET_EnableWriteXorExecute=0 -e DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 \
  -e NNCASE_PLUGIN_PATH=/w/pyenv/nncase/modules -e PATH=/w/pyenv:/usr/local/bin:/usr/bin:/bin -e K230_SCMODEL_PATH=/w/pyenv/nncase.simulator.k230.sc \
  --entrypoint /w/qemu11/usr/bin/qemu-x86_64 python:3.11-slim /usr/local/bin/python3 -u "$@"
