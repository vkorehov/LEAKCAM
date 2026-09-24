# Build image for k230_rtos_sdk on an aarch64 host.
#
# Derived from tools/docker/Dockerfile, but NATIVE arm64 with amd64 multiarch
# runtime libs: make/python/scons/bison/host gcc run natively; the x86-64-only
# RISC-V toolchains and the SDK's prebuilt x86-64 host tools (mkimage,
# k230_priv_gzip, mkdosfs, gen_isp_config_bin, ...) run through the host's
# qemu-x86_64 binfmt handler (registered with the F flag, so it works inside
# the container without qemu being installed here).
#
# Why not a plain linux/amd64 container: under qemu-user 8.2.2 on this host
#  - ubuntu:22.04 `apt-get update` fails ("keyring ... unsupported filetype")
#  - m4 1.4.19 (hence bison) dies with "QEMU internal SIGSEGV", so the SDK's
#    kconfig tool cannot even be built.
#
# Toolchains are NOT baked in: they live on the host in ~/.kendryte/k230_toolchains
# (bind-mounted), installed once via `make dl_toolchain`.
FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture amd64 && apt-get update && \
    apt-get install -y --no-install-recommends \
    make autoconf automake bison flex gcc g++ gawk libncurses5-dev pkg-config \
    libconfuse-dev libssl-dev python3 python3-pip python-is-python3 cmake \
    libyaml-dev scons mtools bzip2 curl git openssh-client rsync dosfstools \
    ca-certificates wget xz-utils file bc cpio unzip zip patch \
    libc6:amd64 libstdc++6:amd64 libgcc-s1:amd64 zlib1g:amd64 libssl3:amd64 libudev1:amd64 \
    && apt-get clean && update-ca-certificates && rm -rf /var/lib/apt/lists/*
RUN pip3 install --no-cache-dir --break-system-packages pycryptodome gmssl scons==3.1.2 jsonschema jinja2
RUN git config --system --add safe.directory '*'
# tools/gen_image.sh calls `repo info`; the launcher defers to the tree's .repo/repo.
RUN curl -fsSL https://raw.githubusercontent.com/canmv-k230/git-repo/main/repo -o /usr/local/bin/repo && \
    chmod a+rx /usr/local/bin/repo
