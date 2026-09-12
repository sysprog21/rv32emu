#!/usr/bin/env bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

check_platform

RET=0

backend="${VNET_BACKEND:-tap}"

if [[ "${backend}" != "tap" && "${backend}" != "user" &&
    "${backend}" != "vmnet" ]]; then
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
    vmnet)
        if [[ "${OS_TYPE}" != "Darwin" ]]; then
            print_warning "Skipping virtio-net vmnet test on non-macOS host"
            exit 0
        fi

        if ! sudo -n true 2> /dev/null; then
            print_error "vmnet test requires passwordless sudo"
            exit 2
        fi

        run_prefix="sudo -E"
        guest_ip=""
        gateway_ip=""
        test_name="vmnet shared"
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
	set guest_ip "${guest_ip}"
	set gateway_ip "${gateway_ip}"

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

	            exec sudo ip addr replace \$gateway_ip/24 dev \$tap_if
	            exec sudo ip link set \$tap_if up
	        }

	        if { "${backend}" == "vmnet" } {
	            set gateway_ip [exec sh -c {
	                for bridge in \$(ifconfig -l); do
	                    case "\$bridge" in
	                        bridge[0-9]*)
	                            ip=\$(ifconfig "\$bridge" 2>/dev/null | awk '/inet / {print \$2; exit}')
	                            if [ -n "\$ip" ]; then
	                                echo "\$ip"
	                                exit 0
	                            fi
	                            ;;
	                    esac
	                done
	                exit 1
	            }]

	            set octets [split \$gateway_ip "."]
	            set guest_ip "[lindex \$octets 0].[lindex \$octets 1].[lindex \$octets 2].10"

	            puts "vmnet gateway: \$gateway_ip"
	            puts "vmnet guest IP: \$guest_ip"
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
	send "ip addr add \$guest_ip/24 dev eth0\\r"

	expect "# "
	send "ip addr show eth0\\r"
	expect {
	    "\$guest_ip/24" {}
	    timeout { exit 3 }
	}

	expect "# "
	send "ping -c 3 -W 5 \$gateway_ip\\r"
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
