#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

# Register emulator cleanup for trap on EXIT
register_cleanup cleanup_emulator

cleanup

# RTC tests in a subshell ()
(. "${SCRIPT_DIR}/rtc.sh")
RET=$?

# reboot tests in a subshell ()
(. "${SCRIPT_DIR}/reboot.sh")
RET=$((${RET} + $?))

# virtio-blk tests in a subshell ()
(. "${SCRIPT_DIR}/virtio-blk.sh")
RET=$((${RET} + $?))

exit ${RET}
