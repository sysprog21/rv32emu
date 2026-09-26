#!/usr/bin/env bash

# Boot the Linux guest with the virtio-blk backing devices attached.
#
# boot-linux-prepare.sh must run as root: it creates the disk images and
# attaches them to a loop (Linux) or disk (Darwin) device, reporting the
# resulting device names back through TMP_FILE. Teardown detaches those devices,
# so it has to run even when the boot itself fails, otherwise the runner leaks
# loop devices into the next step.

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Root writes TMP_FILE, so keep it in a private directory: with
# fs.protected_regular, root may not write a user's file in sticky /tmp.
TMP_DIR=$(mktemp -d "${RUNNER_TEMP:-/tmp}/rv32emu-boot.XXXXXX")
TMP_FILE="${TMP_DIR}/devices"

cleanup()
{
    # Setup records each device in TMP_FILE as it attaches it, so a setup that
    # died partway has already leaked one. Re-read the file here rather than
    # trusting the environment, which only carries what was sourced below.
    if [ -s "${TMP_FILE}" ]; then
        . "${TMP_FILE}"
    fi

    sudo env TMP_FILE="${TMP_FILE}" \
        BLK_DEV_EXT4="${BLK_DEV_EXT4:-}" \
        BLK_DEV_SIMPLEFS="${BLK_DEV_SIMPLEFS:-}" \
        "${SCRIPT_DIR}/boot-linux-prepare.sh" cleanup
    sudo rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

sudo env TMP_FILE="${TMP_FILE}" "${SCRIPT_DIR}/boot-linux-prepare.sh" setup || exit 1
. "${TMP_FILE}"
"${SCRIPT_DIR}/boot-linux.sh"
