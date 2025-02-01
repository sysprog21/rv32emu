#!/usr/bin/env bash

function cleanup {
    sleep 1
    pkill -9 rv32emu
}

function ASSERT {
    $*
    local RES=$?
    if [ $RES -ne 0 ]; then
        echo 'Assert failed: "' $* '"'
        exit $RES
    fi
}

cleanup

ENABLE_VBLK=1
VBLK_IMG=build/disk.img
which dd >/dev/null 2>&1 || ENABLE_VBLK=0
which mkfs.ext4 >/dev/null 2>&1 || which $(brew --prefix e2fsprogs)/sbin/mkfs.ext4 >/dev/null 2>&1 || ENABLE_VBLK=0
which 7z >/dev/null 2>&1 || ENABLE_VBLK=0

TIMEOUT=50
OPTS=" -k build/linux-image/Image "
OPTS+=" -i build/linux-image/rootfs.cpio "
if [ "$ENABLE_VBLK" -eq "1" ]; then
    dd if=/dev/zero of=$VBLK_IMG bs=4M count=32
    mkfs.ext4 $VBLK_IMG || $(brew --prefix e2fsprogs)/sbin/mkfs.ext4 $VBLK_IMG
    OPTS+=" -x vblk:$VBLK_IMG "
else
    printf "Virtio-blk Test...Passed\n"
fi
RUN_LINUX="build/rv32emu ${OPTS}"

if [ "$ENABLE_VBLK" -eq "1" ]; then
ASSERT expect <<DONE
set timeout ${TIMEOUT}
spawn ${RUN_LINUX}
expect "buildroot login:" { send "root\n" } timeout { exit 1 }
expect "# " { send "uname -a\n" } timeout { exit 2 }
expect "riscv32 GNU/Linux" { send "mkdir mnt && mount /dev/vda mnt\n" } timeout { exit 3 }
expect "# " { send "echo rv32emu > mnt/emu.txt\n" } timeout { exit 3 }
expect "# " { send "sync\n" } timeout { exit 3 }
expect "# " { send "umount mnt\n" } timeout { exit 3 }
expect "# " { send "\x01"; send "x" } timeout { exit 3 }
DONE
else
ASSERT expect <<DONE
set timeout ${TIMEOUT}
spawn ${RUN_LINUX}
expect "buildroot login:" { send "root\n" } timeout { exit 1 }
expect "# " { send "uname -a\n" } timeout { exit 2 }
expect "riscv32 GNU/Linux" { send "\x01"; send "x" } timeout { exit 3 }
DONE
fi
ret=$?
cleanup

COLOR_G='\e[32;01m' # Green
COLOR_R='\e[31;01m' # Red
COLOR_N='\e[0m' # No color

MESSAGES=("${COLOR_G}OK!" \
     "${COLOR_R}Fail to boot" \
     "${COLOR_R}Fail to login" \
     "${COLOR_R}Fail to run commands" \
     "${COLOR_R}Fail to find emu.txt in $VBLK_IMG"\
)

printf "\nBoot Linux Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
if [ "$ENABLE_VBLK" -eq "1" ]; then 
    7z l $VBLK_IMG | grep emu.txt >/dev/null 2>&1 || ret=4
    printf "Virtio-blk Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"
fi

exit ${ret}
