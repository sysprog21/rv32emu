#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

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

RUN_LINUX="build/rv32emu ${OPTS_BASE} -x vsnd"

printf "${COLOR_Y}===== Test option: ${OPTS_BASE} -x vsnd =====${COLOR_N}\n"

ASSERT expect <<- DONE
	set timeout ${TIMEOUT}

	spawn ${RUN_LINUX}

	expect {
	    "buildroot login:" {
	        send "root\n"
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

	send "readlink /sys/bus/virtio/devices/virtio0/driver\n"
	expect {
	    "virtio_snd" {}
	    timeout {
	        exit 3
	    }
	}

	expect "# "
	send "cat /proc/asound/cards\n"
	expect {
	    "VirtIO SoundCard" {}
	    timeout {
	        exit 4
	    }
	}

	expect "# "
	send "aplay -l\n"
	expect {
	    "VirtIO SoundCard" {}
	    timeout {
	        exit 5
	    }
	}

	# Exercise the PCM playback path with a known working configuration.
	expect "# "
	set timeout 60
	send "speaker-test -D hw:0,0 -c 1 -r 48000 -F S16_LE -t sine -l 2; echo SPEAKER_TEST_RC:\\\$?\n"

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

	# Verify that playback progresses through two complete test loops.
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
