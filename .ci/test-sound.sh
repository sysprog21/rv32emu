#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

RET=0

register_cleanup cleanup_emulator

TIMEOUT=${BOOT_TIMEOUT:-60}

COLOR_G='\e[32;01m' # Green
COLOR_R='\e[31;01m' # Red
COLOR_Y='\e[33;01m' # Yellow
COLOR_N='\e[0m'     # No color

MESSAGES=("${COLOR_G}OK!"
    "${COLOR_R}Fail to boot"
    "${COLOR_R}Fail to login"
    "${COLOR_R}Fail to bind virtio-snd driver"
    "${COLOR_R}Fail to enumerate ALSA card"
    "${COLOR_R}Fail to enumerate ALSA PCM device"
)

OPTS_BASE=" -k build/linux-image/Image -i build/linux-image/rootfs.cpio"
RUN_LINUX="build/rv32emu ${OPTS_BASE} -x vsnd"

printf "${COLOR_Y}===== Test option: ${OPTS_BASE} -x vsnd =====${COLOR_N}\n"

expect <<- DONE
	set timeout ${TIMEOUT}

	spawn ${RUN_LINUX}

	expect {
	    "buildroot login:" {
	        send "root\\r"
	    }
	    timeout {
	        exit 1
	    }
	}

	expect {
	    "# " {}
	    timeout {
	        exit 2
	    }
	}

	send "readlink /sys/bus/virtio/devices/virtio0/driver\\r"
	expect {
	    "virtio_snd" {}
	    timeout {
	        exit 3
	    }
	}

	expect "# "
	send "cat /proc/asound/cards\\r"
	expect {
	    "VirtIO SoundCard" {}
	    timeout {
	        exit 4
	    }
	}

	expect "# "
	send "aplay -l\\r"
	expect {
	    "VirtIO SoundCard" {
	        send "\\x01"
	        send "x"
	        exit 0
	    }
	    timeout {
	        exit 5
	    }
	}
DONE

ret=$?
RET=$((${RET} + ${ret}))
cleanup

printf "\nVirtio-snd Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"

exit ${RET}
