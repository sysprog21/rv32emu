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

#define RV_REGS_LIST                                   \
    _(zero) /* hard-wired zero, ignoring any writes */ \
    _(ra)   /* return address */                       \
    _(sp)   /* stack pointer */                        \
    _(gp)   /* global pointer */                       \
    _(tp)   /* thread pointer */                       \
    _(t0)   /* temporary/alternate link register */    \
    _(t1)   /* temporaries */                          \
    _(t2)                                              \
    _(s0) /* saved register/frame pointer */           \
    _(s1)                                              \
    _(a0) /* function arguments / return values */     \
    _(a1)                                              \
    _(a2) /* function arguments */                     \
    _(a3)                                              \
    _(a4)                                              \
    _(a5)                                              \
    _(a6)                                              \
    _(a7)                                              \
    _(s2) /* saved register */                         \
    _(s3)                                              \
    _(s4)                                              \
    _(s5)                                              \
    _(s6)                                              \
    _(s7)                                              \
    _(s8)                                              \
    _(s9)                                              \
    _(s10)                                             \
    _(s11)                                             \
    _(t3) /* temporary register */                     \
    _(t4)                                              \
    _(t5)                                              \
    _(t6)

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
#define _(reg) rv_reg_##reg,
    RV_REGS_LIST
#undef _
        N_RV_REGS
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
typedef riscv_word_t (*riscv_mem_ifetch)(riscv_word_t addr);
typedef riscv_word_t (*riscv_mem_read_w)(riscv_word_t addr);
typedef riscv_half_t (*riscv_mem_read_s)(riscv_word_t addr);
typedef riscv_byte_t (*riscv_mem_read_b)(riscv_word_t addr);

/* memory write handlers */
typedef void (*riscv_mem_write_w)(riscv_word_t addr, riscv_word_t data);
typedef void (*riscv_mem_write_s)(riscv_word_t addr, riscv_half_t data);
typedef void (*riscv_mem_write_b)(riscv_word_t addr, riscv_byte_t data);

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

    /* enable misaligned memory access */
    bool allow_misalign;
} riscv_io_t;

/* create a RISC-V emulator */
riscv_t *rv_create(const riscv_io_t *io,
                   riscv_user_t user_data,
                   int argc,
                   char **args,
                   bool output_exit_code);

/* delete a RISC-V emulator */
void rv_delete(riscv_t *rv);

/* reset the RISC-V processor */
void rv_reset(riscv_t *rv, riscv_word_t pc, int argc, char **args);

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

/* dump registers as JSON to out_file_path */
void dump_registers(riscv_t *rv, char *out_file_path);

/* breakpoint exception handler */
void ebreak_handler(riscv_t *rv);

/* halt the core */
void rv_halt(riscv_t *rv);

/* return the halt state */
bool rv_has_halted(riscv_t *rv);

/* return the flag of outputting exit code */
bool rv_enables_to_output_exit_code(riscv_t *rv);

#ifdef __cplusplus
};
#endif
