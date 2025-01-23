#!/usr/bin/env bash

set -e -u -o pipefail

# Install RISCOF
pip3 install git+https://github.com/riscv/riscof.git@d38859f85fe407bcacddd2efcd355ada4683aee4

set -x

export PATH=`pwd`/toolchain/bin:$PATH

PARALLEL=-j$(nproc)

make distclean
# Rebuild with all RISC-V extensions
# FIXME: To pass the RISC-V Architecture Tests, full access to 4 GiB is
# necessary. We need to investigate why full 4 GiB memory access is required
# for this purpose, although the emulator can run all selected benchmarks with
# much smaller memory mapping regions.
make ENABLE_EXT_M=1 ENABLE_EXT_A=1 ENABLE_EXT_F=1 ENABLE_EXT_C=1 \
     ENABLE_Zicsr=1 ENABLE_Zifencei=1 ENABLE_FULL4G=1 $PARALLEL
make arch-test RISCV_DEVICE=IMAFCZicsrZifencei $PARALLEL || exit 1
make arch-test RISCV_DEVICE=FCZicsr $PARALLEL || exit 1
make arch-test RISCV_DEVICE=IMZbaZbbZbcZbs $PARALLEL || exit 1

make distclean
make ENABLE_RV32E=1 ENABLE_FULL4G=1 $PARALLEL
make arch-test RISCV_DEVICE=E $PARALLEL || exit 1
