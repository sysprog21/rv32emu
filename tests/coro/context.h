#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct context_fastest_s {
    uintptr_t ra;
    uintptr_t sp;
    uintptr_t s0;
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;

    void (*entry)(void *);
    void *data;
} context_t[1];

void initialize_context(context_t ctx,
                        void *stack_base,
                        size_t stack_size,
                        void (*entry)(void *data),
                        void *data);

void switch_context(context_t from, context_t to);
