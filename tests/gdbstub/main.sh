#!/usr/bin/env bash

r=$'\r'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

build/rv32emu --gdbstub build/puzzle.elf &
PID=$!
# We should confirm rv32emu is still executing before running GDB
if ps -p $PID > /dev/null
then
    riscv32-unknown-elf-gdb -q -x tests/gdbstub/remote-commands.gdb > /tmp/gdbstub-test.txt

    # check if we stop at the breakpoint
    expected=$(grep -rw "Breakpoint 1 at" /tmp/gdbstub-test.txt | awk {'print $4'})
    ans=$(grep -r "$1 =" /tmp/gdbstub-test.txt | awk {'print $5'})
    if [ "$expected" != "$ans" ]; then
        echo -e "${r}$expected != $ans... ${RED}pass${NC}"
        exit 1
    fi
    echo -e "${r}$expected == $ans... ${GREEN}pass${NC}"
    exit 0
    wait $PID
fi

exit 1
