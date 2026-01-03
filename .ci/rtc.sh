#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

RET=0

# FIXME: refactor to source common variable from common.sh
HOST_UTC_YEAR=$(LC_ALL=C date -u +%Y)

COLOR_G='\e[32;01m' # Green
COLOR_R='\e[31;01m' # Red
COLOR_Y='\e[33;01m' # Yellow
COLOR_N='\e[0m'     # No color

MESSAGES=("${COLOR_G}OK!"
    "${COLOR_R}Fail to boot"
    "${COLOR_R}Fail to login"
    "${COLOR_R}Fail to run commands"
    "${COLOR_R}Fail to find emu.txt in ${disk_img}"
)
TIMEOUT=50
OPTS_BASE=" -k build/linux-image/Image -i build/linux-image/rootfs.cpio"

# RTC alarm and settime tests
TEST_OPTIONS=("${OPTS_BASE}")
EXPECT_CMDS=('
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
TEST_OPTIONS+=("${OPTS_BASE}")
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
TEST_OPTIONS+=("${OPTS_BASE}")
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
TEST_OPTIONS+=("${OPTS_BASE}")
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
TEST_OPTIONS+=("${OPTS_BASE}")
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

for i in "${!TEST_OPTIONS[@]}"; do
    printf "${COLOR_Y}===== Test option: ${TEST_OPTIONS[$i]} =====${COLOR_N}\n"

    RUN_LINUX="build/rv32emu ${TEST_OPTIONS[$i]}"

    ASSERT expect <<- DONE
	set host_utc_year ${HOST_UTC_YEAR}
	set year1 ${YEAR1}
	set year2 ${YEAR2}
	set timeout ${TIMEOUT}
	spawn ${RUN_LINUX}
	${EXPECT_CMDS[$i]}
	DONE

    ret=$?
    RET=$((${RET} + ${ret}))
    cleanup

    printf "\nBoot Linux Test with RTC: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
done

exit ${RET}
