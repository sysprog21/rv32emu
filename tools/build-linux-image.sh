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

OUTPUT_DIR=./build/linux-image/
mkdir -p $OUTPUT_DIR

BR_PKG_RTC_DIR=./tests/system/br_pkgs/rtc

# RTC
BR_RTC_PKG_DIR=./tests/system/br_pkgs/rtc

# Doom
BR_DOOM_PKG_DIR=./tests/system/br_pkgs/doom_riscv

# Quake
BR_QUAKE_PKG_DIR=./tests/system/br_pkgs/quake

function create_br_pkg_config()
{
    local pkg_name=$1
    local output_path=$2

    cat << EOF > "${output_path}"
config BR2_PACKAGE_${pkg_name^^}
    bool "${pkg_name}"
    help
        ${pkg_name} package.
EOF
}

function create_br_pkg_makefile()
{
    local pkg_name=$1
    local output_path=$2
    local output_bin_prefix=${3-}
    local makefile_prefix=${4-}
    local artifact=${5-}

    cat << EOF > "${output_path}"
################################################################################
#
# ${pkg_name} package
#
################################################################################

${pkg_name^^}_VERSION = 1.0
${pkg_name^^}_SITE = package/${pkg_name}/src
${pkg_name^^}_SITE_METHOD = local

define ${pkg_name^^}_BUILD_CMDS
	\$(MAKE) CROSS="\$(TARGET_CROSS)" CC="\$(TARGET_CC)" LD="\$(TARGET_LD)" -C \$(@D)/${makefile_prefix}
endef

define ${pkg_name^^}_INSTALL_TARGET_CMDS
	\$(INSTALL) -D -m 0755 \$(@D)/${output_bin_prefix}/${pkg_name} \$(TARGET_DIR)/usr/bin
	cp -a \$(@D)/${artifact} \$(TARGET_DIR)/root
endef

\$(eval \$(generic-package))
EOF
}

function create_br_pkg_src()
{
    local pkg_name=$1
    local src_c_file=$2
    local output_path=$3
    local mk_output_path=$3/Makefile
    local src_output_path=$3/$pkg_name.c

    # Create src directory
    mkdir -p ${output_path}

    # Create makefile
    # the output binary is in lowercase
    cat << EOF > "${mk_output_path}"
all:
	\$(CC) ${pkg_name}.c -o ${pkg_name}
EOF

    # moving C source file
    cp -f ${src_c_file} ${src_output_path}
}

function update_br_pkg_config()
{
    local pkg_name=$1
    local br_pkg_config_file="${SRC_DIR}/buildroot/package/Config.in"
    local source_line="    source \"package/${pkg_name}/Config.in\""

    # Only append if this package's isn't already present in the menu
    if ! grep -q "/${pkg_name}/" "${br_pkg_config_file}"; then
        sed -i '/^menu "Custom packages"/,/^endmenu$/{
            /^endmenu$/i\
'"${source_line}"'
        }' "${br_pkg_config_file}"
    fi
}

function do_patch_doom
{
    # Need to sed -i --specs=nano.spec to avoid nanolibc and -Bstatic to avoid static linking
    sed -i 's/--specs=nano\.specs//g' ${BR_DOOM_PKG_DIR}/src/riscv/Makefile
    sed -i 's/-Bstatic//g' ${BR_DOOM_PKG_DIR}/src/riscv/Makefile
    # rename output binary from doom-riscv.elf to doom
    sed -i 's/doom-riscv\.elf/doom/g' ${BR_DOOM_PKG_DIR}/src/riscv/Makefile

    local pkg_name="doom"

    mkdir -p ${SRC_DIR}/buildroot/package/${pkg_name}

    # download and unzip Doom artifact(DOOM1.WAD) to buildroot Doom package src
    if [[ ! -f shareware_doom_iwad.zip ]]; then
        wget \
            --user-agent="Mozilla/5.0 (X11; Linux x86_64; rv:121.0) Gecko/20100101 Firefox/121.0" \
            --referer="https://www.doomworld.com/" \
            --show-progress \
            --continue \
            http://www.doomworld.com/3ddownloads/ports/shareware_doom_iwad.zip
        unzip -d ${SRC_DIR}/buildroot/package/${pkg_name}/src shareware_doom_iwad.zip
    fi

    create_br_pkg_config ${pkg_name} ${SRC_DIR}/buildroot/package/${pkg_name}/Config.in
    create_br_pkg_makefile ${pkg_name} ${SRC_DIR}/buildroot/package/${pkg_name}/${pkg_name}.mk "riscv" "riscv" "DOOM1.WAD"
    # cp Doom submodule's src to buildroot Doom package src
    mkdir -p ${SRC_DIR}/buildroot/package/${pkg_name}/src
    cp -rf ${BR_DOOM_PKG_DIR}/src ${SRC_DIR}/buildroot/package/${pkg_name}/

    update_br_pkg_config ${pkg_name}
}

function do_patch_quake
{
    local pkg_name="quake"

    mkdir -p ${SRC_DIR}/buildroot/package/${pkg_name}

    # download and unzip Quake artifact(id1/pak0.pak) to buildroot Quake package src
    if [[ ! -f quakesw-1.0.6.zip ]]; then
        wget -q --show-progress --continue https://www.libsdl.org/projects/quake/data/quakesw-1.0.6.zip
        unzip -d ${SRC_DIR}/buildroot/package/${pkg_name}/src quakesw-1.0.6.zip
    fi

    create_br_pkg_config ${pkg_name} ${SRC_DIR}/buildroot/package/${pkg_name}/Config.in
    create_br_pkg_makefile ${pkg_name} ${SRC_DIR}/buildroot/package/${pkg_name}/${pkg_name}.mk "port/boards/rv32emu" "" "id1/"
    # cmake to generate Makefile
    cd ${BR_QUAKE_PKG_DIR}
    cmake -DCMAKE_TOOLCHAIN_FILE=./port/boards/rv32emu/toolchain.cmake \
        -DCROSS_COMPILE=riscv32-buildroot-linux-gnu- \
        -DCMAKE_BUILD_TYPE=RELEASE -DBOARD_NAME=rv32emu .
    cd -
    # cp Quake submodule's src to buildroot Quake package src
    mkdir -p ${SRC_DIR}/buildroot/package/${pkg_name}/src
    cp -rf ${BR_QUAKE_PKG_DIR}/* ${SRC_DIR}/buildroot/package/${pkg_name}/src

    update_br_pkg_config ${pkg_name}
}

# This function patches the packages when building the rootfs.cpio from scratch
function do_patch_buildroot
{
    local br_pkg_config_file="${SRC_DIR}/buildroot/package/Config.in"

    # Only append if the custom packages menu block isn't already present in the menu
    if ! grep -q "Custom packages" "${br_pkg_config_file}"; then
        cat << EOF >> "${br_pkg_config_file}"
menu "Custom packages"
endmenu
EOF
    fi

    # RTC self-contained C files
    for c in $(find ${BR_RTC_PKG_DIR} -type f); do
        local basename="$(basename ${c})"
        local pkg_name="${basename%.*}"

        mkdir -p ${SRC_DIR}/buildroot/package/${pkg_name}

        create_br_pkg_config ${pkg_name} ${SRC_DIR}/buildroot/package/${pkg_name}/Config.in
        create_br_pkg_makefile ${pkg_name} ${SRC_DIR}/buildroot/package/${pkg_name}/${pkg_name}.mk
        create_br_pkg_src ${pkg_name} ${c} ${SRC_DIR}/buildroot/package/${pkg_name}/src

        update_br_pkg_config ${pkg_name}
    done

    do_patch_doom

    do_patch_quake
}

function do_buildroot
{
    cp -f assets/system/configs/buildroot.config ${SRC_DIR}/buildroot/.config
    cp -f assets/system/configs/busybox.config ${SRC_DIR}/buildroot/busybox.config
    # Otherwise, the error below raises:
    #   You seem to have the current working directory in your
    #   LD_LIBRARY_PATH environment variable. This doesn't work.
    unset LD_LIBRARY_PATH
    do_patch_buildroot
    pushd ${SRC_DIR}/buildroot
    ASSERT make olddefconfig
    ASSERT make ${PARALLEL}
    popd
    cp -f ${SRC_DIR}/buildroot/output/images/rootfs.cpio ${OUTPUT_DIR}
}

function do_linux
{
    cp -f assets/system/configs/linux.config ${SRC_DIR}/linux/.config
    export PATH="${SRC_DIR}/buildroot/output/host/bin:${PATH}"
    export CROSS_COMPILE=riscv32-buildroot-linux-gnu-
    export ARCH=riscv
    pushd ${SRC_DIR}/linux
    ASSERT make olddefconfig
    ASSERT make ${PARALLEL}
    popd
    cp -f ${SRC_DIR}/linux/arch/riscv/boot/Image ${OUTPUT_DIR}
}

function do_simplefs
{
    pushd $SRC_DIR/simplefs
    ASSERT make KDIR=$SRC_DIR/linux $PARALLEL
    popd
    cp -f $SRC_DIR/simplefs/simplefs.ko $OUTPUT_DIR
}

do_buildroot && OK
do_linux && OK
do_simplefs && OK
