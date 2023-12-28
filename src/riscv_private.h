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
#if RV32_HAS(JIT)
#include "cache.h"
#endif

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

/* translated basic block */
typedef struct block {
    uint32_t n_insn;           /**< number of instructions encompased */
    uint32_t pc_start, pc_end; /**< address range of the basic block */

    rv_insn_t *ir_head, *ir_tail; /**< the first and last ir for this block */
    bool backward;
#if RV32_HAS(JIT)
    bool hot; /**< Determine the block is hotspot or not */
    uint32_t offset;
    bool
        translatable; /**< Determine the block has RV32AF insturctions or not */
#endif
} block_t;

typedef struct {
    uint32_t block_capacity; /**< max number of entries in the block map */
    uint32_t size;           /**< number of entries currently in the map */
    block_t **map;           /**< block map */
} block_map_t;

/* clear all block in the block map */
void block_map_clear(riscv_t *rv);

struct riscv_internal {
    bool halt; /* indicate whether the core is halted */

    /* I/O interface */
    riscv_io_t io;

    /* integer registers */
    riscv_word_t X[N_RV_REGS];
    riscv_word_t PC;

    /* user provided data */
    riscv_user_t userdata;

#if RV32_HAS(EXT_F)
    /* float registers */
    riscv_float_t F[N_RV_REGS];
    uint32_t csr_fcsr;
#endif

    /* csr registers */
    uint64_t csr_cycle;    /* Machine cycle counter */
    uint32_t csr_time[2];  /* Performance conter */
    uint32_t csr_mstatus;  /* Machine status regester */
    uint32_t csr_mtvec;    /* Machine trap-handler base address */
    uint32_t csr_misa;     /* ISA and extensions */
    uint32_t csr_mtval;    /* Machine bad address or instruction */
    uint32_t csr_mcause;   /* Machine trap cause */
    uint32_t csr_mscratch; /* Scartch register for machine trap handler */
    uint32_t csr_mepc;     /* Machine exception program counter */
    uint32_t csr_mip;      /* Machine interrupt pending */
    uint32_t csr_mbadaddr;

    bool compressed; /**< current instruction is compressed or not */
#if !RV32_HAS(JIT)
    block_map_t block_map; /**< basic block map */
#else
    struct cache *block_cache;
#endif
    struct mpool *block_mp, *block_ir_mp;
    /* print exit code on syscall_exit */
    bool output_exit_code;
    void *jit_state;
#if RV32_HAS(GDBSTUB)
    /* gdbstub instance */
    gdbstub_t gdbstub;

    bool debug_mode;

    /* GDB instruction breakpoint */
    breakpoint_map_t breakpoint_map;

    /* The flag to notify interrupt from GDB client: it should
     * be accessed by atomic operation when starting the GDBSTUB. */
    bool is_interrupted;
#endif
};

/* sign extend a 16 bit value */
FORCE_INLINE uint32_t sign_extend_h(const uint32_t x)
{
    return (int32_t) ((int16_t) x);
}

/* sign extend an 8 bit value */
FORCE_INLINE uint32_t sign_extend_b(const uint32_t x)
{
    return (int32_t) ((int8_t) x);
}

/* Detect the instruction is RV32C or not */
FORCE_INLINE bool is_compressed(uint32_t insn)
{
    return (insn & FC_OPCODE) != 3;
}
