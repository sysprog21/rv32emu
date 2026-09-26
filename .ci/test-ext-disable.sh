#!/usr/bin/env bash

# Build and run "make check" with one optional feature turned off at a time, in
# both interpreter and JIT mode.
#
# Every feature here is independently switchable, so a CI that only ever builds
# the default configuration hides compile errors behind whichever #ifdef nobody
# flipped. Walking the whole matrix is what catches those.
#
# Usage:
#   .ci/test-ext-disable.sh [GROUP...]
#
# GROUP is one of the names below; with no argument every group runs. Naming
# groups lets the CI matrix spread the run over several runners.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

set -e -u -o pipefail

CORE_FEATURES=(
    ENABLE_EXT_M ENABLE_EXT_A ENABLE_EXT_F ENABLE_EXT_C
    ENABLE_Zicsr ENABLE_Zifencei
)

# Bit manipulation plus the two performance features, which share this group
# only because the split is about balancing runners, not about taxonomy.
BITMANIP_PERF_FEATURES=(
    ENABLE_Zba ENABLE_Zbb ENABLE_Zbc ENABLE_Zbs
    ENABLE_MOP_FUSION ENABLE_BLOCK_CHAINING
)

# Not "GROUPS": bash reserves that name for the caller's group IDs.
FEATURE_GROUPS=("$@")
if [ "${#FEATURE_GROUPS[@]}" -eq 0 ]; then
    FEATURE_GROUPS=(core bitmanip-perf)
fi

FEATURES=()
for group in "${FEATURE_GROUPS[@]}"; do
    case "${group}" in
        core) FEATURES+=("${CORE_FEATURES[@]}") ;;
        bitmanip-perf) FEATURES+=("${BITMANIP_PERF_FEATURES[@]}") ;;
        *)
            print_error "Unknown feature group: ${group} (expected: core, bitmanip-perf)"
            exit 2
            ;;
    esac
done

# cleanconfig rather than distclean: the prebuilt artifacts under build/ are
# expensive to re-fetch and a configuration change does not invalidate them.
for feature in "${FEATURES[@]}"; do
    for defconfig in interpreter_defconfig jit_defconfig; do
        echo "Testing ${feature}=0 (${defconfig})"
        if ! (make cleanconfig && make "${defconfig}" && make "${feature}"=0 check ${PARALLEL}); then
            print_error "${defconfig} check failed with ${feature}=0"
            exit 1
        fi
    done
done
