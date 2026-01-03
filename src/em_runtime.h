/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

/* To terminate the main loop of CPU */
void indirect_rv_halt();

#if RV32_HAS(SYSTEM_MMIO)
/* To bridge xterm.js terminal with UART */
extern uint8_t input_buf_size;

char *get_input_buf();
uint8_t get_input_buf_cap();
void set_input_buf_size(uint8_t size);
#endif

/* To enable/disable run button in index.html to prevent re-execution
 * when the process is already running.
 */
void enable_run_button();
void disable_run_button();
#endif
