#!/usr/bin/env bash

function ASSERT
{
    $*
    RES=$?
    if [ $RES -ne 0 ]; then
        echo 'Assert failed: "' $* '"'
        exit $RES
    fi
}

PASS_COLOR='\e[32;01m'
NO_COLOR='\e[0m'
function OK
{
    printf " [ ${PASS_COLOR} OK ${NO_COLOR} ]\n"
}

SRC_DIR=/tmp

PARALLEL="-j$(nproc)"

function do_buildroot
{
    cp -f assets/system/configs/buildroot.config $SRC_DIR/buildroot/.config
    cp -f assets/system/configs/busybox.config $SRC_DIR/buildroot/busybox.config
    # Otherwise, the error below raises:
    #   You seem to have the current working directory in your
    #   LD_LIBRARY_PATH environment variable. This doesn't work.
    unset LD_LIBRARY_PATH
    pushd $SRC_DIR/buildroot
    ASSERT make olddefconfig
    ASSERT make $PARALLEL
    popd
    cp -f $SRC_DIR/buildroot/output/images/rootfs.cpio ./build/
}

function do_linux
{
    cp -f assets/system/configs/linux.config $SRC_DIR/linux/.config
    export PATH="$SRC_DIR/buildroot/output/host/bin:$PATH"
    export CROSS_COMPILE=riscv32-buildroot-linux-gnu-
    export ARCH=riscv
    pushd $SRC_DIR/linux
    ASSERT make olddefconfig
    ASSERT make $PARALLEL
    popd
    cp -f $SRC_DIR/linux/arch/riscv/boot/Image ./build/
}

do_buildroot && OK
do_linux && OK
