/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

/* To terminate the main loop of CPU */
void indirect_rv_halt();

/* Report whether a VM instance is still alive */
int indirect_rv_alive();

/* To cleanup the previous VM instance before starting a new one */
void indirect_rv_cleanup();

/* Report whether the current halt was explicitly requested by the user. */
int indirect_rv_stop_requested();

/* Reset static interpreter state between VM lifecycles. */
void reset_rv_run_state();

#if RV32_HAS(SYSTEM_MMIO)
/* To bridge xterm.js terminal with UART */
extern uint8_t input_buf_size;

char *get_input_buf();
uint8_t get_input_buf_cap();
void set_input_buf_size(uint8_t size);
uint8_t get_input_buf_size();
void u8250_put_rx_char(uint8_t c);
void u8250_reset_input_buffer();
#endif

/* To enable/disable run button in index.html to prevent re-execution
 * when the process is already running.
 */
void enable_run_button();
void disable_run_button();
void report_run_completion();
#endif
