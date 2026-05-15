#!/bin/sh
# /etc/init.d/S99automount
#
# Auto-mount the rv32emu virtio-blk device at /mnt if present.
# Tries ext4, ext2, squashfs, and filesystem autodetect in order.
# Read-only by default so a corrupt or unknown filesystem cannot brick
# the boot. Skips silently if no /dev/vda exists.

DEVICE=/dev/vda
MOUNT_POINT=/mnt

start()
{
    [ -b "$DEVICE" ] || return 0
    mkdir -p "$MOUNT_POINT"
    for fstype in ext4 ext2 squashfs auto; do
        if mount -t "$fstype" -o ro "$DEVICE" "$MOUNT_POINT" 2> /dev/null; then
            echo "[automount] $DEVICE mounted at $MOUNT_POINT ($fstype, ro)"
            return 0
        fi
    done
    echo "[automount] $DEVICE present but mount failed (unknown filesystem)"
    return 1
}

stop()
{
    mountpoint -q "$MOUNT_POINT" 2> /dev/null && umount "$MOUNT_POINT" 2> /dev/null
}

case "$1" in
    start) start ;;
    stop) stop ;;
    restart)
        stop
        start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
        ;;
esac
