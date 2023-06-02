#!/usr/bin/env bash

source tests/common.sh

S=tests/cc

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
