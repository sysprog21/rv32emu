#pragma once
#include <stdio.h>
#include <map>

#include "io.h"
#include "riscv.h"

// state structure passed to the VM
struct state_t {
    memory_t mem;

    // the data segment break address
    riscv_word_t break_addr;

    // file descriptor map
    std::map<int, FILE *> fd_map;
};

// main syscall handler
extern void syscall_handler(struct riscv_t *);
