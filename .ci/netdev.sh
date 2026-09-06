#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

RET=0

backend="${VNET_BACKEND:-tap}"

if [[ "${backend}" != "tap" && "${backend}" != "user" ]]; then
    print_error "Unsupported virtio-net backend for this test: ${backend}"
    exit 2
fi

case "${backend}" in
    tap)
        if [[ "${OS_TYPE}" != "Linux" ]]; then
            print_warning "Skipping virtio-net TAP test on non-Linux host"
            exit 0
        fi

        run_prefix="sudo -E"
        guest_ip="192.168.100.2"
        gateway_ip="192.168.100.1"
        test_name="TAP"
        ;;
    user)
        run_prefix=""
        guest_ip="10.0.2.15"
        gateway_ip="10.0.2.2"
        test_name="user-mode SLIRP"
        ;;
esac

register_cleanup cleanup_emulator

TIMEOUT=${NETDEV_BOOT_TIMEOUT:-${TIMEOUT}}
MESSAGES+=("${COLOR_R}Fail to ping gateway")
RUN_LINUX="${run_prefix} build/rv32emu ${OPTS_BASE} -x vnet:${backend}"

printf "${COLOR_Y}===== Test option: ${OPTS_BASE} -x vnet:${backend} =====${COLOR_N}\n"

ASSERT expect <<- DONE
	set timeout ${TIMEOUT}
	set tap_if ""

	spawn ${RUN_LINUX}

	expect {
	    -re {allocated TAP interface: (tap[0-9]+)} {
	        set tap_if \$expect_out(1,string)
	        exp_continue
	    }
	    "buildroot login:" {
	        if { "${backend}" == "tap" } {
	            if { "\$tap_if" == "" } {
	                set tap_if [exec sh -c {ip -o link show | awk -F': ' '\$2 ~ /^tap[0-9]+/ {print \$2; exit}'}]
	            }

	            exec sudo ip addr replace ${gateway_ip}/24 dev \$tap_if
	            exec sudo ip link set \$tap_if up
	        }

	        send "root\\r"
	    }
	    timeout {
	        exit 1
	    }
	}

	expect "# "
	send "readlink /sys/bus/virtio/devices/virtio0/driver\\r"
	expect {
	    "virtio_net" {}
	    timeout { exit 2 }
	}

	expect "# "
	send "ip link set eth0 up\\r"

	expect "# "
	send "ip addr flush dev eth0\\r"

	expect "# "
	send "ip addr add ${guest_ip}/24 dev eth0\\r"

	expect "# "
	send "ip addr show eth0\\r"
	expect {
	    "${guest_ip}/24" {}
	    timeout { exit 3 }
	}

	expect "# "
	send "ping -c 3 -W 5 ${gateway_ip}\\r"
	expect {
	    -re {3 packets transmitted, 3 packets received|3 packets transmitted, 3 received} {
	        send "\\x01"
	        send "x"
	        exit 0
	    }
	    -re {3 packets transmitted, 0 packets received|3 packets transmitted, 0 received|100% packet loss} {
	        exit 4
	    }
	    timeout {
	        exit 4
	    }
	}
DONE

ret=$?
RET=$((${RET} + ${ret}))
cleanup

printf "\nVirtio-net ${test_name} Test: [ ${MESSAGES[$ret]}${COLOR_N} ]\n"

exit ${RET}
