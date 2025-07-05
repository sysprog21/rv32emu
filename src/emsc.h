/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <emscripten.h>

void indirect_rv_halt();

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
extern uint8_t input_buf_size;

char *get_input_buf();
uint8_t get_input_buf_cap();
void set_input_buf_size(uint8_t size);
#endif
