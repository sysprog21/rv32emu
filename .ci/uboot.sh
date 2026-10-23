#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

register_cleanup cleanup_emulator

RET=0

UBOOT_BIN="build/linux-image/u-boot.bin"
if [ ! -f "${UBOOT_BIN}" ]; then
    print_error "${UBOOT_BIN} not found"
    exit 1
fi

OPTS_UBOOT=" -k ${UBOOT_BIN} -x vblk:build/linux-image/rootfs.ext4,rootfs"

TEST_OPTIONS=("${OPTS_UBOOT}")
EXPECT_CMDS=('
    expect "Hit any key to stop autoboot:" { send "\n" } eof { exit 1 } timeout { exit 1 }
    expect "=> " { send "sbi\n" } eof { exit 2 } timeout { exit 2 }
    expect "SBI 0.3" {} eof { exit 3 } timeout { exit 3 }
    expect "=> " { send "reset\n" } eof { exit 2 } timeout { exit 2 }
    expect -ex "cold reboot" {} eof { exit 1 } timeout { exit 1 }
    expect "Hit any key to stop autoboot:" { send "\n" } eof { exit 1 } timeout { exit 1 }
    expect "=> " { send "poweroff\n" } eof { exit 2 } timeout { exit 2 }
    expect "System reset: type=shutdown" {} eof { exit 3 } timeout { exit 3 }
')

TEST_OPTIONS+=("${OPTS_UBOOT}")
EXPECT_CMDS+=('
    expect "U-Boot 2026" {} eof { exit 1 } timeout { exit 1 }
    expect "Starting kernel ..." {} eof { exit 1 } timeout { exit 1 }
    expect "buildroot login:" { send "root\n" } eof { exit 1 } timeout { exit 1 }
    expect "# " { send "uname -a\n" } eof { exit 2 } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } eof { exit 3 } timeout { exit 3 }
')

for i in "${!TEST_OPTIONS[@]}"; do
    printf "${COLOR_Y}===== Test option: ${TEST_OPTIONS[$i]} =====${COLOR_N}\n"

    RUN_UBOOT="build/rv32emu ${TEST_OPTIONS[$i]}"

    ASSERT expect <<- DONE
	set timeout ${TIMEOUT}
	spawn ${RUN_UBOOT}
	${EXPECT_CMDS[$i]}
	DONE

    ret=$?
    RET=$((${RET} + ${ret}))
    cleanup

    printf "\nU-Boot Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
done

exit ${RET}
