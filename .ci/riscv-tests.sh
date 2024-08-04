#!/usr/bin/env bash

set -e -u -o pipefail

# Install RISCOF
pip3 install git+https://github.com/riscv/riscof.git@d38859f85fe407bcacddd2efcd355ada4683aee4

set -x

export PATH=`pwd`/toolchain/riscv/bin:$PATH

make clean
make arch-test RISCV_DEVICE=IMAFCZicsrZifencei || exit 1
make arch-test RISCV_DEVICE=FCZicsr || exit 1
