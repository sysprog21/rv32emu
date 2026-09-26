#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

# Strict mode matters more here than in the other boot scripts: this one runs as
# root and creates, formats and attaches block devices. Without it a failed
# mkfs.ext4 fell through to losetup, chown and mount on a garbage image, and the
# real error surfaced much later as an unexplained boot timeout.
#
# The sibling scripts (boot-linux.sh, virtio-blk.sh, reboot.sh, rtc.sh)
# deliberately do not do this: they sum up $? from expect runs that are expected
# to fail, and errexit would abort on the first failing case instead of
# reporting all of them.
set -e -u -o pipefail

check_platform

VBLK_IMGS=(
    build/disk_ext4.img
    build/disk_simplefs.img
)

# Tools only the setup path needs. Checked there rather than here: teardown
# must stay runnable when setup failed precisely because a tool was missing,
# otherwise the devices setup did attach are never released.
check_setup_tools()
{
    which dd > /dev/null 2>&1 || {
        echo "Error: dd not found"
        exit 1
    }
    which mkfs.ext4 > /dev/null 2>&1 \
        || which "$(brew --prefix e2fsprogs 2> /dev/null)/sbin/mkfs.ext4" > /dev/null 2>&1 \
        || {
            echo "Error: mkfs.ext4 not found"
            exit 1
        }

    # Optional tooling used later in the test suite
    if ! command -v debugfs > /dev/null 2>&1 && ! command -v 7z > /dev/null 2>&1; then
        print_warning "Neither debugfs nor 7z is available; virtio-blk verification will be skipped."
    fi
}

# Default rather than $1: under set -u a missing argument would abort here with
# "unbound variable" and never reach the usage message below.
ACTION=${1:-}

# Both actions read and write TMP_FILE, which the caller passes through
# "sudo env". Under set -u a missing one aborts with a bare "unbound variable"
# from whichever line happens to touch it first.
if [ -z "${TMP_FILE:-}" ] && [ "${ACTION}" != "" ]; then
    echo "Error: TMP_FILE must be set (see .ci/boot-linux-test.sh)" >&2
    exit 2
fi

case "$ACTION" in
    setup)
        check_setup_tools

        # Clone simplefs to use mkfs.simplefs util and create simplefs disk
        # image
        git clone https://github.com/sysprog21/simplefs -b rel2026.0 --depth 1

        # Setup disk images
        for disk_img in "${VBLK_IMGS[@]}"; do
            case "${OS_TYPE}" in
                Linux)

                    # Setup a /dev/ block device with ext4 fs to test guestOS
                    # access to hostOS /dev/ block device
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

                    # Setup a /dev/ block device with ext4 fs to test guestOS
                    # access to hostOS /dev/ block device
                    if [[ "${disk_img}" =~ ext4 ]]; then
                        dd if=/dev/zero of=${disk_img} bs=4M count=32
                        $(brew --prefix e2fsprogs)/sbin/mkfs.ext4 ${disk_img}

                        # Write simplefs.ko BEFORE hdiutil attach (hdiutil locks
                        # the image file)
                        DEBUGFS_CMD="$(brew --prefix e2fsprogs)/sbin/debugfs"
                        if [ ! -x "${DEBUGFS_CMD}" ]; then
                            echo "Error: debugfs not found at ${DEBUGFS_CMD}"
                            exit 1
                        fi
                        "${DEBUGFS_CMD}" -w -R \
                            "write build/linux-image/simplefs.ko simplefs.ko" "${disk_img}" \
                            || exit 1
                    else
                        mkdir -p simplefs/build
                        make IMAGE=${disk_img} ${disk_img} -C simplefs
                        mv simplefs/${disk_img} ./build
                    fi
                    BLK_DEV=$(hdiutil attach -nomount ${disk_img})
                    ;;
            esac

            # On Linux, ${disk_img} will be created by root and owned by
            # root:root. Even if "others" have read and write (rw) permissions,
            # accessing the file for certain operations may still require
            # elevated privileges (e.g., setuid). To simplify this, we change
            # the ownership to the user who invoked sudo: runner on GitHub CI.
            chown "${SUDO_USER:-$(id -un)}": ${disk_img}

            # Add other's rw permission to the disk image and device, so
            # non-superuser can rw them
            chmod o+r,o+w ${disk_img}
            chmod o+r,o+w ${BLK_DEV}

            # Export ${BLK_DEV} to a tmp file. Then, source to "$GITHUB_ENV" in
            # job step.
            if [[ "${disk_img}" =~ ext4 ]]; then
                echo "export BLK_DEV_EXT4=${BLK_DEV}" >> "${TMP_FILE}"
            else
                echo "export BLK_DEV_SIMPLEFS=${BLK_DEV}" >> "${TMP_FILE}"
            fi
        done

        # Put simplefs.ko into ext4 fs (Linux only; Darwin handled above before
        # hdiutil attach)
        if [ "${OS_TYPE}" = "Linux" ]; then
            mkdir -p mnt
            mount "${VBLK_IMGS[0]}" mnt
            cp build/linux-image/simplefs.ko mnt
            umount mnt
            rm -rf mnt
        fi
        ;;
    cleanup)
        # Remove simplefs repo
        rm -rf simplefs

        # Detach the /dev/loopx(Linux) or /dev/diskx(Darwin).
        #
        # Cleanup also runs when setup died partway through, so a device may
        # never have been attached. Skip the empty ones and keep going past a
        # failed detach: leaking one loop device is better than aborting before
        # the other one and the disk images are released.
        for blk_dev in "${BLK_DEV_EXT4:-}" "${BLK_DEV_SIMPLEFS:-}"; do
            [ -n "${blk_dev}" ] || continue
            case "${OS_TYPE}" in
                Linux) losetup -d "${blk_dev}" || true ;;
                Darwin) hdiutil detach "${blk_dev}" || true ;;
            esac
        done

        # delete disk images
        for disk_img in "${VBLK_IMGS[@]}"; do
            rm -f ${disk_img}
        done

        # delete tmp file; -f because setup may have died before creating it
        rm -f "${TMP_FILE}"
        ;;
    *)
        printf "Usage: %s {setup|cleanup}\n" "$0"
        exit 1
        ;;
esac
