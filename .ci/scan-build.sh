#!/usr/bin/env bash

# Run the Clang static analyzer over an interpreter and a JIT build.
#
# --status-bugs makes scan-build exit non-zero on any report, which is what
# turns this into a gate rather than a report nobody reads.
#
# Usage:
#   .ci/scan-build.sh [defconfig...]     (default: defconfig jit_defconfig)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

set -e -u -o pipefail

LLVM_VERSION=$(cat "${SCRIPT_DIR}/llvm-version")

# The analyzer never runs the emulator, so it does not need a real prebuilt
# release; mk/artifact.mk only wants the variable to be non-empty.
export LATEST_RELEASE="${LATEST_RELEASE:-dummy}"

# LLVM 23 folded the valist.* checkers into security.VAList. Ask the
# analyzer which name it knows, since an unknown checker fails every file.
# Capture the list first: grep -q stopping early would let pipefail report
# clang's SIGPIPE as "not found".
CHECKERS=$("clang-${LLVM_VERSION}" -cc1 -analyzer-checker-help 2> /dev/null || true)
if [[ "${CHECKERS}" == *security.VAList* ]]; then
    VALIST_CHECKER=security.VAList
else
    VALIST_CHECKER=valist
fi

DEFCONFIGS=("$@")
if [ "${#DEFCONFIGS[@]}" -eq 0 ]; then
    DEFCONFIGS=(defconfig jit_defconfig)
fi

for defconfig in "${DEFCONFIGS[@]}"; do
    case "${defconfig}" in
        jit_defconfig) jit=1 ;;
        *) jit=0 ;;
    esac

    echo "Running scan-build over ${defconfig}"
    make distclean
    make "${defconfig}"
    # scan-build still exits 0 when the analyzer cannot process a file, which
    # would pass the gate without analyzing anything, so fail on that too.
    log=$(mktemp)
    trap 'rm -f "${log}"' EXIT
    "scan-build-${LLVM_VERSION}" -v -o ~/scan-build \
        --status-bugs \
        --use-cc="clang-${LLVM_VERSION}" \
        --force-analyze-debug-code \
        --show-description \
        -analyzer-config stable-report-filename=true \
        -enable-checker "${VALIST_CHECKER},nullability" \
        make ENABLE_EXT_F=0 ENABLE_SDL=0 "ENABLE_JIT=${jit}" 2>&1 | tee "${log}"
    if grep -q 'The analyzer encountered problems' "${log}"; then
        echo "ERROR: the analyzer failed on some sources; see the failures above" >&2
        exit 1
    fi
    rm -f "${log}"
done
