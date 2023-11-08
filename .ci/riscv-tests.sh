#!/usr/bin/env bash

set -e -u -o pipefail

# Install RISCOF
python3 -m pip install git+https://github.com/riscv/riscof

set -x

export PATH=`pwd`/toolchain/riscv/bin:$PATH

make clean
make arch-test RISCV_DEVICE=I || exit 1
make arch-test RISCV_DEVICE=IM  || exit 1
make arch-test RISCV_DEVICE=IC || exit 1
make arch-test RISCV_DEVICE=IZifencei || exit 1
make arch-test RISCV_DEVICE=IZicsr || exit 1
