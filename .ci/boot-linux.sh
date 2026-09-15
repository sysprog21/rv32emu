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

# Gzipped images tests in a subshell ()
(. "${SCRIPT_DIR}/boot-gzip-linux.sh")
RET=$((${RET} + $?))

# reboot tests in a subshell ()
(. "${SCRIPT_DIR}/reboot.sh")
RET=$((${RET} + $?))

# virtio-blk tests in a subshell ()
(. "${SCRIPT_DIR}/virtio-blk.sh")
RET=$((${RET} + $?))

# Virtio-net user-mode backend test
if grep -q '^CONFIG_VIRTIO_NET_USER=y$' .config; then
    (
        export VNET_BACKEND=user
        . "${SCRIPT_DIR}/netdev.sh"
    )
    RET=$((${RET} + $?))
fi

# Virtio-net TAP backend test
if grep -q '^CONFIG_VIRTIO_NET_TAP=y$' .config; then
    (
        export VNET_BACKEND=tap
        . "${SCRIPT_DIR}/netdev.sh"
    )
    RET=$((${RET} + $?))
fi

# Virtio-net vmnet backend test
if grep -q '^CONFIG_VIRTIO_NET_VMNET=y$' .config; then
    (
        export VNET_BACKEND=vmnet
        . "${SCRIPT_DIR}/netdev.sh"
    )
    RET=$((${RET} + $?))
fi

exit ${RET}
