/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#ifndef RV32EMU_H
#define RV32EMU_H

#include <stddef.h>
#include <stdint.h>

/* Keep the public entry points visible when the core is built hidden. */
#if defined(__GNUC__) || defined(__clang__)
#define RV32EMU_API __attribute__((visibility("default")))
#else
#define RV32EMU_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Native user-mode ELF execution. ISA extensions are selected at build time.
 * Only one live instance is supported per process. Serialize all API calls;
 * calls are neither thread-safe nor reentrant. Do not mix with internal rv_*
 * APIs. Guest syscalls access host files and streams: this is not a sandbox.
 */
typedef struct rv32emu rv32emu_t;

typedef enum {
    RV32EMU_OK = 0,
    RV32EMU_HALTED,
    RV32EMU_INVALID_ARGUMENT,
    RV32EMU_BUSY,
    RV32EMU_CREATE_FAILED,
    RV32EMU_INVALID_STATE
} rv32emu_status_t;

typedef struct {
    /* Bytes, 16 KiB through 4 GiB, a multiple of 4 KiB and the host page size;
     * default 64 MiB.
     */
    uint64_t memory_size;
    int argc; /* 0 supplies the ELF path as argv[0]. */
    const char *const *argv;
} rv32emu_options_t;

/* Initialize options before overriding fields; NULL options selects defaults.
 * Options and argument strings are borrowed only for the duration of create.
 * The current argument limit is 128 strings totaling 2048 bytes including NULs.
 * Recompile clients when updating the library; no stable binary ABI is
 * promised.
 */
RV32EMU_API void rv32emu_options_init(rv32emu_options_t *options);

/* Load an ELF and prepare its stack. On failure, *out is NULL. A failed create
 * releases its resources and permits retry. Invalid ELF input never exits the
 * host process. The ELF path must name a readable, supported RV32 executable.
 * Creation sets the process-wide log level to errors only.
 */
RV32EMU_API rv32emu_status_t rv32emu_create(const char *elf_path,
                                            const rv32emu_options_t *options,
                                            rv32emu_t **out);
RV32EMU_API void rv32emu_destroy(rv32emu_t *emu); /* NULL is allowed. */

/* Run one slice with a positive instruction budget <= INT_MAX. Execution may
 * overshoot to finish a translated block; this is not exact single-stepping or
 * a wall-clock deadline.
 *
 * Returns OK when yielded or HALTED when stopped. A halted instance stays
 * halted. Host I/O may block during a slice.
 */
RV32EMU_API rv32emu_status_t rv32emu_step(rv32emu_t *emu, uint32_t budget);
RV32EMU_API rv32emu_status_t rv32emu_halt(rv32emu_t *emu);

/* Returns INVALID_STATE until halted. Manual halt leaves the exit code at 0; an
 * internal fatal error halts with EXIT_FAILURE.
 */
RV32EMU_API rv32emu_status_t rv32emu_exit_code(const rv32emu_t *emu,
                                               int *exit_code);

/* Register numbers are architectural x0..x31 (x0..x15 for an RV32E build).
 * Writes to x0 succeed without changing it. PC must meet ISA alignment.
 */
RV32EMU_API rv32emu_status_t rv32emu_get_reg(const rv32emu_t *emu,
                                             unsigned reg,
                                             uint32_t *value);
RV32EMU_API rv32emu_status_t rv32emu_set_reg(rv32emu_t *emu,
                                             unsigned reg,
                                             uint32_t value);
RV32EMU_API rv32emu_status_t rv32emu_get_pc(const rv32emu_t *emu,
                                            uint32_t *value);
RV32EMU_API rv32emu_status_t rv32emu_set_pc(rv32emu_t *emu, uint32_t value);

/* Copy bytes with bounds checking. No partial copies on error. A zero size
 * permits NULL data and addresses through the end of RAM. Writes are allowed
 * only before the first step, so translated code cannot become stale.
 */
RV32EMU_API rv32emu_status_t rv32emu_read_memory(const rv32emu_t *emu,
                                                 uint32_t address,
                                                 void *data,
                                                 size_t size);
RV32EMU_API rv32emu_status_t rv32emu_write_memory(rv32emu_t *emu,
                                                  uint32_t address,
                                                  const void *data,
                                                  size_t size);
RV32EMU_API const char *rv32emu_status_string(rv32emu_status_t status);

#ifdef __cplusplus
}
#endif
#endif
