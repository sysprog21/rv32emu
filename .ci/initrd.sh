#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

# Register emulator cleanup for trap on EXIT
register_cleanup cleanup_emulator

cleanup

RET=0

####################### Boot tests using initrd #######################

# VirtIO net tests, only for the backends enabled in the build
# TAP on Linux
if grep -q '^CONFIG_VIRTIO_NET_TAP=y$' .config; then
    (
        export VNET_BACKEND=tap
        . "${SCRIPT_DIR}/netdev.sh" "initrd"
    )
    RET=$((${RET} + $?))
fi

# slirp
if grep -q '^CONFIG_VIRTIO_NET_USER=y$' .config; then
    (
        export VNET_BACKEND=user
        . "${SCRIPT_DIR}/netdev.sh" "initrd"
    )
    RET=$((${RET} + $?))
fi

# vmnet on macOS
if grep -q '^CONFIG_VIRTIO_NET_VMNET=y$' .config; then
    (
        export VNET_BACKEND=vmnet
        . "${SCRIPT_DIR}/netdev.sh" "initrd"
    )
    RET=$((${RET} + $?))
fi

# Gzipped images tests
(. "${SCRIPT_DIR}/boot-gzip-linux.sh")
RET=$((${RET} + $?))

# VirtIO block tests
VBLK_IMGS=(
    build/disk_ext4.img
    build/disk_simplefs.img
)
SIMPLEFS_KO_SRC="${VBLK_IMGS[0]}"
# single ext4 disk + readonly
TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${VBLK_IMGS[0]},readonly")
EXPECT_CMDS+=('
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir -p mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')
# two disks: ext4 disk + rw + simplefs disk + readonly
TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${SIMPLEFS_KO_SRC} -x vblk:${VBLK_IMGS[1]},readonly")
EXPECT_CMDS+=('
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir -p simplefs_ko_src && mount /dev/vdb simplefs_ko_src && \
		insmod simplefs_ko_src/simplefs.ko\n" } timeout { exit 3 }
        expect "simplefs: module loaded" { send "mkdir -p mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')

# single ext4 disk + rw
TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${VBLK_IMGS[0]}")
EXPECT_CMDS+=('
        expect "buildroot login:" { send "root\n" } timeout { exit 1 }
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir -p mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect "# " { send "sync\n" } timeout { exit 3 }
        expect "# " { send "umount mnt\n" } timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
')

# RTC alarm and settime tests
HOST_UTC_YEAR=$(LC_ALL=C date -u +%Y)
TEST_OPTIONS+=("${OPTS_BASE}")
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

# Reboot Tests
# cold reboot
TEST_OPTIONS+=("${OPTS_BASE}")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "reboot\n" } timeout { exit 3 }
    expect -ex "cold reboot" {} timeout { exit 1 }
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')

for i in "${!TEST_OPTIONS[@]}"; do
    printf "${COLOR_Y}===== Test option: ${TEST_OPTIONS[$i]} =====${COLOR_N}\n"

    RUN_LINUX="build/rv32emu ${TEST_OPTIONS[$i]}"

    ASSERT expect <<- DONE
	set host_utc_year ${HOST_UTC_YEAR}
	set timeout ${TIMEOUT}
	spawn ${RUN_LINUX}
	${EXPECT_CMDS[$i]}
	DONE

    ret=$?
    RET=$((${RET} + ${ret}))
    cleanup

    printf "\nLinux Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
done

exit ${RET}
