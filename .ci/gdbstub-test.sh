#!/usr/bin/env bash

set -e -u -o pipefail

export PATH=$(pwd)/toolchain/bin:$PATH

# The test assembles its own guest and drives it with the matching GDB, so it
# needs a gcc and gdb pair from one RISC-V toolchain.
GDB=
CC=
prefixes=("${CROSS_COMPILE:-}" "riscv32-unknown-elf-" "riscv-none-elf-")
for prefix in "${prefixes[@]}"; do
    # Skip empty prefix to avoid matching host tools
    [ -z "${prefix}" ] && continue
    if command -v "${prefix}gdb" > /dev/null 2>&1 \
        && command -v "${prefix}gcc" > /dev/null 2>&1; then
        GDB=${prefix}gdb
        CC=${prefix}gcc
        break
    fi
done
if [ -z "${GDB}" ]; then
    echo "No RISC-V gcc and gdb pair found"
    exit 1
fi

# The guest's vector accesses need an EXT_V emulator; leave them out otherwise.
ARCH=rv32ima
RVV=
if grep -q '^CONFIG_EXT_V=y' .config 2> /dev/null; then
    ARCH=rv32imav
    RVV=-DWITH_RVV
fi
GUEST=build/gdbstub-guest
${CC} -march=${ARCH} -mabi=ilp32 -nostdlib -static -fno-pie -no-pie \
    -Wl,-e,_start ${RVV} -o ${GUEST} tests/gdbstub.S

build/rv32emu -g ${GUEST} &
PID=$!

# Before starting GDB, we should ensure rv32emu is still running.
if ! ps -p $PID > /dev/null; then
    exit 1
fi

tmpfile=/tmp/rv32emu-gdbstub.$PID

OPTS="-ex 'set output-radix 16' "
OPTS+="-ex 'file ${GUEST}' "
OPTS+="-ex 'target remote :1234' "

# Instruction breakpoints: stop at each in turn, read a register at the first,
# then single-step off the last.
breakpoints=(bp_1 bp_2 bp_3)
for b in "${breakpoints[@]}"; do
    OPTS+="-ex 'break *${b}' "
done
OPTS+="-ex 'break *after_copy' "
for b in "${breakpoints[@]}"; do
    OPTS+="-ex 'continue' "
    OPTS+="-ex 'info symbol \$pc' "
    [ "${b}" = bp_1 ] && OPTS+="-ex 'print/x \$t4' "
done
OPTS+="-ex 'stepi' "
OPTS+="-ex 'info symbol \$pc' "

# Register and memory writes, read back from guest memory after the guest has
# copied them; a plain memory read of static data alongside.
OPTS+="-ex 'set \$t3 = 0xcafe' "
OPTS+="-ex 'set {int}&mem_in = 0xbeef' "
OPTS+="-ex 'print/x *(int *)&r_scalar' "
OPTS+="-ex 'continue' "
OPTS+="-ex 'info symbol \$pc' "
OPTS+="-ex 'print/x *(int *)&reg_out' "
OPTS+="-ex 'print/x *(int *)&mem_out' "
OPTS+="-ex 'delete' "

# Data watchpoints on every guest access path: scalar, misaligned (emulated
# with narrower accesses), AMO (a read then a write) and vector loads and
# stores. Each case arms one watchpoint, runs to its hit, and records where it
# stopped.
cases=(
    "watch w_scalar after_sw New value = 0x1"
    "rwatch r_scalar after_lw Value = 0x12345678"
    "watch ms_hi after_misaligned_sw New value = 0x1122"
    "rwatch ml_hi after_misaligned_lw Value = 0x5a5a5a5a"
    "awatch amo_var after_amo New value = 0x5"
)
if [ -n "${RVV}" ]; then
    cases+=(
        "rwatch vsrc1 after_vle Value = 0x2"
        "watch vdst1 after_vse New value = 0x2"
    )
fi
for c in "${cases[@]}"; do
    read -r cmd sym _ <<< "${c}"
    OPTS+="-ex '${cmd} *(int *)&${sym}' "
    OPTS+="-ex 'continue' "
    OPTS+="-ex 'info symbol \$pc' "
    OPTS+="-ex 'delete' "
done
# Run the guest to its exit
OPTS+="-ex 'continue' "

eval "${GDB} --batch ${OPTS}" > ${tmpfile}

pass()
{
    echo "gdbstub test: $1 OK"
}
fail()
{
    echo "gdbstub test: $1 FAILED: $2 (log: ${tmpfile})"
    exit 1
}
expect()
{
    grep -Eq "$2" ${tmpfile} || fail "$1" "$2"
}

for b in "${breakpoints[@]}" after_copy; do
    expect "breakpoint + continue" "^${b} in section .text"
done
pass "breakpoint + continue"

expect "stepi" "^after_stepi in section .text"
pass "stepi"

expect "register read" '^\$[0-9]+ = 0x600d$'
expect "register write" '^\$[0-9]+ = 0xcafe$'
pass "register read/write"

expect "memory read" '^\$[0-9]+ = 0x12345678$'
expect "memory write" '^\$[0-9]+ = 0xbeef$'
pass "memory read/write"

# The stub reports a hit once the access has completed. GDB expects a RISC-V
# target to report it before the instruction runs, so it single-steps once more
# before stopping: the stop lands one instruction past the label.
for c in "${cases[@]}"; do
    read -r cmd sym label expected <<< "${c}"
    expect "watchpoint (${cmd} ${sym})" "watchpoint .*${sym}"
    expect "watchpoint (${cmd} ${sym})" "^${label}( \+ 4)? in section .text"
    expect "watchpoint (${cmd} ${sym})" "^${expected}"
done
pass "watchpoint (scalar, misaligned, AMO${RVV:+, vector})"

# Pass and wait
wait $PID
exit 0
