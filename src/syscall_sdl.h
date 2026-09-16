/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Entry points implemented in syscall_sdl.c and called from the syscall
 * dispatcher, the emulation loop, and the UART device.
 *
 * These were previously declared with a local `extern` at each use site, in
 * three different files, and two of those spelled sdl_video_audio_cleanup with
 * an empty parameter list, which disables argument checking. Declaring them
 * once here lets the compiler check the definitions against the uses.
 */

#pragma once

#include "feature.h"

#if RV32_HAS(SDL)

#include "riscv.h"

void syscall_draw_frame(riscv_t *rv);
void syscall_setup_queue(riscv_t *rv);
void syscall_submit_queue(riscv_t *rv);
void syscall_setup_audio(riscv_t *rv);
void syscall_control_audio(riscv_t *rv);

/* Tear down the SDL window and mixer. Called when the guest exits through the
 * built-in exit syscall, and when the UART sees ctrl-c.
 */
void sdl_video_audio_cleanup(void);

#endif /* RV32_HAS(SDL) */
