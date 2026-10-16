#!/usr/bin/env bash

set -e -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"
check_platform

export ZEPHYR_OUT="${ZEPHYR_OUT:-build/zephyr}"
export RV32EMU="${RV32EMU:-build/rv32emu}"
export BOOT_TIMEOUT="${BOOT_TIMEOUT:-60}"
export SYNC_LINES="${SYNC_LINES:-38}"
mkdir -p "${ZEPHYR_OUT}/logs"
truncate -s 1M "${ZEPHYR_OUT}/dummy.img"
test -x "${RV32EMU}"
test -s "${ZEPHYR_OUT}/hello_world.bin"
test -s "${ZEPHYR_OUT}/synchronization.bin"

# Like the Linux boot checks, Expect supplies a PTY and bounded output checks.
# Kill only this spawned guest and reap it on success, timeout, or early EOF.
ASSERT expect << 'DONE'
set timeout $env(BOOT_TIMEOUT)
if {![string is integer -strict $timeout] || $timeout < 1 ||
    ![string is integer -strict $env(SYNC_LINES)] || $env(SYNC_LINES) < 2} {
    puts stderr "BOOT_TIMEOUT must be positive and SYNC_LINES must be >= 2"
    exit 1
}
proc finish {status message} {
    puts $message
    catch {exec kill -TERM [exp_pid]}
    after 200
    catch {exec kill -KILL [exp_pid]}
    catch {close}
    catch {wait}
    exit $status
}
foreach sample {hello_world synchronization} {
    log_file -noappend $env(ZEPHYR_OUT)/logs/$sample.log
    spawn $env(RV32EMU) -k $env(ZEPHYR_OUT)/$sample.bin \
        -x vblk:$env(ZEPHYR_OUT)/dummy.img,rootfs
    if {$sample eq "hello_world"} {
        expect {
            "Hello World! qemu_riscv32" {}
            timeout {finish 1 "hello_world: greeting timed out"}
            eof {finish 1 "hello_world: guest exited before greeting"}
        }
    } else {
        set count 0
        set previous ""
        expect {
            -re {thread_([ab]): Hello World[^\r\n]*\r*\n} {
                set current $expect_out(1,string)
                if {$current eq $previous} {
                    finish 1 "synchronization: repeated thread_$current"
                }
                set previous $current
                incr count
                if {$count < $env(SYNC_LINES)} {exp_continue -continue_timer}
            }
            timeout {finish 1 "synchronization: only $count alternating lines"}
            eof {finish 1 "synchronization: guest exited after $count lines"}
        }
        puts "synchronization: $count alternating lines"
    }
    catch {exec kill -TERM [exp_pid]}
    after 200
    catch {exec kill -KILL [exp_pid]}
    catch {close}
    catch {wait}
    log_file
    puts "$sample: PASS"
}
DONE
