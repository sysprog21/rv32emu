#!/usr/bin/env bash

. .ci/common.sh

check_platform

mkdir -p toolchain

# GNU Toolchain for RISC-V
GCC_VER=2023.01.04
TOOLCHAIN_REPO=https://github.com/riscv-collab/riscv-gnu-toolchain/releases

# Install RISCOF
python3 -m pip install git+https://github.com/riscv/riscof

set -x

wget -q \
    ${TOOLCHAIN_REPO}/download/${GCC_VER}/riscv32-elf-ubuntu-22.04-nightly-${GCC_VER}-nightly.tar.gz -O- \
| tar -C toolchain -xz

export PATH=`pwd`/toolchain/riscv/bin:$PATH

make clean
make arch-test RISCV_DEVICE=I || exit 1
make arch-test RISCV_DEVICE=IM  || exit 1
make arch-test RISCV_DEVICE=IC || exit 1
make arch-test RISCV_DEVICE=IZifencei || exit 1
make arch-test RISCV_DEVICE=IZicsr || exit 1
