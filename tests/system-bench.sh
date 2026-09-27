#!/usr/bin/env bash

# Boot the prebuilt Linux image and time guest workloads in host wall-clock
# time. Guest clocks derive from the emulated cycle count in system mode, so
# scores the guest prints itself (DMIPS, iterations per second) do not show how
# fast the emulator runs; the milliseconds reported here do.
#
# Usage:
#   tests/system-bench.sh [EMULATOR] [RUNS]
#
# EMULATOR defaults to build/rv32emu, built in system mode; RUNS to 1. The
# kernel and root file system come from build/linux-image, as fetched by "make
# artifact". Each run prints one line of name=milliseconds pairs.

set -e -u -o pipefail

EMU=${1:-build/rv32emu}
RUNS=${2:-1}
IMAGE=build/linux-image

command -v expect > /dev/null || {
    echo "expect is required" >&2
    exit 2
}
for f in "${EMU}" "${IMAGE}/Image" "${IMAGE}/rootfs.cpio"; do
    [ -e "${f}" ] || {
        echo "Missing ${f}" >&2
        exit 2
    }
done

for _ in $(seq "${RUNS}"); do
    expect -f - "${EMU}" "${IMAGE}" << 'EOF'
set emu [lindex $argv 0]
set image [lindex $argv 1]
log_user 0
set timeout 3600
set results {}
proc step {name cmd} {
    global results
    send "$cmd\n"
    set t0 [clock milliseconds]
    expect -re "\n# $" {} timeout { exit 1 }
    lappend results "$name=[expr {[clock milliseconds] - $t0}]"
}
set t0 [clock milliseconds]
spawn $emu -k $image/Image -i $image/rootfs.cpio
expect "buildroot login:" {} timeout { exit 1 }
lappend results "boot=[expr {[clock milliseconds] - $t0}]"
send "root\n"
expect -re "\n# $" {} timeout { exit 1 }
# Compute-bound programs shipped in the root file system.
step dhrystone "echo 20000000 | dhrystone > /dev/null"
step coremark "coremark 0x0 0x0 0x66 10000 7 1 2000 > /dev/null"
# Process creation: page tables, address-space switches and TLB flushes.
step fork-exec "i=0; while \[ \$i -lt 300 \]; do /bin/true; i=\$((i+1)); done"
# A pipeline through the kernel's pipes and page cache.
step gzip-md5 "dd if=/dev/zero bs=64k count=256 2>/dev/null | gzip -1 | md5sum > /dev/null"
send "\x01"; send "x"
expect eof
puts [join $results " "]
EOF
done
