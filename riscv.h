#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RISC-V register files */
enum {
    rv_reg_zero = 0,
    rv_reg_ra,
    rv_reg_sp,
    rv_reg_gp,
    rv_reg_tp,
    rv_reg_t0,
    rv_reg_t1,
    rv_reg_t2,
    rv_reg_s0,
    rv_reg_s1,
    rv_reg_a0,
    rv_reg_a1,
    rv_reg_a2,
    rv_reg_a3,
    rv_reg_a4,
    rv_reg_a5,
    rv_reg_a6,
    rv_reg_a7,
    rv_reg_s2,
    rv_reg_s3,
    rv_reg_s4,
    rv_reg_s5,
    rv_reg_s6,
    rv_reg_s7,
    rv_reg_s8,
    rv_reg_s9,
    rv_reg_s10,
    rv_reg_s11,
    rv_reg_t3,
    rv_reg_t4,
    rv_reg_t5,
    rv_reg_t6,
};

struct riscv_t;
typedef void *riscv_user_t;

typedef uint32_t riscv_word_t;
typedef uint16_t riscv_half_t;
typedef uint8_t riscv_byte_t;
typedef uint32_t riscv_exception_t;
typedef float riscv_float_t;

/* memory read handlers */
typedef riscv_word_t (*riscv_mem_ifetch)(struct riscv_t *rv, riscv_word_t addr);
typedef riscv_word_t (*riscv_mem_read_w)(struct riscv_t *rv, riscv_word_t addr);
typedef riscv_half_t (*riscv_mem_read_s)(struct riscv_t *rv, riscv_word_t addr);
typedef riscv_byte_t (*riscv_mem_read_b)(struct riscv_t *rv, riscv_word_t addr);

/* memory write handlers */
typedef void (*riscv_mem_write_w)(struct riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_word_t data);
typedef void (*riscv_mem_write_s)(struct riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_half_t data);
typedef void (*riscv_mem_write_b)(struct riscv_t *rv,
                                  riscv_word_t addr,
                                  riscv_byte_t data);

/* system instruction handlers */
typedef void (*riscv_on_ecall)(struct riscv_t *rv);
typedef void (*riscv_on_ebreak)(struct riscv_t *rv);

/* RISC-V emulator I/O interface */
struct riscv_io_t {
    /* memory read interface */
    riscv_mem_ifetch mem_ifetch;
    riscv_mem_read_w mem_read_w;
    riscv_mem_read_s mem_read_s;
    riscv_mem_read_b mem_read_b;

    /* memory write interface */
    riscv_mem_write_w mem_write_w;
    riscv_mem_write_s mem_write_s;
    riscv_mem_write_b mem_write_b;

    /* system commands */
    riscv_on_ecall on_ecall;
    riscv_on_ebreak on_ebreak;
};

/* create a RISC-V emulator */
struct riscv_t *rv_create(const struct riscv_io_t *io, riscv_user_t user_data);

/* delete a RISC-V emulator */
void rv_delete(struct riscv_t *);

/* reset the RISC-V processor */
void rv_reset(struct riscv_t *, riscv_word_t pc);

/* step the RISC-V emulator */
void rv_step(struct riscv_t *, int32_t cycles);

/* get RISC-V user data bound to an emulator */
riscv_user_t rv_userdata(struct riscv_t *);

/* set the program counter of a RISC-V emulator */
bool rv_set_pc(struct riscv_t *rv, riscv_word_t pc);

/* get the program counter of a RISC-V emulator */
riscv_word_t rv_get_pc(struct riscv_t *rv);

/* set a register of the RISC-V emulator */
void rv_set_reg(struct riscv_t *, uint32_t reg, riscv_word_t in);

/* get a register of the RISC-V emulator */
riscv_word_t rv_get_reg(struct riscv_t *, uint32_t reg);

/* halt the core */
void rv_halt(struct riscv_t *);

/* return the halt state */
bool rv_has_halted(struct riscv_t *);

#ifdef __cplusplus
};
#endif
