#!/usr/bin/env bash

. .ci/common.sh

check_platform

function cleanup
{
    sleep 1
    pkill -9 rv32emu
}

function ASSERT
{
    $*
    local RES=$?
    if [ ${RES} -ne 0 ]; then
        echo 'Assert failed: "' $* '"'
        exit ${RES}
    fi
}

cleanup

ENABLE_VBLK=1
VBLK_IMG=build/disk.img
[ -f "${VBLK_IMG}" ] || ENABLE_VBLK=0

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
        expect "# " { send "uname -a\n" } timeout { exit 2 }
        expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
        expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
        expect -ex "-sh: can'\''t create mnt/emu.txt: Read-only file system" {} timeout { exit 3 }
        expect "# " { send "\x01"; send "x" } timeout { exit 3 }
    ')

    # Read-write using disk image
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${VBLK_IMG}")
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

    # Read-write using /dev/loopx(Linux) or /dev/diskx(Darwin) block device
    TEST_OPTIONS+=("${OPTS_BASE} -x vblk:${BLK_DEV}")
    EXPECT_CMDS+=("${VBLK_EXPECT_CMDS}")
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
            7z l ${VBLK_IMG} | grep emu.txt > /dev/null 2>&1 || ret=4
        fi
        printf "Virtio-blk Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
    fi
done

exit ${ret}
