#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The scheduler state is process-wide: one coro_init() may be active at a time,
 * so only one system-mode emulator can run per process. A second coro_init()
 * fails instead of corrupting the first.
 */

/* Initialize the coroutine subsystem.
 * @total_slots: number of coroutine slots, one per hart
 */
bool coro_init(uint32_t total_slots);

/* Cleanup coroutine subsystem */
void coro_cleanup(void);

/* Create the coroutine for a hart.
 * @slot_id: coroutine slot index, in [0, total_slots)
 * @func: entry point (the hart's run loop)
 * @arg: user data passed to func
 */
bool coro_create_hart(uint32_t slot_id, void (*func)(void *), void *arg);

/* Resume execution of a hart coroutine */
void coro_resume_hart(uint32_t slot_id);

/* Suspend the current hart and return to whoever resumed it */
void coro_yield(void);

/* Abandon the current hart stack and restart its entry function on resume. */
bool coro_restart_current(void);
