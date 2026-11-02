#!/usr/bin/env bash

# Build the riscv32 glibc cross toolchain as a Buildroot SDK, for reuse as the
# external toolchain in assets/system/configs/buildroot.config. The tarball
# lands in /tmp/buildroot-toolchain-output/images/.

set -e

OUTPUT_DIR=/tmp/buildroot-toolchain-output

mkdir -p ${OUTPUT_DIR}
cp -f assets/system/configs/buildroot-toolchain.config ${OUTPUT_DIR}/.config
# Otherwise, the error below raises:
#   You seem to have the current working directory in your
#   LD_LIBRARY_PATH environment variable. This doesn't work.
unset LD_LIBRARY_PATH
make -C /tmp/buildroot O=${OUTPUT_DIR} olddefconfig
make -C /tmp/buildroot O=${OUTPUT_DIR} -j$(nproc) sdk
