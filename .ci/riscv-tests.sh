#!/usr/bin/env bash

. .ci/common.sh

check_platform

mkdir -p toolchain

# GNU Toolchain for RISC-V
GCC_VER=12.2.0-1
XPACK_REPO=https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download

set -x

wget -q \
    ${XPACK_REPO}/v${GCC_VER}/xpack-riscv-none-elf-gcc-${GCC_VER}-linux-x64.tar.gz -O- \
| tar -C toolchain -xz

export PATH=`pwd`/toolchain/xpack-riscv-none-elf-gcc-${GCC_VER}/bin:$PATH

make clean
make arch-test RISCV_DEVICE=I || exit 1
make arch-test RISCV_DEVICE=M  || exit 1
make arch-test RISCV_DEVICE=C || exit 1
make arch-test RISCV_DEVICE=Zifencei || exit 1
make arch-test RISCV_DEVICE=privilege || exit 1
