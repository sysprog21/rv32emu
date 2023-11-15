#!/usr/bin/env bash
set -e -u -o pipefail

# check the existence of the clang toolchain
command -v clang &> /dev/null

# compile
make clean
clang \
    -g -O1 \
    -fsanitize=fuzzer,address,undefined \
    -include src/common.h \
    -D RV32_FEATURE_EXT_F=0 \
    -D RV32_FEATURE_SDL=0 \
    -D DEFAULT_STACK_ADDR=0xFFFFE000 \
    -D DEFAULT_ARGS_ADDR=0xFFFFF000 \
    -D FUZZER \
    -o build/rv32emu_fuzz \
       src/fuzz-target.cc \
       src/map.c \
       src/utils.c \
       src/decode.c \
       src/io.c \
       src/syscall.c \
       src/emulate.c \
       src/riscv.c \
       src/elf.c \
       src/cache.c \
       src/mpool.c \
       src/main.c

# populate the initial CORPUS for the fuzzer using valid elf
mkdir -p build/fuzz/CORPUS_DIR
cp build/*.elf build/fuzz/CORPUS_DIR

# execute
./build/rv32emu_fuzz build/fuzz/CORPUS_DIR -timeout=3 -max_total_time=1200
