#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

RET=0

# Boot with gzipped images test
# Both gzipped
OPTS_BASE=" -k build/linux-image/Image.gz -i build/linux-image/rootfs.cpio.gz"
TEST_OPTIONS=("${OPTS_BASE}")
EXPECT_CMDS=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')
# gzipped kernel
OPTS_BASE=" -k build/linux-image/Image.gz -i build/linux-image/rootfs.cpio"
TEST_OPTIONS+=("${OPTS_BASE}")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')
# gzipped rootfs.cpio
OPTS_BASE=" -k build/linux-image/Image -i build/linux-image/rootfs.cpio.gz"
TEST_OPTIONS+=("${OPTS_BASE}")
EXPECT_CMDS+=('
    expect "buildroot login:" { send "root\n" } timeout { exit 1 }
    expect "# " { send "uname -a\n" } timeout { exit 2 }
    expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
')

BOOT_ATTEMPTS=$(
    normalize_test_attempts "${BOOT_ATTEMPTS_DEFAULT:-1}" 1
)

run_gzip_boot_case()
{
    expect <<- DONE
	set timeout ${TIMEOUT}
	spawn ${RUN_LINUX}
	${EXPECT_CMDS[$i]}
DONE
}

for i in "${!TEST_OPTIONS[@]}"; do
    printf "${COLOR_Y}===== Test option: ${TEST_OPTIONS[$i]} =====${COLOR_N}\n"

    RUN_LINUX="build/rv32emu ${TEST_OPTIONS[$i]}"

    run_test_with_retry \
        "Boot Linux with gzipped images test" \
        "${BOOT_ATTEMPTS}" \
        run_gzip_boot_case

    ret=$?
    RET=$((${RET} + ${ret}))

    if [ "${ret}" -ne 0 ]; then
        exit "${ret}"
    fi

    printf "\nBoot Linux with gzipped images test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
done

exit ${RET}
