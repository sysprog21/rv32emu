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
    uint32_t insn_capacity;    /**< maximum of instructions encompased */
    struct block *predict;     /**< block prediction */
    rv_insn_t *ir;             /**< IR as memory blocks */
#if RV32_HAS(JIT)
    bool hot; /**< Determine the block is hotspot or not */
#endif
} block_t;

/* Map the translated basic block to its program counter to accelerate the
 * searching process. It's important to note that this data structure is not
 * utilized in JIT mode.
 */
typedef struct {
    uint32_t block_capacity; /**< max number of entries in the block map */
    uint32_t size;           /**< number of entries currently in the map */
    block_t **map;           /**< block map */
} block_map_t;

/* clear all block in the block map */
void block_map_clear(block_map_t *map);

struct riscv_internal {
    bool halt;

    /* I/O interface */
    riscv_io_t io;

    /* integer registers */
    riscv_word_t X[RV_N_REGS];
    riscv_word_t PC;

    /* user provided data */
    riscv_user_t userdata;

#if RV32_HAS(EXT_F)
    /* float registers */
    union {
        riscv_float_t F[RV_N_REGS];
        uint32_t F_int[RV_N_REGS]; /* integer shortcut */
    };
    uint32_t csr_fcsr;
#endif

    /* csr registers */
    uint64_t csr_cycle;
    uint32_t csr_time[2];
    uint32_t csr_mstatus;
    uint32_t csr_mtvec;
    uint32_t csr_misa;
    uint32_t csr_mtval;
    uint32_t csr_mcause;
    uint32_t csr_mscratch;
    uint32_t csr_mepc;
    uint32_t csr_mip;
    uint32_t csr_mbadaddr;

    bool compressed; /**< current instruction is compressed or not */

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

#if !RV32_HAS(JIT)
    block_map_t block_map; /**< basic block map */
#else
    struct cache *block_cache;
    struct cache *code_cache;
#endif

    /* print exit code on syscall_exit */
    bool output_exit_code;
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

#if RV32_HAS(EXT_F)
#include <math.h>
#include "softfloat.h"

#if defined(__APPLE__)
static inline int isinff(float x)
{
    return __builtin_fabsf(x) == __builtin_inff();
}
static inline int isnanf(float x)
{
    return x != x;
}
#endif
#endif /* RV32_HAS(EXT_F) */

#if RV32_HAS(Zicsr)
/* CSRRW (Atomic Read/Write CSR) instruction atomically swaps values in the
 * CSRs and integer registers. CSRRW reads the old value of the CSR,
 * zero-extends the value to XLEN bits, and then writes it to register rd.
 * The initial value in rs1 is written to the CSR.
 * If rd == x0, then the instruction shall not read the CSR and shall not cause
 * any of the side effects that might occur on a CSR read.
 */
uint32_t csr_csrrw(riscv_t *rv, uint32_t csr, uint32_t val);

/* perform csrrs (atomic read and set) */
uint32_t csr_csrrs(riscv_t *rv, uint32_t csr, uint32_t val);

/* perform csrrc (atomic read and clear)
 * Read old value of CSR, zero-extend to XLEN bits, write to rd.
 * Read value from rs1, use as bit mask to clear bits in CSR.
 */
uint32_t csr_csrrc(riscv_t *rv, uint32_t csr, uint32_t val);
#endif
