#!/usr/bin/env bash

set -e -u -o pipefail

. .ci/common.sh

check_platform

mkdir -p toolchain

if [[ "$#" == "0" ]] || [[ "$1" != "riscv-collab" ]]; then
    GCC_VER=14.2.0-2
    TOOLCHAIN_REPO=https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack
    TOOLCHAIN_URL=${TOOLCHAIN_REPO}/releases/download/v${GCC_VER}/xpack-riscv-none-elf-gcc-${GCC_VER}-linux-x64.tar.gz
else
    UBUNTU_VER=`lsb_release -r | cut -f2`
    GCC_VER=2024.11.22
    TOOLCHAIN_REPO=https://github.com/riscv-collab/riscv-gnu-toolchain
    TOOLCHAIN_URL=${TOOLCHAIN_REPO}/releases/download/${GCC_VER}/riscv32-elf-ubuntu-${UBUNTU_VER}-gcc-nightly-${GCC_VER}-nightly.tar.xz
fi

wget -q ${TOOLCHAIN_URL} -O- | tar -C toolchain --strip-components=1 -xz
