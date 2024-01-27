/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "io.h"
#include "map.h"

#if RV32_HAS(EXT_F)
#define float16_t softfloat_float16_t
#define bfloat16_t softfloat_bfloat16_t
#define float32_t softfloat_float32_t
#define float64_t softfloat_float64_t
#include "softfloat/softfloat.h"
#undef float16_t
#undef bfloat16_t
#undef float32_t
#undef float64_t
#endif

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
/* clang-format off */
enum {
#define _(r) rv_reg_##r,
    RV_REGS_LIST
#undef _
    N_RV_REGS
};
/* clang-format on */

#define MISA_SUPER (1 << ('S' - 'A'))
#define MISA_USER (1 << ('U' - 'A'))
#define MISA_I (1 << ('I' - 'A'))
#define MISA_M (1 << ('M' - 'A'))
#define MISA_A (1 << ('A' - 'A'))
#define MISA_F (1 << ('F' - 'A'))
#define MISA_C (1 << ('C' - 'A'))
#define MSTATUS_MPIE_SHIFT 7
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_MPIE (1 << MSTATUS_MPIE_SHIFT)
#define MSTATUS_MPP (3 << MSTATUS_MPP_SHIFT)

#define BLOCK_MAP_CAPACITY_BITS 10

/* forward declaration for internal structure */
typedef struct riscv_internal riscv_t;
typedef void *riscv_user_t;

typedef uint32_t riscv_word_t;
typedef uint16_t riscv_half_t;
typedef uint8_t riscv_byte_t;
typedef uint32_t riscv_exception_t;
#if RV32_HAS(EXT_F)
typedef softfloat_float32_t riscv_float_t;
#endif

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
typedef void (*riscv_on_memset)(riscv_t *rv);
typedef void (*riscv_on_memcpy)(riscv_t *rv);
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
    riscv_on_memset on_memset;
    riscv_on_memcpy on_memcpy;
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

/* memset handler */
void memset_handler(riscv_t *rv);

/* memcpy handler */
void memcpy_handler(riscv_t *rv);

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

/* state structure passed to the runtime */
typedef struct {
    memory_t *mem;

    /* the data segment break address */
    riscv_word_t break_addr;

    /* file descriptor map: int -> (FILE *) */
    map_t fd_map;
} state_t;

/* create a state */
state_t *state_new(void);

/* delete a state */
void state_delete(state_t *s);

void rv_profile(riscv_t *rv, char *out_file_path);

#ifdef __cplusplus
};
#endif
