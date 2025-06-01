#!/usr/bin/env bash

. .ci/common.sh

check_platform

VBLK_IMG=build/disk.img
which dd > /dev/null 2>&1 || {
    echo "Error: dd not found"
    exit 1
}
which mkfs.ext4 > /dev/null 2>&1 || which $(brew --prefix e2fsprogs)/sbin/mkfs.ext4 > /dev/null 2>&1 \
    || {
        echo "Error: mkfs.ext4 not found"
        exit 1
    }
which 7z > /dev/null 2>&1 || {
    echo "Error: 7z not found"
    exit 1
}

ACTION=$1

case "$ACTION" in
    setup)
        # Setup a disk image
        dd if=/dev/zero of=${VBLK_IMG} bs=4M count=32

        # Setup a /dev/ block device with ${VBLK_IMG} to test guestOS access to hostOS /dev/ block device
        case "${OS_TYPE}" in
            Linux)
                mkfs.ext4 ${VBLK_IMG}
                BLK_DEV=$(losetup -f)
                losetup ${BLK_DEV} ${VBLK_IMG}
                ;;
            Darwin)
                $(brew --prefix e2fsprogs)/sbin/mkfs.ext4 ${VBLK_IMG}
                BLK_DEV=$(hdiutil attach -nomount ${VBLK_IMG})
                ;;
        esac

        # On Linux, ${VBLK_IMG} will be created by root and owned by root:root.
        # Even if "others" have read and write (rw) permissions, accessing the file for certain operations may
        # still require elevated privileges (e.g., setuid).
        # To simplify this, we change the ownership to a non-root user.
        # Use this with caution—changing ownership to runner:runner is specific to the GitHub CI environment.
        chown runner: ${VBLK_IMG}
        # Add other's rw permission to the disk image and device, so non-superuser can rw them
        chmod o+r,o+w ${VBLK_IMG}
        chmod o+r,o+w ${BLK_DEV}

        # Export ${BLK_DEV} to a tmp file. Then, source to "$GITHUB_ENV" in job step.
        echo "export BLK_DEV=${BLK_DEV}" > "${TMP_FILE}"
        ;;
    cleanup)
        # Detach the /dev/loopx(Linux) or /dev/diskx(Darwin)
        case "${OS_TYPE}" in
            Linux)
                losetup -d ${BLK_DEV}
                ;;
            Darwin)
                hdiutil detach ${BLK_DEV}
                ;;
        esac

        # delete disk image
        rm -f ${VBLK_IMG}

        # delete tmp file
        rm "${TMP_FILE}"
        ;;
    *)
        printf "Usage: %s {setup|cleanup}\n" "$0"
        exit 1
        ;;
esac
