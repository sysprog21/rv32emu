#!/usr/bin/env bash
# Build or run the pinned, patched Zephyr comparison board with Twister.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -lt 2 || ! $1 =~ ^(qemu|rv32emu)$ ]]; then
    echo "Usage: $0 {qemu|rv32emu} OUTPUT [Twister options...]" >&2
    exit 1
fi
engine=$1
output=$2
shift 2
: "${ZEPHYR_BASE:?Set ZEPHYR_BASE to the pinned comparison checkout}"
: "${ZEPHYR_MODULES:?Set ZEPHYR_MODULES to the semicolon-separated module paths}"
: "${CROSS_COMPILE:?Set CROSS_COMPILE to the RISC-V GCC prefix}"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export ZEPHYR_COMPARE_RUNNER="${SCRIPT_DIR}/zephyr/compare-run.py"
export PATH="${SCRIPT_DIR}/zephyr:${PATH}"
export RV32EMU="${RV32EMU:-$(pwd)/build/rv32emu}"
export OPENSBI="${OPENSBI:-/usr/lib/riscv32-linux-gnu/opensbi/generic/fw_dynamic.bin}"
mkdir -p "${output}"
output="$(cd "${output}" && pwd)"
export ZEPHYR_COMPARE_OUT="${ZEPHYR_COMPARE_OUT:-${output}/guest-runs}"
board_root="${SCRIPT_DIR}/zephyr/compare"
python "${ZEPHYR_BASE}/scripts/twister" \
    -T "${ZEPHYR_BASE}/tests" -T "${ZEPHYR_BASE}/samples" \
    -A "${board_root}/boards" -p rv32_compare --simulation custom \
    --force-toolchain --allow-installed-plugin --enable-slow --report-filtered \
    -O "${output}" -x "BOARD_ROOT=${board_root}" \
    -x "EXTRA_DTC_OVERLAY_FILE=${board_root}/boards/local/rv32_compare/${engine}.overlay" \
    -x "COMPARE_ENGINE=${engine}" -x "ZEPHYR_MODULES=${ZEPHYR_MODULES}" \
    -x "EXTRA_LDFLAGS=-Wl,--no-warn-rwx-segments" "$@"
