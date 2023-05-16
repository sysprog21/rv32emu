#!/usr/bin/env bash

export PATH=`pwd`/toolchain/riscv/bin:$PATH

GDB=
prefixes=("${CROSS_COMPILE}" "riscv32-unknown-elf" "riscv-none-elf")
for prefix in "${prefixes[@]}"; do
    utility=${prefix}-gdb
    command -v "${utility}" &> /dev/null
    if [[ $? == 0 ]]; then
        GDB=${utility}
    fi
done

# Check if GDB is available
if [ -z ${GDB} ]; then
    exit 1
fi

build/rv32emu --gdbstub build/puzzle.elf &
PID=$!

# Before starting GDB, we should ensure rv32emu is still running.
if ! ps -p $PID > /dev/null; then
    exit 1
fi

OPTS=
tmpfile=/tmp/rv32emu-gdbstub.$PID
breakpoints=(0x10700 0x10800 0x10900)
bkpt_count=${#breakpoints[@]}
OPTS+="-ex 'file build/puzzle.elf' "
OPTS+="-ex 'target remote :1234' "
for t in ${breakpoints[@]}; do
    OPTS+="-ex 'break *$t' "
done
for ((i = 1; i <= $bkpt_count; i++)); do
    OPTS+="-ex 'continue' "
    OPTS+="-ex 'print \$pc' "
done
for ((i = 1; i <= $bkpt_count; i++)); do
    OPTS+="-ex 'del $i' "
done
OPTS+="-ex 'stepi' "
OPTS+="-ex 'stepi' "
OPTS+="-ex 'continue' "

eval "${GDB} --batch ${OPTS}" > ${tmpfile}

# check if we stop at the breakpoint
for ((i = 1; i <= $bkpt_count; i++)); do
    expected=$(grep -w "Breakpoint ${i} at" ${tmpfile} | awk {'print $4'})
    ans=$(grep "\$${i} =" ${tmpfile} | awk {'print $5'})
    if [ "$expected" != "$ans" ]; then
        exit 1 # Fail
    fi
done

# Pass and wait
wait $PID
exit 0
