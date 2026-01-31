#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

RET=0

# Reboot Tests
# cold reboot
TEST_OPTIONS=("${OPTS_BASE}")
EXPECT_CMDS=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "reboot\n" } timeout { exit 3 }
    expect -ex "cold reboot" {} timeout { exit 1 }
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')
# warm reboot
TEST_OPTIONS+=("${OPTS_BASE}")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "echo 'warm' > /sys/kernel/reboot/mode && reboot\n" } timeout { exit 3 }
    expect -ex "warm reboot" {} timeout { exit 1 }
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')

for i in "${!TEST_OPTIONS[@]}"; do
    printf "${COLOR_Y}===== Test option: ${TEST_OPTIONS[$i]} =====${COLOR_N}\n"

    RUN_LINUX="build/rv32emu ${TEST_OPTIONS[$i]}"

    ASSERT expect <<- DONE
	set timeout ${TIMEOUT}
	spawn ${RUN_LINUX}
	${EXPECT_CMDS[$i]}
	DONE

    ret=$?
    RET=$((${RET} + ${ret}))
    cleanup

    printf "\nBoot Linux Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
done

exit ${RET}
