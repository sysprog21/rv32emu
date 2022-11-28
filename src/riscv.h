/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RISC-V registers (mnemonics, ABI names)
 *
 * There are 32 registers in RISC-V. The program counter is a further register
 * "pc" that is present.
 *
 * There is no dedicated register that is used for the stack pointer, or
 * subroutine return address. The instruction encoding allows any x register
 * to be used for that purpose.
 *
 * However the standard calling conventions uses "x1" to store the return
 * address of call,  with "x5" as an alternative link register, and "x2" as
 * the stack pointer.
 */
enum {
    rv_reg_zero = 0, /* hard-wired zero, ignoring any writes */
    rv_reg_ra,       /* return address */
    rv_reg_sp,       /* stack pointer */
    rv_reg_gp,       /* global pointer */
    rv_reg_tp,       /* thread pointer */
    rv_reg_t0,       /* temporary/alternate link register */
    rv_reg_t1,       /* temporaries */
    rv_reg_t2,
    rv_reg_s0, /* saved register/frame pointer */
    rv_reg_s1,
    rv_reg_a0, /* function arguments / return values */
    rv_reg_a1,
    rv_reg_a2, /* function arguments */
    rv_reg_a3,
    rv_reg_a4,
    rv_reg_a5,
    rv_reg_a6,
    rv_reg_a7,
    rv_reg_s2, /* saved register */
    rv_reg_s3,
    rv_reg_s4,
    rv_reg_s5,
    rv_reg_s6,
    rv_reg_s7,
    rv_reg_s8,
    rv_reg_s9,
    rv_reg_s10,
    rv_reg_s11,
    rv_reg_t3, /* temporary register */
    rv_reg_t4,
    rv_reg_t5,
    rv_reg_t6,
};

/* forward declaration for internal structure */
typedef struct riscv_internal riscv_t;
typedef void *riscv_user_t;

typedef uint32_t riscv_word_t;
typedef uint16_t riscv_half_t;
typedef uint8_t riscv_byte_t;
typedef uint32_t riscv_exception_t;
typedef float riscv_float_t;

/* memory read handlers */
typedef riscv_word_t (*riscv_mem_ifetch)(riscv_t *rv, riscv_word_t addr);
typedef riscv_word_t (*riscv_mem_read_w)(riscv_t *rv, riscv_word_t addr);
typedef riscv_half_t (*riscv_mem_read_s)(riscv_t *rv, riscv_word_t addr);
typedef riscv_byte_t (*riscv_mem_read_b)(riscv_t *rv, riscv_word_t addr);

/* memory write handlers */
typedef void (*riscv_mem_write_w)(riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_word_t data);
typedef void (*riscv_mem_write_s)(riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_half_t data);
typedef void (*riscv_mem_write_b)(riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_byte_t data);

/* system instruction handlers */
typedef void (*riscv_on_ecall)(riscv_t *rv);
typedef void (*riscv_on_ebreak)(riscv_t *rv);

/* RISC-V emulator I/O interface */
typedef struct {
    /* memory read interface */
    riscv_mem_ifetch mem_ifetch;
    riscv_mem_read_w mem_read_w;
    riscv_mem_read_s mem_read_s;
    riscv_mem_read_b mem_read_b;

    /* memory write interface */
    riscv_mem_write_w mem_write_w;
    riscv_mem_write_s mem_write_s;
    riscv_mem_write_b mem_write_b;

    /* system */
    riscv_on_ecall on_ecall;
    riscv_on_ebreak on_ebreak;
} riscv_io_t;

/* create a RISC-V emulator */
riscv_t *rv_create(const riscv_io_t *io, riscv_user_t user_data);

/* delete a RISC-V emulator */
void rv_delete(riscv_t *rv);

/* reset the RISC-V processor */
void rv_reset(riscv_t *rv, riscv_word_t pc);

#if RV32_HAS(GDBSTUB)
/* Run the RISC-V emulator as gdbstub */
void rv_debug(riscv_t *rv);
#endif

/* step the RISC-V emulator */
void rv_step(riscv_t *rv, int32_t cycles);

/* get RISC-V user data bound to an emulator */
riscv_user_t rv_userdata(riscv_t *rv);

/* set the program counter of a RISC-V emulator */
bool rv_set_pc(riscv_t *rv, riscv_word_t pc);

/* get the program counter of a RISC-V emulator */
riscv_word_t rv_get_pc(riscv_t *rv);

/* set a register of the RISC-V emulator */
void rv_set_reg(riscv_t *rv, uint32_t reg, riscv_word_t in);

/* get a register of the RISC-V emulator */
riscv_word_t rv_get_reg(riscv_t *rv, uint32_t reg);

/* system call handler */
void syscall_handler(riscv_t *rv);

/* environment call handler */
void ecall_handler(riscv_t *rv);

/* breakpoint exception handler */
void ebreak_handler(riscv_t *rv);

/* statistics */
void rv_stats(riscv_t *rv);

/* halt the core */
void rv_halt(riscv_t *rv);

/* return the halt state */
bool rv_has_halted(riscv_t *rv);

#ifdef __cplusplus
};
#endif
