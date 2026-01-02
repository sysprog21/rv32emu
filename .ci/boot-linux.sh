#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

# Register emulator cleanup for trap on EXIT
register_cleanup cleanup_emulator

check_image_for_file()
{
    local image_path=$1
    local file_path=$2
    local tool_available=0
    local debugfs_cmd

    debugfs_cmd=$(command -v debugfs 2> /dev/null || true)
    if [ -z "${debugfs_cmd}" ] && [ -x /sbin/debugfs ]; then
        debugfs_cmd=/sbin/debugfs
    fi

    if [ -n "${debugfs_cmd}" ]; then
        tool_available=1
        if "${debugfs_cmd}" -R "stat ${file_path}" "${image_path}" > /dev/null 2>&1; then
            return 0
        fi
    fi

    if command -v 7z > /dev/null 2>&1; then
        tool_available=1
        if 7z l "${image_path}" | grep -q "${file_path}"; then
            return 0
        fi
    fi

    if [ -n "${debugfs_cmd}" ]; then
        tool_available=1
        if sudo "${debugfs_cmd}" -R "stat ${file_path}" "${image_path}" > /dev/null 2>&1; then
            return 0
        fi
    fi

    if [ "${tool_available}" -eq 0 ]; then
        print_warning "Skipping verification of ${file_path} in ${image_path}: neither debugfs nor 7z is available."
        return 0
    fi

    return 1
}

# Use ASSERT from common.sh

cleanup

ENABLE_VBLK=1
VBLK_IMGS=(
    build/disk_ext4.img
)
# FIXME: mkfs.simplefs is not compilable on macOS, thus running the
# simplefs cases on Linux runner for now
if [[ "${OS_TYPE}" == "Linux" ]]; then
    VBLK_IMGS+=(build/disk_simplefs.img)
fi
SIMPLEFS_KO_SRC="${VBLK_IMGS[0]}"

# If any disk image is not found in the VBLK_IMGS, skip the VBLK tests
for disk_img in "${VBLK_IMGS[@]}"; do
    if [ ! -f "${disk_img}" ]; then
        ENABLE_VBLK=0
    fi
done

TIMEOUT=50
OPTS_BASE=" -k build/linux-image/Image"
OPTS_BASE+=" -i build/linux-image/rootfs.cpio"

TEST_OPTIONS=("base (${OPTS_BASE})")
EXPECT_CMDS=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')

COLOR_G='\e[32;01m' # Green
COLOR_R='\e[31;01m' # Red
COLOR_Y='\e[33;01m' # Yellow
COLOR_N='\e[0m'     # No color

for disk_img in "${VBLK_IMGS[@]}"; do
    TEST_OPTIONS=()
    EXPECT_CMDS=()

    MESSAGES=("${COLOR_G}OK!"
        "${COLOR_R}Fail to boot"
        "${COLOR_R}Fail to login"
        "${COLOR_R}Fail to run commands"
        "${COLOR_R}Fail to find emu.txt in ${disk_img}"
    )

    if [ "${ENABLE_VBLK}" -eq "1" ]; then
        # Read-only
        if [[ ${disk_img} =~ simplefs ]]; then
            TEST_OPTION=" -x vblk:${SIMPLEFS_KO_SRC} -x vblk:${disk_img},readonly"
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir simplefs_ko_src && mount /dev/vdb simplefs_ko_src && \
		insmod simplefs_ko_src/simplefs.ko\n" } timeout { exit 3 }
        expect "simplefs: module loaded" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
        else
            TEST_OPTION=" -x vblk:${disk_img},readonly"
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
        fi
        TEST_OPTIONS+=("${TEST_OPTION}")
        EXPECT_CMDS+=("${EXPECT_CMD}")

        # multiple blocks, Read-only, one disk image, one loop device (/dev/loopx(Linux) or /dev/diskx(Darwin))
        # FIXME: On macOS, block devices (/dev/diskX) require pread() fallback instead of mmap().
        # Combined with JIT compilation (especially gcc), this results in significantly slower
        # I/O performance that exceeds the boot timeout. Skip multi-device tests on macOS.
        if [[ "${OS_TYPE}" == "Darwin" ]]; then
            print_warning "Skipping multi-device readonly test on macOS (block device pread fallback too slow)"
        elif [[ ${disk_img} =~ simplefs ]]; then
            TEST_OPTION=(" -x vblk:${SIMPLEFS_KO_SRC} -x vblk:${disk_img},readonly -x vblk:${BLK_DEV_SIMPLEFS},readonly")
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir simplefs_ko_src && mount /dev/vdc simplefs_ko_src && \
		insmod simplefs_ko_src/simplefs.ko\n" } timeout { exit 3 }
        expect "simplefs: module loaded" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "mkdir mnt2 && mount /dev/vdb mnt2\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt2/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt2/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
            TEST_OPTIONS+=("${TEST_OPTION}")
            EXPECT_CMDS+=("${EXPECT_CMD}")
        else
            TEST_OPTION=(" -x vblk:${disk_img},readonly -x vblk:${BLK_DEV_EXT4},readonly")
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "mkdir mnt2 && mount /dev/vdb mnt2\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt2/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt2/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
            TEST_OPTIONS+=("${TEST_OPTION}")
            EXPECT_CMDS+=("${EXPECT_CMD}")
        fi

        # Read-write using disk image with ~ home directory symbol
        if [[ ${disk_img} =~ simplefs ]]; then
            TEST_OPTION=(" -x vblk:${SIMPLEFS_KO_SRC} -x vblk:~$(pwd | sed "s|$HOME||")/${disk_img}")
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir simplefs_ko_src && mount /dev/vdb simplefs_ko_src && \
		insmod simplefs_ko_src/simplefs.ko\n" } timeout { exit 3 }
        expect "simplefs: module loaded" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "cat mnt/emu.txt\n" } timeout { exit 3 }
        expect "rv32emu" { send "umount mnt\n" } timeout { exit 4 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
        else
            TEST_OPTION=(" -x vblk:~$(pwd | sed "s|$HOME||")/${disk_img}")
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt\n" } timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
        fi
        TEST_OPTIONS+=("${TEST_OPTION}")
        EXPECT_CMDS+=("${EXPECT_CMD}")

        # Read-write using disk image
        if [[ ${disk_img} =~ simplefs ]]; then
            TEST_OPTION=(" -x vblk:${SIMPLEFS_KO_SRC} -x vblk:${disk_img}")
            VBLK_EXPECT_CMDS='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir simplefs_ko_src && mount /dev/vdb simplefs_ko_src && \
		insmod simplefs_ko_src/simplefs.ko\n" } timeout { exit 3 }
        expect "simplefs: module loaded" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "cat mnt/emu.txt\n" } timeout { exit 3 }
        expect "rv32emu" { send "umount mnt\n" } timeout { exit 4 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
            EXPECT_CMD=("${VBLK_EXPECT_CMDS}")
        else
            TEST_OPTION=(" -x vblk:${disk_img}")
            VBLK_EXPECT_CMDS='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt\n" } timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
            EXPECT_CMD=("${VBLK_EXPECT_CMDS}")
        fi
        TEST_OPTIONS+=("${TEST_OPTION}")
        EXPECT_CMDS+=("${EXPECT_CMD}")

        # Read-write using /dev/loopx(Linux) or /dev/diskx(Darwin) block device
        if [[ ${disk_img} =~ simplefs ]]; then
            TEST_OPTIONS+=(" -x vblk:${SIMPLEFS_KO_SRC} -x vblk:${BLK_DEV_SIMPLEFS}")
            EXPECT_CMDS+=("${EXPECT_CMD}")
        else
            TEST_OPTIONS+=(" -x vblk:${BLK_DEV_EXT4}")
            EXPECT_CMDS+=("${EXPECT_CMD}")
        fi

        # multiple blocks, Read-write, one disk image and one loop device (/dev/loopx(Linux) or /dev/diskx(Darwin))
        # FIXME: On macOS, hdiutil locks the disk image file when attached as a block device.
        # Opening both the original file and its attached block device for read-write access
        # simultaneously causes pread() to block indefinitely. Skip this test on macOS.
        if [[ "${OS_TYPE}" == "Darwin" ]]; then
            print_warning "Skipping combined disk image + block device read-write test on macOS (file locking conflict)"
        elif [[ ${disk_img} =~ simplefs ]]; then
            TEST_OPTION=(" -x vblk:${SIMPLEFS_KO_SRC} -x vblk:${disk_img} -x vblk:${BLK_DEV_SIMPLEFS}")
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir simplefs_ko_src && mount /dev/vdc simplefs_ko_src && \
		insmod simplefs_ko_src/simplefs.ko\n" } timeout { exit 3 }
        expect "simplefs: module loaded" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt\n" } timeout { exit 3 }
        expect "# " { send "mkdir mnt2 && mount /dev/vdb mnt2\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt2/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "cat mnt2/emu.txt\n" } timeout { exit 3 }
        expect "rv32emu" { send "umount mnt2\n" } timeout { exit 4 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
            TEST_OPTIONS+=("${TEST_OPTION}")
            EXPECT_CMDS+=("${EXPECT_CMD}")
        else
            TEST_OPTION=(" -x vblk:${disk_img} -x vblk:${BLK_DEV_EXT4}")
            EXPECT_CMD='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt\n" } timeout { exit 3 }
        expect "# " { send "mkdir mnt2 && mount /dev/vdb mnt2\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt2/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt2\n" } timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
            TEST_OPTIONS+=("${TEST_OPTION}")
            EXPECT_CMDS+=("${EXPECT_CMD}")
        fi
    fi

    for i in "${!TEST_OPTIONS[@]}"; do
        printf "${COLOR_Y}===== Test option: ${TEST_OPTIONS[$i]} =====${COLOR_N}\n"

        OPTS="${OPTS_BASE}"
        # No need to add option when running base test
        if [[ ! "${TEST_OPTIONS[$i]}" =~ "base" ]]; then
            OPTS+="${TEST_OPTIONS[$i]}"
        fi
        RUN_LINUX="build/rv32emu ${OPTS}"

        ASSERT expect <<- DONE
	set timeout ${TIMEOUT}
	spawn ${RUN_LINUX}
	${EXPECT_CMDS[$i]}
	DONE

        ret=$?
        cleanup

        printf "\nBoot Linux Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
        if [[ "${TEST_OPTIONS[$i]}" =~ vblk ]]; then
            # read-only test first, so the emu.txt definitely does not exist, skipping the check
            if [[ ! "${TEST_OPTIONS[$i]}" =~ readonly ]]; then
                if [[ ${disk_img} =~ simplefs ]]; then
                    # Does not verify the written file on hostOS since 7z does not recognize the format.
                    # But, the written file is printed out and verified in EXPECT_CMD in guestOS
                    :
                else
                    7z l ${disk_img} | grep emu.txt > /dev/null 2>&1 || ret=4
                fi
            fi
            printf "Virtio-blk Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
        fi
    done
done

exit ${ret}
