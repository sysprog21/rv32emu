#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

# Overwrite the variable to use initrd rootfs when explicitly requested.
SPECIFY_ROOTFS=${1:-}
if [[ -n "${SPECIFY_ROOTFS}" && "${SPECIFY_ROOTFS}" != "initrd" ]]; then
    print_error "Unsupported rootfs for virtio-snd test: ${SPECIFY_ROOTFS}"
    exit 2
fi
if [[ "${SPECIFY_ROOTFS}" == "initrd" ]]; then
    OPTS_BASE_ROOTFS_EXT4="${OPTS_BASE}"
fi

# Ensure a failed/timeout test does not leave rv32emu running.
register_cleanup cleanup_emulator

RET=0

MESSAGES=(
    "${COLOR_G}OK!"
    "${COLOR_R}Fail to boot"
    "${COLOR_R}Fail to login"
    "${COLOR_R}Fail to bind virtio-snd driver"
    "${COLOR_R}Fail to enumerate ALSA card"
    "${COLOR_R}Fail to enumerate ALSA PCM device"
    "${COLOR_R}Fail to play PCM with speaker-test"
)

# GitHub Actions runners do not provide a physical audio output device.
# Use the ALSA null PCM as the default output so PortAudio can exercise
# the virtio-snd playback path in headless CI.
if [[ "${CI:-}" == "true" && "$(uname -s)" == "Linux" ]]; then
    cat > "${HOME}/.asoundrc" << 'EOF'
pcm.!default {
    type null
}
EOF
fi

RUN_LINUX="build/rv32emu ${OPTS_BASE_ROOTFS_EXT4} -x vsnd"

printf "${COLOR_Y}===== Test option: ${OPTS_BASE_ROOTFS_EXT4} -x vsnd =====${COLOR_N}\n"

ASSERT expect <<- DONE
	set timeout ${TIMEOUT}

	spawn ${RUN_LINUX}

	expect {
	    "buildroot login:" {
	        send "root\r"
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

	send "readlink /sys/bus/virtio/devices/virtio0/driver\r"
	expect {
	    "virtio_snd" {}
	    timeout {
	        exit 3
	    }
	}

	expect "# "
	send "cat /proc/asound/cards\r"
	expect {
	    "VirtIO SoundCard" {}
	    timeout {
	        exit 4
	    }
	}

	expect "# "
	send "aplay -l\r"
	expect {
	    "VirtIO SoundCard" {}
	    timeout {
	        exit 5
	    }
	}

	# Exercise the PCM playback path with a bounded single loop. Use an
	# explicitly sized buffer to tolerate scheduling jitter on shared CI runners.
	expect "# "
	set timeout 60
	send "speaker-test -D hw:0,0 -c 1 -r 48000 -F S16_LE -t sine -b 1200000 -p 300000 -P 4 -l 1; echo SPEAKER_TEST_RC:\\\$?\r"

	expect {
	    "Playback open error:" {
	        exit 6
	    }
	    "Setting of hwparams failed:" {
	        exit 6
	    }
	    "Setting of swparams failed:" {
	        exit 6
	    }
	    "Transfer failed:" {
	        exit 6
	    }
	    "0 - Mono" {}
	    timeout {
	        exit 6
	    }
	}

	# One completed loop is sufficient to verify end-to-end PCM progress.
	expect {
	    "Time per period =" {}
	    "Transfer failed:" {
	        exit 6
	    }
	    timeout {
	        exit 6
	    }
	}

	expect {
	    "SPEAKER_TEST_RC:0" {}
	    -re {SPEAKER_TEST_RC:([1-9][0-9]*)} {
	        exit 6
	    }
	    timeout {
	        exit 6
	    }
	}

	expect "# "
	send "\x01"
	send "x"
DONE

ret=$?
RET=$((${RET} + ${ret}))
cleanup

printf "\nVirtio-snd Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"

exit ${RET}
