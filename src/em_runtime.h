/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

/* To terminate the main loop of CPU */
void indirect_rv_halt(void);

/* Report whether a VM instance is still alive */
int indirect_rv_alive(void);

/* To cleanup the previous VM instance before starting a new one */
void indirect_rv_cleanup(void);

/* Report whether the current halt was explicitly requested by the user. */
int indirect_rv_stop_requested(void);

/* Reset static interpreter state between VM lifecycles. */
void reset_rv_run_state(void);

#if RV32_HAS(SYSTEM_MMIO)
/* To bridge xterm.js terminal with UART */
extern uint8_t input_buf_size;

char *get_input_buf(void);
uint8_t get_input_buf_cap(void);
void set_input_buf_size(uint8_t size);
uint8_t get_input_buf_size(void);
void u8250_put_rx_char(uint8_t c);
void u8250_reset_input_buffer(void);
#endif

/* Run step(arg) as the browser main loop, at full speed while the page is
 * visible and once per animation frame while it is hidden. Does not return.
 */
void em_run_main_loop(em_arg_callback_func step, void *arg);

/* Cancel the loop started by em_run_main_loop(). */
void em_stop_main_loop(void);

/* To enable/disable run button in index.html to prevent re-execution
 * when the process is already running.
 */
void enable_run_button(void);
void disable_run_button(void);
void report_run_completion(void);
#endif
