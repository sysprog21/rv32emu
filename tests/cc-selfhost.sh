#!/usr/bin/env bash

function fail()
{
    echo "*** Fail"
    exit 1
}

O=build
S=tests/cc
RUN=$O/rv32emu

if [ ! -f $RUN ]; then
    echo "No build/rv32emu found!"
    exit 1
fi

echo "Generating cross compiler..."
cat $S/stdlib.c $S/emit.c $S/cc.c | cc -o $O/cc-native -x c - || fail
echo "Generating native compiler..."
cat $S/stdlib.c $S/emit.c $S/cc.c | $O/cc-native > $O/cc.elf || fail
echo "Self-hosting C compiler..."
cat $S/stdlib.c $S/emit.c $S/cc.c | $RUN $O/cc.elf > out.elf || fail
echo "Build 'hello' program with the self-hosting compiler..."
cat $S/stdlib.c $S/hello.c | $RUN out.elf > hello.elf || fail
echo "Executing the compiled program..."
$RUN hello.elf
rm -f out.elf hello.elf $O/cc-native
