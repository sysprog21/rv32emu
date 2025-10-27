#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

VBLK_IMGS=(
    build/disk_ext4.img
)
# FIXME: mkfs.simplefs is not compilable on macOS, thus running the
# simplefs cases on Linux runner for now
if [[ "${OS_TYPE}" == "Linux" ]]; then
    VBLK_IMGS+=(build/disk_simplefs.img)
fi

which dd > /dev/null 2>&1 || {
    echo "Error: dd not found"
    exit 1
}
which mkfs.ext4 > /dev/null 2>&1 || which $(brew --prefix e2fsprogs)/sbin/mkfs.ext4 > /dev/null 2>&1 \
    || {
        echo "Error: mkfs.ext4 not found"
        exit 1
    }
# Optional tooling used later in the test suite
if ! command -v debugfs > /dev/null 2>&1 && ! command -v 7z > /dev/null 2>&1; then
    print_warning "Neither debugfs nor 7z is available; virtio-blk verification will be skipped."
fi

ACTION=$1

case "$ACTION" in
    setup)
        # Clone simplefs to use mkfs.simplefs util and create simplefs disk image
        git clone https://github.com/sysprog21/simplefs.git -b rel2025.0 --depth 1

        # Setup disk images
        for disk_img in "${VBLK_IMGS[@]}"; do
            case "${OS_TYPE}" in
                Linux)
                    # Setup a /dev/ block device with ext4 fs to test guestOS access to hostOS /dev/ block device
                    if [[ "${disk_img}" =~ ext4 ]]; then
                        dd if=/dev/zero of=${disk_img} bs=4M count=32
                        mkfs.ext4 ${disk_img}
                    else
                        mkdir -p simplefs/build
                        make IMAGE=${disk_img} ${disk_img} -C simplefs
                        mv simplefs/${disk_img} ./build
                    fi
                    BLK_DEV=$(losetup -f)
                    losetup ${BLK_DEV} ${disk_img}
                    ;;
                Darwin)
                    # Setup a /dev/ block device with ext4 fs to test guestOS access to hostOS /dev/ block device
                    dd if=/dev/zero of=${disk_img} bs=4M count=32
                    $(brew --prefix e2fsprogs)/sbin/mkfs.ext4 ${disk_img}
                    BLK_DEV=$(hdiutil attach -nomount ${disk_img})
                    ;;
            esac

            # On Linux, ${disk_img} will be created by root and owned by root:root.
            # Even if "others" have read and write (rw) permissions, accessing the file for certain operations may
            # still require elevated privileges (e.g., setuid).
            # To simplify this, we change the ownership to a non-root user.
            # Use this with caution—changing ownership to runner:runner is specific to the GitHub CI environment.
            chown runner: ${disk_img}
            # Add other's rw permission to the disk image and device, so non-superuser can rw them
            chmod o+r,o+w ${disk_img}
            chmod o+r,o+w ${BLK_DEV}

            # Export ${BLK_DEV} to a tmp file. Then, source to "$GITHUB_ENV" in job step.
            if [[ "${disk_img}" =~ ext4 ]]; then
                echo "export BLK_DEV_EXT4=${BLK_DEV}" >> "${TMP_FILE}"
            else
                echo "export BLK_DEV_SIMPLEFS=${BLK_DEV}" >> "${TMP_FILE}"
            fi
        done

        # Put simplefs.ko into ext4 fs
        mkdir -p mnt
        mount ${VBLK_IMGS[0]} mnt
        cp build/linux-image/simplefs.ko mnt
        umount mnt
        rm -rf mnt
        ;;
    cleanup)
        # Remove simplefs repo
        rm -rf simplefs

        # Detach the /dev/loopx(Linux) or /dev/diskx(Darwin)
        case "${OS_TYPE}" in
            Linux)
                losetup -d ${BLK_DEV_EXT4}
                losetup -d ${BLK_DEV_SIMPLEFS}
                ;;
            Darwin)
                hdiutil detach ${BLK_DEV_EXT4}
                ;;
        esac

        # delete disk images
        for disk_img in "${VBLK_IMGS[@]}"; do
            rm -f ${disk_img}
        done

        # delete tmp file
        rm "${TMP_FILE}"
        ;;
    *)
        printf "Usage: %s {setup|cleanup}\n" "$0"
        exit 1
        ;;
esac
