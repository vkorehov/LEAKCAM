#!/bin/sh
# Cross-compile leak_nn.elf (static, RT-Smart userspace) in a throw-away container from the SDK
# build image. Read-only mounts of the SDK and the LEAKCAM firmware; never runs make in the SDK.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SDK=${SDK:-$HOME/k230_rtos_sdk}
FW=${FW:-$HOME/k230d-hw/LEAKCAM/firmware/k230_capture}
TC=${TC:-$HOME/.kendryte/k230_toolchains}
exec docker run --rm --name leak-nn-build --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$SDK:$SDK:ro" -v "$FW:$FW:ro" -v "$TC:$TC:ro" -v "$HERE:$HERE" -w "$HERE" \
  k230-rtos-sdk-build:arm64-x86tc sh -c "
set -e
R=$SDK/src/rtsmart
CXX=$TC/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-g++
CC=$TC/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-gcc
F='-O2 -march=rv64imafdcv -mabi=lp64d -mcmodel=medany'
\$CC \$F -I$FW -c $FW/imgdiff.c -o imgdiff.o
\$CC \$F -I$FW -c $FW/imgqual.c -o imgqual.o
\$CXX \$F -std=c++20 -fopenmp -I$FW -I. \
  -I\$R/libs/opencv/include/opencv4 -I\$R/libs/nncase/riscv64 -I\$R/libs/nncase/riscv64/nncase/include \
  -I\$R/libs/nncase/riscv64/nncase/include/nncase/runtime -I\$R/libs/nncase/riscv64/rvvlib/include \
  -c leak_nn.cc -o leak_nn.o
\$CXX \$F -fopenmp -T $SDK/src/rtsmart/examples/ai/usage_kpu/cmake/link.lds --static -o leak_nn.elf leak_nn.o imgqual.o imgdiff.o \
  -L\$R/libs/nncase/riscv64/nncase/lib -L\$R/libs/nncase/riscv64/rvvlib -L\$R/mpp/userapps/lib \
  -L\$R/libs/opencv/lib -L\$R/libs/opencv/lib/opencv4/3rdparty \
  -Wl,--start-group -lrvv -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 -lsys -latomic -Wl,--end-group \
  -Wl,--start-group -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -llibjpeg-turbo -llibopenjp2 -llibpng -llibtiff -llibwebp -lzlib -lcsi_cv -Wl,--end-group
ls -la leak_nn.elf
"
