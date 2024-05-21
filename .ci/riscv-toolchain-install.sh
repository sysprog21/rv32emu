#!/usr/bin/env bash

set -e -u -o pipefail

. .ci/common.sh

check_platform

mkdir -p toolchain

# GNU Toolchain for RISC-V
GCC_VER=2024.04.12
TOOLCHAIN_REPO=https://github.com/riscv-collab/riscv-gnu-toolchain/releases

wget -q \
    ${TOOLCHAIN_REPO}/download/${GCC_VER}/riscv32-elf-ubuntu-22.04-gcc-nightly-${GCC_VER}-nightly.tar.gz -O- \
| tar -C toolchain -xz
