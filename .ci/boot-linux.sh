#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

cleanup()
{
    sleep 1
    pkill -9 rv32emu
}

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

ASSERT()
{
    $*
    local RES=$?
    if [ ${RES} -ne 0 ]; then
        echo 'Assert failed: "' $* '"'
        exit ${RES}
    fi
}

cleanup

# To test RTC clock
HOST_UTC_YEAR=$(LC_ALL=C date -u +%Y)

ENABLE_VBLK=1
VBLK_IMG=build/disk.img
[ -f "${VBLK_IMG}" ] || ENABLE_VBLK=0

TIMEOUT=50
OPTS_BASE=" -k build/linux-image/Image"
OPTS_BASE+=" -i build/linux-image/rootfs.cpio"

TEST_OPTIONS=("base (${OPTS_BASE})")
EXPECT_CMDS=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
    expect "rtc0" { } timeout { exit 3 }
    expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
    expect "${host_utc_year}" { } timeout { exit 3 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')

# RTC alarm and settime tests
TEST_OPTIONS+=("base (${OPTS_BASE})")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
    expect "rtc0" { } timeout { exit 3 }
    expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
    expect "${host_utc_year}" { } timeout { exit 3 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { } timeout { exit 3 }
    expect "# " { send "rtc_alarm\n" } timeout { exit 3 }
    expect "alarm_IRQ	: no" { } timeout { exit 3 }
    expect "alarm_IRQ	: yes" { } timeout { exit 3 }
    expect "alarm_IRQ	: no" { } timeout { exit 3 }
    expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')
YEAR1=1980
YEAR2=2030
TEST_OPTIONS+=("base (${OPTS_BASE})")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
    expect "rtc0" { } timeout { exit 3 }
    expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
    expect "${host_utc_year}" { } timeout { exit 3 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { } timeout { exit 3 }
    expect "# " { send "rtc_settime ${year1}\n" } timeout { exit 3 }
    expect "rtc_date	: ${year1}-01-01" { } timeout { exit 3 }
    expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')
TEST_OPTIONS+=("base (${OPTS_BASE})")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
    expect "rtc0" { } timeout { exit 3 }
    expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
    expect "${host_utc_year}" { } timeout { exit 3 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { } timeout { exit 3 }
    expect "# " { send "rtc_settime ${year2}\n" } timeout { exit 3 }
    expect "rtc_date	: ${year2}-01-01" { } timeout { exit 3 }
    expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')
TEST_OPTIONS+=("base (${OPTS_BASE})")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
    expect "rtc0" { } timeout { exit 3 }
    expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
    expect "${host_utc_year}" { } timeout { exit 3 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { } timeout { exit 3 }
    expect "# " { send "rtc_settime ${year1}\n" } timeout { exit 3 }
    expect "rtc_date	: ${year1}-01-01" { } timeout { exit 3 }
    expect "# " { send "rtc_alarm\n" } timeout { exit 3 }
    expect "alarm_IRQ	: no" { } timeout { exit 3 }
    expect "alarm_IRQ	: yes" { } timeout { exit 3 }
    expect "alarm_IRQ	: no" { } timeout { exit 3 }
    expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')
TEST_OPTIONS+=("base (${OPTS_BASE})")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
    expect "rtc0" { } timeout { exit 3 }
    expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
    expect "${host_utc_year}" { } timeout { exit 3 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { } timeout { exit 3 }
    expect "# " { send "rtc_settime ${year2}\n" } timeout { exit 3 }
    expect "rtc_date	: ${year2}-01-01" { } timeout { exit 3 }
    expect "# " { send "rtc_alarm\n" } timeout { exit 3 }
    expect "alarm_IRQ	: no" { } timeout { exit 3 }
    expect "alarm_IRQ	: yes" { } timeout { exit 3 }
    expect "alarm_IRQ	: no" { } timeout { exit 3 }
    expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')

COLOR_G='\e[32;01m' # Green
COLOR_R='\e[31;01m' # Red
COLOR_Y='\e[33;01m' # Yellow
COLOR_N='\e[0m'     # No color

MESSAGES=("${COLOR_G}OK!"
    "${COLOR_R}Fail to boot"
    "${COLOR_R}Fail to login"
    "${COLOR_R}Fail to run commands"
    "${COLOR_R}Fail to find emu.txt in ${VBLK_IMG}"
)

if [ "${ENABLE_VBLK}" -eq "1" ]; then
    # Read-only
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${VBLK_IMG},readonly")
    EXPECT_CMDS+=('
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
        expect "rtc0" { } timeout { exit 3 }
        expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
        expect "${host_utc_year}" { } timeout { exit 3 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    ')

    # multiple blocks, Read-only, one disk image, one loop device (/dev/loopx(Linux) or /dev/diskx(Darwin))
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${VBLK_IMG},readonly -x vblk:${BLK_DEV},readonly")
    EXPECT_CMDS+=('
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
        expect "rtc0" { } timeout { exit 3 }
        expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
        expect "${host_utc_year}" { } timeout { exit 3 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "mkdir mnt2 && mount /dev/vdb mnt2\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt2/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt2/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    ')

    # Read-write using disk image with ~ home directory symbol
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:~$(pwd | sed "s|$HOME||")/${VBLK_IMG}")
    VBLK_EXPECT_CMDS='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt\n" } timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
    EXPECT_CMDS+=("${VBLK_EXPECT_CMDS}")

    # Read-write using disk image
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${VBLK_IMG}")
    VBLK_EXPECT_CMDS='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
        expect "rtc0" { } timeout { exit 3 }
        expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
        expect "${host_utc_year}" { } timeout { exit 3 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt\n" } timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    '
    EXPECT_CMDS+=("${VBLK_EXPECT_CMDS}")

    # Read-write using /dev/loopx(Linux) or /dev/diskx(Darwin) block device
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${BLK_DEV}")
    EXPECT_CMDS+=("${VBLK_EXPECT_CMDS}")

    # multiple blocks, Read-write, one disk image and one loop device (/dev/loopx(Linux) or /dev/diskx(Darwin))
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${VBLK_IMG} -x vblk:${BLK_DEV}")
    VBLK_EXPECT_CMDS='
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "dmesg | grep rtc\n" } timeout { exit 2 }
        expect "rtc0" { } timeout { exit 3 }
        expect "# " { send "date -u +%Y\n" } timeout { exit 2 }
        expect "${host_utc_year}" { } timeout { exit 3 }
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
    EXPECT_CMDS+=("${VBLK_EXPECT_CMDS}")
fi

for i in "${!TEST_OPTIONS[@]}"; do
    printf "${COLOR_Y}===== Test option: ${TEST_OPTIONS[$i]} =====${COLOR_N}\n"

    OPTS="${OPTS_BASE}"
    # No need to add option when running base test
    case "${TEST_OPTIONS[$i]}" in
        *base*) ;;
        *)
            OPTS+="${TEST_OPTIONS[$i]}"
            ;;
    esac
    RUN_LINUX="build/rv32emu ${OPTS}"

    ASSERT expect <<- DONE
	set host_utc_year ${HOST_UTC_YEAR}
	set year1 ${YEAR1}
	set year2 ${YEAR2}
	set timeout ${TIMEOUT}
	spawn ${RUN_LINUX}
	${EXPECT_CMDS[$i]}
	DONE

    ret=$?
    cleanup

    printf "\nBoot Linux Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
    case "${TEST_OPTIONS[$i]}" in
        *vblk*)
            # read-only test first, so the emu.txt definitely does not exist, skipping the check
            case "${TEST_OPTIONS[$i]}" in
                *readonly*) ;;
                *)
                    if ! check_image_for_file "${VBLK_IMG}" emu.txt; then
                        ret=4
                    fi
                    ;;
            esac
            printf "Virtio-blk Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
            ;;
    esac
done

exit ${ret}
