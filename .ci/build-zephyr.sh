#!/usr/bin/env bash

set -e -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"
check_platform

: "${ZEPHYR_BASE:?Set ZEPHYR_BASE to the Zephyr checkout}"
: "${PICOLIBC_DIR:?Set PICOLIBC_DIR to the picolibc checkout}"
: "${CROSS_COMPILE:?Set CROSS_COMPILE to the guest compiler prefix}"
ZEPHYR_BASE="$(cd "${ZEPHYR_BASE}" && pwd)"
PICOLIBC_DIR="$(cd "${PICOLIBC_DIR}" && pwd)"
export ZEPHYR_BASE
ZEPHYR_TLS="${ZEPHYR_TLS:-n}"
case "${ZEPHYR_TLS}" in
    y | n) ;;
    *)
        print_error "ZEPHYR_TLS must be y or n"
        exit 1
        ;;
esac

ZEPHYR_OUT="${ZEPHYR_OUT:-build/zephyr}"
mkdir -p "${ZEPHYR_OUT}"
ZEPHYR_OUT="$(cd "${ZEPHYR_OUT}" && pwd)"
CONF_FILES="${SCRIPT_DIR}/zephyr/rv32emu.conf"
if [ "${ZEPHYR_TLS}" = n ]; then
    CONF_FILES+=";${SCRIPT_DIR}/zephyr/no-tls.conf"
fi

"${CROSS_COMPILE}gcc" --version
git -C "${ZEPHYR_BASE}" rev-parse HEAD
git -C "${PICOLIBC_DIR}" rev-parse HEAD
for sample in hello_world synchronization; do
    sample_dir="${ZEPHYR_OUT}/${sample}"
    cmake -GNinja -S "${ZEPHYR_BASE}/samples/${sample}" -B "${sample_dir}" \
        -DBOARD=qemu_riscv32 -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
        -DCROSS_COMPILE="${CROSS_COMPILE}" -DZEPHYR_MODULES="${PICOLIBC_DIR}" \
        -DDTC_OVERLAY_FILE="${SCRIPT_DIR}/zephyr/rv32emu.overlay" \
        -DEXTRA_CONF_FILE="${CONF_FILES}" -DUSE_CCACHE=0
    cmake --build "${sample_dir}" --parallel
    config="${sample_dir}/zephyr/.config"
    for setting in RISCV_S_MODE RISCV_S_MODE_EXTERNAL_SBI RISCV_SUPERVISOR_TIMER PICOLIBC; do
        grep -qx "CONFIG_${setting}=y" "${config}"
    done
    grep -qx 'CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=65000000' "${config}"
    if [ "${ZEPHYR_TLS}" = n ]; then
        grep -qx '# CONFIG_THREAD_LOCAL_STORAGE is not set' "${config}"
    else
        grep -qx 'CONFIG_THREAD_LOCAL_STORAGE=y' "${config}"
    fi
    if "${CROSS_COMPILE}nm" "${sample_dir}/zephyr/zephyr.elf" | grep '__emutls'; then
        print_error "Emulated TLS is incompatible with this Zephyr guest"
        exit 1
    fi
    "${CROSS_COMPILE}objcopy" -O binary "${sample_dir}/zephyr/zephyr.elf" \
        "${ZEPHYR_OUT}/${sample}.bin"
done
