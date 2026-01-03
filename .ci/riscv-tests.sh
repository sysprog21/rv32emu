#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

set -e -u -o pipefail

# Install RISCOF
# Note: riscv-config 3.18.3 pins pyyaml==5.2, but PyYAML 5.x fails to build on
# Python 3.12 (Cython compatibility). Since requirements.txt is a full freeze
# of all transitive dependencies, use --no-deps to bypass resolver conflicts.
# riscof also depends on riscv-config, so --no-deps prevents re-triggering.
pip3 install --no-deps riscv-config==3.18.3
pip3 install --no-deps -r .ci/requirements.txt
# Smoke test: verify riscv-config works with PyYAML 6.x
python3 -c "import riscv_config; print('riscv-config import OK')"

# Workaround for RISCOF bug: dbgen.py line 158 uses 'list' (Python built-in)
# instead of 'flist' (file list variable). This causes TypeError when the
# database deletion code path is triggered. Fix by patching the installed file.
# See: https://github.com/riscv/riscof - dbgen.py generate() function
DBGEN_PATH=$(python3 -c "import riscof; import os; print(os.path.join(os.path.dirname(riscof.__file__), 'dbgen.py'))")
if grep -q 'if key not in list:' "$DBGEN_PATH" 2> /dev/null; then
    echo "Patching RISCOF dbgen.py: fixing 'list' -> 'flist' bug"
    # Use portable sed in-place: create temp file, then move (works on both Linux and macOS)
    sed 's/if key not in list:/if key not in flist:/' "$DBGEN_PATH" > "${DBGEN_PATH}.tmp" \
        && mv "${DBGEN_PATH}.tmp" "$DBGEN_PATH"
fi

set -x

export PATH=$(pwd)/toolchain/bin:$PATH

make distclean
# Rebuild with all RISC-V extensions (including B-extension: Zba/Zbb/Zbc/Zbs)
make ENABLE_ARCH_TEST=1 ENABLE_EXT_M=1 ENABLE_EXT_A=1 ENABLE_EXT_F=1 ENABLE_EXT_C=1 \
    ENABLE_Zicsr=1 ENABLE_Zifencei=1 \
    ENABLE_Zba=1 ENABLE_Zbb=1 ENABLE_Zbc=1 ENABLE_Zbs=1 $PARALLEL

# Pre-fetch shared resources before parallel execution to avoid race conditions
make ENABLE_ARCH_TEST=1 artifact
make riscof-check
git submodule update --init tests/riscv-arch-test/

# Determine host platform for sail binary (matches mk/common.mk logic)
case "$(uname -m)" in
    x86_64) HOST_PLATFORM=x86 ;;
    aarch64) HOST_PLATFORM=aarch64 ;;
    arm64) HOST_PLATFORM=arm64 ;;
    *)
        print_error "Unsupported platform: $(uname -m)"
        exit 1
        ;;
esac
cp "build/rv32emu-prebuilt-sail-${HOST_PLATFORM}" tests/arch-test-target/sail_cSim/riscv_sim_RV32
chmod +x tests/arch-test-target/sail_cSim/riscv_sim_RV32

# Run architecture tests in parallel (each uses device-specific work directory and config)
arch_test_pids=()
arch_test_devices=("IMAFCZicsrZifencei" "FCZicsr" "IMZbaZbbZbcZbs")

for device in "${arch_test_devices[@]}"; do
    make ENABLE_ARCH_TEST=1 arch-test RISCV_DEVICE="$device" hw_data_misaligned_support=1 SKIP_PREREQ=1 $PARALLEL &
    arch_test_pids+=($!)
done

# Wait for all parallel arch-tests and check results
arch_test_failed=0
for i in "${!arch_test_pids[@]}"; do
    if ! wait "${arch_test_pids[$i]}"; then
        print_error "arch-test failed for device: ${arch_test_devices[$i]}"
        arch_test_failed=1
    fi
done

if [ "$arch_test_failed" -ne 0 ]; then
    exit 1
fi

# Rebuild with RV32E
make distclean
make ENABLE_ARCH_TEST=1 ENABLE_RV32E=1 $PARALLEL
make ENABLE_ARCH_TEST=1 arch-test RISCV_DEVICE=E SKIP_PREREQ=1 $PARALLEL || exit 1

# Rebuild with JIT
# Do not run the architecture test with "Zicsr" extension. It ignores
# the hardware misalignment (hw_data_misaligned_support) option.
make distclean
make ENABLE_ARCH_TEST=1 ENABLE_JIT=1 ENABLE_T2C=0 \
    ENABLE_EXT_M=1 ENABLE_EXT_A=1 ENABLE_EXT_F=1 ENABLE_EXT_C=1 \
    ENABLE_Zicsr=1 ENABLE_Zifencei=1 $PARALLEL
make ENABLE_ARCH_TEST=1 arch-test RISCV_DEVICE=IMC hw_data_misaligned_support=0 SKIP_PREREQ=1 $PARALLEL || exit 1
