#!/usr/bin/env bash

set -e -u -o pipefail

# Install RISCOF
pip3 install -r requirements.txt

set -x

export PATH=`pwd`/toolchain/bin:$PATH

PARALLEL=-j$(nproc)

make distclean
# Rebuild with all RISC-V extensions
# FIXME: To pass the RISC-V Architecture Tests, full access to 4 GiB is
# necessary. We need to investigate why full 4 GiB memory access is required
# for this purpose, although the emulator can run all selected benchmarks with
# much smaller memory mapping regions.
make ENABLE_ARCH_TEST=1 ENABLE_EXT_M=1 ENABLE_EXT_A=1 ENABLE_EXT_F=1 ENABLE_EXT_C=1 \
     ENABLE_Zicsr=1 ENABLE_Zifencei=1 ENABLE_FULL4G=1 $PARALLEL
make arch-test RISCV_DEVICE=IMAFCZicsrZifencei hw_data_misaligned_support=1 $PARALLEL || exit 1
make arch-test RISCV_DEVICE=FCZicsr hw_data_misaligned_support=1 $PARALLEL || exit 1
make arch-test RISCV_DEVICE=IMZbaZbbZbcZbs hw_data_misaligned_support=1 $PARALLEL || exit 1

# Rebuild with RV32E
make distclean
make ENABLE_ARCH_TEST=1 ENABLE_RV32E=1 ENABLE_FULL4G=1 $PARALLEL
make arch-test RISCV_DEVICE=E $PARALLEL || exit 1

# Rebuild with JIT
# Do not run the architecture test with "Zicsr" extension. It ignores
# the hardware misalignment (hw_data_misaligned_support) option.
make distclean
make ENABLE_ARCH_TEST=1 ENABLE_JIT=1 ENABLE_T2C=0 \
     ENABLE_EXT_M=1 ENABLE_EXT_A=1 ENABLE_EXT_F=1 ENABLE_EXT_C=1 \
     ENABLE_Zicsr=1 ENABLE_Zifencei=1 ENABLE_FULL4G=1 $PARALLEL
make arch-test RISCV_DEVICE=IMC hw_data_misaligned_support=0 $PARALLEL || exit 1
