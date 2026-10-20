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

CI_SOUND_DIR=""
CI_SOUND_PID=""

cleanup_ci_audio()
{
    local attempt
    if [ -n "${CI_SOUND_PID}" ]; then
        kill "${CI_SOUND_PID}" 2> /dev/null || true
        for ((attempt = 0; attempt < 20; attempt++)); do
            kill -0 "${CI_SOUND_PID}" 2> /dev/null || break
            sleep 0.1
        done
        if kill -0 "${CI_SOUND_PID}" 2> /dev/null; then
            kill -KILL "${CI_SOUND_PID}" 2> /dev/null || true
        fi
        wait "${CI_SOUND_PID}" 2> /dev/null || true
        CI_SOUND_PID=""
    fi
    if [ -n "${CI_SOUND_DIR}" ]; then
        rm -rf -- "${CI_SOUND_DIR}"
        CI_SOUND_DIR=""
    fi
}

setup_ci_audio()
{
    local tool plugin_dir attempt
    for tool in pulseaudio pactl pkg-config; do
        command -v "${tool}" > /dev/null 2>&1 || {
            print_error "Headless audio requires ${tool}"
            return 1
        }
    done
    plugin_dir="${ALSA_PLUGIN_DIR:-$(pkg-config --variable=libdir alsa)/alsa-lib}"
    if [ ! -r "${plugin_dir}/libasound_module_pcm_pulse.so" ]; then
        print_error "Headless audio requires the ALSA PulseAudio plugin in ${plugin_dir}"
        return 1
    fi

    CI_SOUND_DIR=$(mktemp -d "${RUNNER_TEMP:-/tmp}/rv32emu-sound.XXXXXX") || return 1
    register_cleanup cleanup_ci_audio
    mkdir -m 700 "${CI_SOUND_DIR}/config" "${CI_SOUND_DIR}/runtime" "${CI_SOUND_DIR}/state" || return 1
    cat > "${CI_SOUND_DIR}/server.pa" << EOF
load-module module-native-protocol-unix socket="${CI_SOUND_DIR}/native" auth-anonymous=1
load-module module-null-sink sink_name=rv32emu-ci rate=48000 channels=1
set-default-sink rv32emu-ci
EOF
    cat > "${CI_SOUND_DIR}/asound.conf" << EOF
pcm.!default {
    type pulse
    server "unix:${CI_SOUND_DIR}/native"
    hint {
        show on
        description "rv32emu CI audio"
    }
}
EOF
    cat > "${CI_SOUND_DIR}/client.conf" << EOF
    autospawn = no
    default-server = unix:${CI_SOUND_DIR}/native
EOF
    export PULSE_SERVER="unix:${CI_SOUND_DIR}/native"
    export PULSE_CLIENTCONFIG="${CI_SOUND_DIR}/client.conf"
    export PULSE_COOKIE="${CI_SOUND_DIR}/cookie"
    export ALSA_CONFIG_PATH="${CI_SOUND_DIR}/asound.conf"
    export ALSA_PLUGIN_DIR="${plugin_dir}"
    XDG_CONFIG_HOME="${CI_SOUND_DIR}/config" \
        XDG_RUNTIME_DIR="${CI_SOUND_DIR}/runtime" \
        PULSE_RUNTIME_PATH="${CI_SOUND_DIR}/runtime" \
        PULSE_STATE_PATH="${CI_SOUND_DIR}/state" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=${CI_SOUND_DIR}/no-dbus" \
        pulseaudio -n --daemonize=no --fail=yes --use-pid-file=no --exit-idle-time=-1 \
        --file="${CI_SOUND_DIR}/server.pa" > "${CI_SOUND_DIR}/server.log" 2>&1 &
    CI_SOUND_PID=$!
    for ((attempt = 0; attempt < 50; attempt++)); do
        if [ "$(pactl get-default-sink 2> /dev/null)" = "rv32emu-ci" ] \
            && pactl list short sinks | awk '$2 == "rv32emu-ci" { found = 1 } END { exit !found }'; then
            return 0
        fi
        kill -0 "${CI_SOUND_PID}" 2> /dev/null || break
        sleep 0.1
    done
    print_error "Private headless audio server failed to start"
    cat "${CI_SOUND_DIR}/server.log" >&2
    return 1
}

# ALSA's null PCM has no sample clock. A private PulseAudio null sink paces
# headless CI playback and keeps each test's output isolated.
if [[ "${CI:-}" == "true" && "${OS_TYPE}" == "Linux" ]]; then
    setup_ci_audio || exit 1
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
