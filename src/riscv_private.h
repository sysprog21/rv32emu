/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once
#include <stdbool.h>

#if RV32_HAS(GDBSTUB)
#include "breakpoint.h"
#include "mini-gdbstub/include/gdbstub.h"
#endif
#include "decode.h"
#include "riscv.h"

#define RV_NUM_REGS 32

/* CSRs */
enum {
    /* floating point */
    CSR_FFLAGS = 0x001, /* Floating-point accrued exceptions */
    CSR_FRM = 0x002,    /* Floating-point dynamic rounding mode */
    CSR_FCSR = 0x003,   /* Floating-point control and status register */

    /* Machine trap setup */
    CSR_MSTATUS = 0x300,    /* Machine status register */
    CSR_MISA = 0x301,       /* ISA and extensions */
    CSR_MEDELEG = 0x302,    /* Machine exception delegate register */
    CSR_MIDELEG = 0x303,    /* Machine interrupt delegate register */
    CSR_MIE = 0x304,        /* Machine interrupt-enable register */
    CSR_MTVEC = 0x305,      /* Machine trap-handler base address */
    CSR_MCOUNTEREN = 0x306, /* Machine counter enable */

    /* machine trap handling */
    CSR_MSCRATCH = 0x340, /* Scratch register for machine trap handlers */
    CSR_MEPC = 0x341,     /* Machine exception program counter */
    CSR_MCAUSE = 0x342,   /* Machine trap cause */
    CSR_MTVAL = 0x343,    /* Machine bad address or instruction */
    CSR_MIP = 0x344,      /* Machine interrupt pending */

    /* low words */
    CSR_CYCLE = 0xC00, /* Cycle counter for RDCYCLE instruction */
    CSR_TIME = 0xC01,  /* Timer for RDTIME instruction */
    CSR_INSTRET = 0xC02,

    /* high words */
    CSR_CYCLEH = 0xC80,
    CSR_TIMEH = 0xC81,
    CSR_INSTRETH = 0xC82,

    CSR_MVENDORID = 0xF11, /* Vendor ID */
    CSR_MARCHID = 0xF12,   /* Architecture ID */
    CSR_MIMPID = 0xF13,    /* Implementation ID */
    CSR_MHARTID = 0xF14,   /* Hardware thread ID */
};

struct riscv_internal {
    bool halt;

    /* I/O interface */
    riscv_io_t io;

    /* integer registers */
    riscv_word_t X[RV_NUM_REGS];
    riscv_word_t PC;

    /* user provided data */
    riscv_user_t userdata;

#if RV32_HAS(GDBSTUB)
    /* gdbstub instance */
    gdbstub_t gdbstub;

    /* GDB instruction breakpoint */
    breakpoint_map_t breakpoint_map;
#endif

#if RV32_HAS(EXT_F)
    /* float registers */
    union {
        riscv_float_t F[RV_NUM_REGS];
        uint32_t F_int[RV_NUM_REGS]; /* integer shortcut */
    };
    uint32_t csr_fcsr;
#endif

    /* csr registers */
    uint64_t csr_cycle;
    uint64_t csr_time;
    uint32_t csr_mstatus;
    uint32_t csr_mtvec;
    uint32_t csr_misa;
    uint32_t csr_mtval;
    uint32_t csr_mcause;
    uint32_t csr_mscratch;
    uint32_t csr_mepc;
    uint32_t csr_mip;
    uint32_t csr_mbadaddr;

    bool compressed;            /**< current instruction is compressed or not */
    struct block_map block_map; /**< basic block map */
};

/* sign extend a 16 bit value */
static inline uint32_t sign_extend_h(const uint32_t x)
{
    return (int32_t) ((int16_t) x);
}

/* sign extend an 8 bit value */
static inline uint32_t sign_extend_b(const uint32_t x)
{
    return (int32_t) ((int8_t) x);
}
