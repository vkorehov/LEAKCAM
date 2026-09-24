#!/usr/bin/env bash
# Run a k230_rtos_sdk make target inside the build container
# (native arm64 Debian with amd64 multiarch libs; the SDK's x86-64-only
# RISC-V toolchains and prebuilt host tools run via the host qemu binfmt).
# See leakcam_docker/Dockerfile for why not a plain linux/amd64 container.
#
# Usage:
#   ./leakcam_docker_build.sh                          # make log (full build)
#   ./leakcam_docker_build.sh dl_toolchain             # one-time toolchain install
#   ./leakcam_docker_build.sh k230d_rtos_evb_defconfig # select board config
#   ./leakcam_docker_build.sh log                      # build, tee to log.txt
#   ./leakcam_docker_build.sh --shell                  # interactive shell
#   ./leakcam_docker_build.sh --rebuild-image          # rebuild the image first
#   JOBS=5 ./leakcam_docker_build.sh rtsmart           # JOBS -> make -j and NCPUS (scons -j)
#
# The SDK and the toolchain dir are bind-mounted at their host paths, and the
# container runs as the host user, so output files are owned by you.
set -euo pipefail

SDK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${IMAGE:-k230-rtos-sdk-build:arm64-x86tc}"
NAME="${NAME:-k230-rtos-sdk-build}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/.kendryte/k230_toolchains}"
JOBS="${JOBS:-5}"
MAKE_J="${MAKE_J:-$JOBS}"   # MAKE_J=1 for a fully serial top-level make

if [[ "${1:-}" == "--rebuild-image" ]]; then
    shift
    docker build -t "$IMAGE" "$SDK_DIR/leakcam_docker"
fi
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" "$SDK_DIR/leakcam_docker"
fi

mkdir -p "$TOOLCHAIN_DIR"
docker rm -f "$NAME" >/dev/null 2>&1 || true

TTY=()
[[ -t 0 && -t 1 ]] && TTY=(-it)

if [[ "${1:-}" == "--shell" ]]; then
    CMD=(bash)
else
    [[ $# -eq 0 ]] && set -- log
    # Top-level -j$MAKE_J reaches the SDK's serial sub-makes (libs, mpp,
    # examples) through the jobserver; without it only scons (NCPUS) is
    # parallel. A clean k230d_rtos_bpi_zero build with -j5 passed (2026-09-24).
    CMD=(make ${MAKE_J:+-j"$MAKE_J"} "$@")
    # The SDK's own `make log` pipes through tee and always exits 0; do the
    # same logging here but keep make's exit status.
    if [[ "$*" == "log" ]]; then
        CMD=(bash -o pipefail -c "make ${MAKE_J:+-j$MAKE_J} 2>&1 | tee log.txt")
    fi
fi

exec docker run --rm "${TTY[@]}" --name "$NAME" \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e SDK_TOOLCHAIN_DIR="$TOOLCHAIN_DIR" \
    -e NCPUS="$JOBS" \
    -v "$SDK_DIR:$SDK_DIR" \
    -v "$TOOLCHAIN_DIR:$TOOLCHAIN_DIR" \
    -w "$SDK_DIR" \
    "$IMAGE" "${CMD[@]}"
