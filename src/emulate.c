/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if RV32_HAS(EXT_F)
#include <math.h>
#include "soft-float.h"

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

#if RV32_HAS(GDBSTUB)
extern struct target_ops gdbstub_ops;
#endif

#include "decode.h"
#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"

/* RISC-V exception code list */
#define RV_EXCEPTION_LIST                                       \
    _(insn_misaligned, 0)  /* Instruction address misaligned */ \
    _(illegal_insn, 2)     /* Illegal instruction */            \
    _(breakpoint, 3)       /* Breakpoint */                     \
    _(load_misaligned, 4)  /* Load address misaligned */        \
    _(store_misaligned, 6) /* Store/AMO address misaligned */   \
    _(ecall_M, 11)         /* Environment call from M-mode */

enum {
#define _(type, code) rv_exception_code##type = code,
    RV_EXCEPTION_LIST
#undef _
};

static void rv_exception_default_handler(riscv_t *rv)
{
    rv->csr_mepc += rv->compressed ? INSN_16 : INSN_32;
    rv->PC = rv->csr_mepc; /* mret */
}

#define EXCEPTION_HANDLER_IMPL(type, code)                                \
    static void rv_except_##type(riscv_t *rv, uint32_t mtval)             \
    {                                                                     \
        /* mtvec (Machine Trap-Vector Base Address Register)              \
         * mtvec[MXLEN-1:2]: vector base address                          \
         * mtvec[1:0] : vector mode                                       \
         */                                                               \
        const uint32_t base = rv->csr_mtvec & ~0x3;                       \
        const uint32_t mode = rv->csr_mtvec & 0x3;                        \
        /* mepc  (Machine Exception Program Counter)                      \
         * mtval (Machine Trap Value Register)                            \
         * mcause (Machine Cause Register): store exception code          \
         */                                                               \
        rv->csr_mepc = rv->PC;                                            \
        rv->csr_mtval = mtval;                                            \
        rv->csr_mcause = code;                                            \
        if (!rv->csr_mtvec) { /* in case CSR is not configured */         \
            rv_exception_default_handler(rv);                             \
            return;                                                       \
        }                                                                 \
        switch (mode) {                                                   \
        case 0: /* DIRECT: All exceptions set PC to base */               \
            rv->PC = base;                                                \
            break;                                                        \
        /* VECTORED: Asynchronous interrupts set PC to base + 4 * code */ \
        case 1:                                                           \
            rv->PC = base + 4 * code;                                     \
            break;                                                        \
        }                                                                 \
    }

/* RISC-V exception handlers */
#define _(type, code) EXCEPTION_HANDLER_IMPL(type, code)
RV_EXCEPTION_LIST
#undef _

/* Get current time in microsecnds and update csr_time register */
static inline void update_time(riscv_t *rv)
{
    struct timeval tv;
    rv_gettimeofday(&tv);

    rv->csr_time = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
}

#if RV32_HAS(Zicsr)
/* get a pointer to a CSR */
static uint32_t *csr_get_ptr(riscv_t *rv, uint32_t csr)
{
    switch (csr) {
    case CSR_MSTATUS: /* Machine Status */
        return (uint32_t *) (&rv->csr_mstatus);
    case CSR_MTVEC: /* Machine Trap Handler */
        return (uint32_t *) (&rv->csr_mtvec);
    case CSR_MISA: /* Machine ISA and Extensions */
        return (uint32_t *) (&rv->csr_misa);

    /* Machine Trap Handling */
    case CSR_MSCRATCH: /* Machine Scratch Register */
        return (uint32_t *) (&rv->csr_mscratch);
    case CSR_MEPC: /* Machine Exception Program Counter */
        return (uint32_t *) (&rv->csr_mepc);
    case CSR_MCAUSE: /* Machine Exception Cause */
        return (uint32_t *) (&rv->csr_mcause);
    case CSR_MTVAL: /* Machine Trap Value */
        return (uint32_t *) (&rv->csr_mtval);
    case CSR_MIP: /* Machine Interrupt Pending */
        return (uint32_t *) (&rv->csr_mip);

    /* Machine Counter/Timers */
    case CSR_CYCLE: /* Cycle counter for RDCYCLE instruction */
        return (uint32_t *) (&rv->csr_cycle) + 0;
    case CSR_CYCLEH: /* Upper 32 bits of cycle */
        return (uint32_t *) (&rv->csr_cycle) + 1;

    /* TIME/TIMEH - very roughly about 1 ms per tick */
    case CSR_TIME: { /* Timer for RDTIME instruction */
        update_time(rv);
        return (uint32_t *) (&rv->csr_time) + 0;
    }
    case CSR_TIMEH: { /* Upper 32 bits of time */
        update_time(rv);
        return (uint32_t *) (&rv->csr_time) + 1;
    }
    case CSR_INSTRET: /* Number of Instructions Retired Counter */
        /* Number of Instructions Retired Counter, just use cycle */
        return (uint32_t *) (&rv->csr_cycle) + 0;
#if RV32_HAS(EXT_F)
    case CSR_FFLAGS:
        return (uint32_t *) (&rv->csr_fcsr);
    case CSR_FCSR:
        return (uint32_t *) (&rv->csr_fcsr);
#endif
    default:
        return NULL;
    }
}

static inline bool csr_is_writable(uint32_t csr)
{
    return csr < 0xc00;
}

/* CSRRW (Atomic Read/Write CSR) instruction atomically swaps values in the
 * CSRs and integer registers. CSRRW reads the old value of the CSR,
 * zero - extends the value to XLEN bits, then writes it to integer register rd.
 * The initial value in rs1 is written to the CSR.
 * If rd == x0, then the instruction shall not read the CSR and shall not cause
 * any of the side effects that might occur on a CSR read.
 */
static uint32_t csr_csrrw(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c = val;

    return out;
}

/* perform csrrs (atomic read and set) */
static uint32_t csr_csrrs(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c |= val;

    return out;
}

/* perform csrrc (atomic read and clear)
 * Read old value of CSR, zero-extend to XLEN bits, write to rd
 * Read value from rs1, use as bit mask to clear bits in CSR
 */
static uint32_t csr_csrrc(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c &= ~val;
    return out;
}
#endif

#if RV32_HAS(GDBSTUB)
void rv_debug(riscv_t *rv)
{
    if (!gdbstub_init(&rv->gdbstub, &gdbstub_ops,
                      (arch_info_t){
                          .reg_num = 33,
                          .reg_byte = 4,
                          .target_desc = TARGET_RV32,
                      },
                      GDBSTUB_COMM)) {
        return;
    }

    rv->breakpoint_map = breakpoint_map_new();

    if (!gdbstub_run(&rv->gdbstub, (void *) rv))
        return;

    breakpoint_map_destroy(rv->breakpoint_map);
    gdbstub_close(&rv->gdbstub);
}
#endif /* RV32_HAS(GDBSTUB) */

static inline bool insn_is_misaligned(uint32_t pc)
{
    return (pc &
#if RV32_HAS(EXT_C)
            0x1
#else
            0x3
#endif
    );
}

/* execute a basic block */
static bool emulate(riscv_t *rv, const block_t *block)
{
#if RV32_HAS(COMPUTED_GOTO)
    static const void *dispatch_table[] = {
#define _(inst) [rv_insn_##inst] = &&do_##inst,
        RISCV_INSN_LIST
#undef _
    };

#define DISPATCH()              \
    /* enforce zero register */ \
    rv->X[rv_reg_zero] = 0;     \
    /* current IR */            \
    ir = block->ir + index++;   \
    /* jump */                  \
    goto *dispatch_table[ir->opcode];

/* clang-format off */
#define _(inst, code)                    \
    do_##inst: code                      \
    /* step over instruction */          \
    rv->PC += ir->insn_len;              \
    /* increment the cycles CSR */       \
    rv->csr_cycle++;                     \
    /* all instructions have executed */ \
    if (unlikely(index == n_insn))       \
        return true;                     \
    DISPATCH()
/* clang-format on */
#define EPILOGUE()

#else /* !RV32_HAS(COMPUTED_GOTO) */
#define DISPATCH()                          \
    for (uint32_t i = 0; i < n_insn; i++) { \
        ir = block->ir + i;                 \
        /* enforce zero register */         \
        rv->X[rv_reg_zero] = 0;             \
        switch (ir->opcode) {
/* clang-format off */
#define _(inst, code)         \
    case rv_insn_##inst: code \
        break;
#define EPILOGUE()                          \
        }                                   \
        /* step over instruction */         \
        rv->PC += ir->insn_len;             \
        /* increment the cycles csr */      \
        rv->csr_cycle++;                    \
    }                                       \
    return true;
/* clang-format on */
#endif /* RV32_HAS(COMPUTED_GOTO) */

    const uint32_t n_insn = block->n_insn;
    rv_insn_t *ir;

#if RV32_HAS(COMPUTED_GOTO)
    /* current index in block */
    uint32_t index = 0;
#endif

    /* main loop */
    DISPATCH()

    /* LUI (Load Upper Immediate) is used to build 32-bit constants and uses the
     * U-type format. LUI places the U-immediate value in the top 20 bits of the
     * destination register rd, filling in the lowest 12 bits with zeros. The
     * 32-bit result is sign-extended to 64 bits.
     */
    _(lui, rv->X[ir->rd] = ir->imm;)

    /* AUIPC (Add Upper Immediate to PC) is used to build pc-relative addresses
     * and uses the U-type format. AUIPC forms a 32-bit offset from the 20-bit
     * U-immediate, filling in the lowest 12 bits with zeros, adds this offset
     * to the address of the AUIPC instruction, then places the result in
     * register rd.
     */
    _(auipc, rv->X[ir->rd] = ir->imm + rv->PC;)

    /* JAL: Jump and Link
     * store successor instruction address into rd.
     * add next J imm (offset) to pc.
     */
    _(jal, {
        const uint32_t pc = rv->PC;
        /* Jump */
        rv->PC += ir->imm;
        /* link with return address */
        if (ir->rd)
            rv->X[ir->rd] = pc + ir->insn_len;
        /* check instruction misaligned */
        if (unlikely(insn_is_misaligned(rv->PC))) {
            rv->compressed = false;
            rv_except_insn_misaligned(rv, pc);
            return false;
        }
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* JALR: Jump and Link Register
     * The indirect jump instruction JALR uses the I-type encoding. The
     * target address is obtained by adding the sign-extended 12-bit
     * I-immediate to the register rs1, then setting the least-significant
     * bit of the result to zero. The address of the instruction following
     * the jump (pc+4) is written to register rd. Register x0 can be used as
     * the destination if the result is not required.
     */
    _(jalr, {
        const uint32_t pc = rv->PC;
        /* jump */
        rv->PC = (rv->X[ir->rs1] + ir->imm) & ~1U;
        /* link */
        if (ir->rd)
            rv->X[ir->rd] = pc + ir->insn_len;
        /* check instruction misaligned */
        if (unlikely(insn_is_misaligned(rv->PC))) {
            rv->compressed = false;
            rv_except_insn_misaligned(rv, pc);
            return false;
        }
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* BEQ: Branch if Equal */
    _(beq, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] == rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (unlikely(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* increment the cycles csr */
            rv->csr_cycle++;
            /* can branch */
            return true;
        }
    })

    /* BNE: Branch if Not Equal */
    _(bne, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] != rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (unlikely(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* increment the cycles csr */
            rv->csr_cycle++;
            /* can branch */
            return true;
        }
    })

    /* BLT: Branch if Less Than */
    _(blt, {
        const uint32_t pc = rv->PC;
        if ((int32_t) rv->X[ir->rs1] < (int32_t) rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (unlikely(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* increment the cycles csr */
            rv->csr_cycle++;
            /* can branch */
            return true;
        }
    })

    /* BGE: Branch if Greater Than */
    _(bge, {
        const uint32_t pc = rv->PC;
        if ((int32_t) rv->X[ir->rs1] >= (int32_t) rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (unlikely(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* increment the cycles csr */
            rv->csr_cycle++;
            /* can branch */
            return true;
        }
    })

    /* BLTU: Branch if Less Than Unsigned */
    _(bltu, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] < rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (unlikely(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* increment the cycles csr */
            rv->csr_cycle++;
            /* can branch */
            return true;
        }
    })

    /* BGEU: Branch if Greater Than Unsigned */
    _(bgeu, {
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] >= rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (unlikely(insn_is_misaligned(rv->PC))) {
                rv->compressed = false;
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* increment the cycles csr */
            rv->csr_cycle++;
            /* can branch */
            return true;
        }
    })

    /* LB: Load Byte */
    _(lb, {
        rv->X[ir->rd] =
            sign_extend_b(rv->io.mem_read_b(rv, rv->X[ir->rs1] + ir->imm));
    })

    /* LH: Load Halfword */
    _(lh, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (unlikely(addr & 1)) {
            rv->compressed = false;
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = sign_extend_h(rv->io.mem_read_s(rv, addr));
    })

    /* LW: Load Word */
    _(lw, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (unlikely(addr & 3)) {
            rv->compressed = false;
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
    })

    /* LBU: Load Byte Unsigned */
    _(lbu, rv->X[ir->rd] = rv->io.mem_read_b(rv, rv->X[ir->rs1] + ir->imm);)

    /* LHU: Load Halfword Unsigned */
    _(lhu, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (unlikely(addr & 1)) {
            rv->compressed = false;
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_s(rv, addr);
    })

    /* SB: Store Byte */
    _(sb, rv->io.mem_write_b(rv, rv->X[ir->rs1] + ir->imm, rv->X[ir->rs2]);)

    /* SH: Store Halfword */
    _(sh, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (unlikely(addr & 1)) {
            rv->compressed = false;
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_s(rv, addr, rv->X[ir->rs2]);
    })

    /* SW: Store Word */
    _(sw, {
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (unlikely(addr & 3)) {
            rv->compressed = false;
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
    })

    /* ADDI (Add Immediate) adds the sign-extended 12-bit immediate to register
     * rs1. Arithmetic overflow is ignored and the result is simply the low XLEN
     * bits of the result. ADDI rd, rs1, 0 is used to implement the MV rd, rs1
     * assembler pseudo-instruction.
     */
    _(addi, rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + ir->imm;)

    /* SLTI (Set on Less Than Immediate) places the value 1 in register rd if
     * register rs1 is less than the signextended immediate when both are
     * treated as signed numbers, else 0 is written to rd.
     */
    _(slti, rv->X[ir->rd] = ((int32_t) (rv->X[ir->rs1]) < ir->imm) ? 1 : 0;)

    /* SLTIU (Set on Less Than Immediate Unsigned) places the value 1 in
     * register rd if register rs1 is less than the immediate when both are
     * treated as unsigned numbers, else 0 is written to rd.
     */
    _(sltiu, rv->X[ir->rd] = (rv->X[ir->rs1] < (uint32_t) ir->imm) ? 1 : 0;)

    /* XORI: Exclusive OR Immediate */
    _(xori, rv->X[ir->rd] = rv->X[ir->rs1] ^ ir->imm;)

    /* ORI: OR Immediate */
    _(ori, rv->X[ir->rd] = rv->X[ir->rs1] | ir->imm;)

    /* ANDI (AND Immediate) performs bitwise AND on register rs1 and the
     * sign-extended 12-bit immediate and place the result in rd.
     */
    _(andi, rv->X[ir->rd] = rv->X[ir->rs1] & ir->imm;)

    /* SLLI (Shift Left Logical) performs logical left shift on the value in
     * register rs1 by the shift amount held in the lower 5 bits of the
     * immediate.
     */
    _(slli, rv->X[ir->rd] = rv->X[ir->rs1] << (ir->imm & 0x1f);)

    /* SRLI (Shift Right Logical) performs logical right shift on the value in
     * register rs1 by the shift amount held in the lower 5 bits of the
     * immediate.
     */
    _(srli, rv->X[ir->rd] = rv->X[ir->rs1] >> (ir->imm & 0x1f);)

    /* SRAI (Shift Right Arithmetic) performs arithmetic right shift on the
     * value in register rs1 by the shift amount held in the lower 5 bits of the
     * immediate.
     */
    _(srai, rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (ir->imm & 0x1f);)

    /* ADD */
    _(add,
      rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + (int32_t) (rv->X[ir->rs2]);)

    /* SUB: Substract */
    _(sub,
      rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) - (int32_t) (rv->X[ir->rs2]);)

    /* SLL: Shift Left Logical */
    _(sll, rv->X[ir->rd] = rv->X[ir->rs1] << (rv->X[ir->rs2] & 0x1f);)

    /* SLT: Set on Less Than */
    _(slt, {
        rv->X[ir->rd] =
            ((int32_t) (rv->X[ir->rs1]) < (int32_t) (rv->X[ir->rs2])) ? 1 : 0;
    })

    /* SLTU: Set on Less Than Unsigned */
    _(sltu, rv->X[ir->rd] = (rv->X[ir->rs1] < rv->X[ir->rs2]) ? 1 : 0;)

    /* XOR: Exclusive OR */
    _(xor, rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2];)

    /* SRL: Shift Right Logical */
    _(srl, rv->X[ir->rd] = rv->X[ir->rs1] >> (rv->X[ir->rs2] & 0x1f);)

    /* SRA: Shift Right Arithmetic */
    _(sra, {
        rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (rv->X[ir->rs2] & 0x1f);
    })

    /* OR */
    _(or, rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2];)

    /* AND */
    _(and, rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2];)

    /* ECALL: Environment Call */
    _(ecall, {
        rv->io.on_ecall(rv); /* increment the cycles csr */
        rv->csr_cycle++;
        return true;
    })

    /* EBREAK: Environment Break */
    _(ebreak, {
        rv->io.on_ebreak(rv); /* increment the cycles csr */
        rv->csr_cycle++;
        return true;
    })

    /* WFI: Wait for Interrupt */
    _(wfi, return false;)

    /* URET: return from traps in U-mode */
    _(uret, return false;)

    /* SRET: return from traps in S-mode */
    _(sret, return false;)

    /* HRET: return from traps in H-mode */
    _(hret, return false;)

    /* MRET: return from traps in U-mode */
    _(mret, {
        rv->PC = rv->csr_mepc;
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* this is a branch */
        return true;
    })

    /* RV32 Zifencei Standard Extension */
#if RV32_HAS(Zifencei)
    _(fencei, /* FIXME: fill real implementations */);
#endif

    /* RV32 Zicsr Standard Extension */
#if RV32_HAS(Zicsr)
    /* CSRRW: Atomic Read/Write CSR */
    _(csrrw, {
        uint32_t tmp = csr_csrrw(rv, ir->imm, rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRS: Atomic Read and Set Bits in CSR */
    _(csrrs, {
        uint32_t tmp = csr_csrrs(
            rv, ir->imm, (ir->rs1 == rv_reg_zero) ? 0U : rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRC: Atomic Read and Clear Bits in CSR */
    _(csrrc, {
        uint32_t tmp = csr_csrrc(
            rv, ir->imm, (ir->rs1 == rv_reg_zero) ? ~0U : rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRWI */
    _(csrrwi, {
        uint32_t tmp = csr_csrrw(rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRSI */
    _(csrrsi, {
        uint32_t tmp = csr_csrrs(rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })

    /* CSRRCI */
    _(csrrci, {
        uint32_t tmp = csr_csrrc(rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
    })
#endif /* RV32_HAS(Zicsr) */

    /* RV32M Standard Extension */
#if RV32_HAS(EXT_M)
    /* MUL: Multiply */
    _(mul, rv->X[ir->rd] = (int32_t) rv->X[ir->rs1] * (int32_t) rv->X[ir->rs2];)

    /* MULH: Multiply High Signed Signed */
    _(mulh, {
        const int64_t a = (int32_t) rv->X[ir->rs1];
        const int64_t b = (int32_t) rv->X[ir->rs2];
        rv->X[ir->rd] = ((uint64_t) (a * b)) >> 32;
    })

    /* MULHSU: Multiply High Signed Unsigned */
    _(mulhsu, {
        const int64_t a = (int32_t) rv->X[ir->rs1];
        const uint64_t b = rv->X[ir->rs2];
        rv->X[ir->rd] = ((uint64_t) (a * b)) >> 32;
    })

    /* MULHU: Multiply High Unsigned Unsigned */
    _(mulhu, {
        rv->X[ir->rd] =
            ((uint64_t) rv->X[ir->rs1] * (uint64_t) rv->X[ir->rs2]) >> 32;
    })

    /* DIV: Divide Signed */
    _(div, {
        const int32_t dividend = (int32_t) rv->X[ir->rs1];
        const int32_t divisor = (int32_t) rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? ~0U
                        : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                            ? rv->X[ir->rs1] /* overflow */
                            : (unsigned int) (dividend / divisor);
    })

    /* DIVU: Divide Unsigned */
    _(divu, {
        const uint32_t dividend = rv->X[ir->rs1];
        const uint32_t divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? ~0U : dividend / divisor;
    })

    /* REM: Remainder Signed */
    _(rem, {
        const int32_t dividend = rv->X[ir->rs1];
        const int32_t divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? dividend
                        : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                            ? 0 /* overflow */
                            : (dividend % divisor);
    })

    /* REMU: Remainder Unsigned */
    _(remu, {
        const uint32_t dividend = rv->X[ir->rs1];
        const uint32_t divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? dividend : dividend % divisor;
    })
#endif /* RV32_HAS(EXT_M) */

    /* RV32A Standard Extension
     * At present, AMO is not implemented atomically because the emulated
     * RISC-V core just runs on single thread, and no out-of-order execution
     * happens. In addition, rl/aq are not handled.
     */
#if RV32_HAS(EXT_A)
    /* LR.W: Load Reserved */
    _(lrw, {
        /* skip registration of the 'reservation set'
         * FIXME: uimplemented
         */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, rv->X[ir->rs1]);
    })

    /* SC.W: Store Conditional */
    _(scw, {
        /* assume the 'reservation set' is valid
         * FIXME: unimplemented
         */
        rv->io.mem_write_w(rv, rv->X[ir->rs1], rv->X[ir->rs2]);
        rv->X[ir->rd] = 0;
    })

    /* AMOSWAP.W: Atomic Swap */
    _(amoswapw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        rv->io.mem_write_s(rv, ir->rs1, rv->X[ir->rs2]);
    })

    /* AMOADD.W: Atomic ADD */
    _(amoaddw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = (int32_t) rv->X[ir->rd] + (int32_t) rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOXOR.W: Atomix XOR */
    _(amoxorw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] ^ rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOAND.W: Atomic AND */
    _(amoandw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] & rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOOR.W: Atomic OR */
    _(amoorw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] | rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMIN.W: Atomic MIN */
    _(amominw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t a = rv->X[ir->rd];
        const int32_t b = rv->X[ir->rs2];
        const int32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMAX.W: Atomic MAX */
    _(amomaxw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t a = rv->X[ir->rd];
        const int32_t b = rv->X[ir->rs2];
        const int32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMINU.W */
    _(amominuw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const uint32_t a = rv->X[ir->rd];
        const uint32_t b = rv->X[ir->rs2];
        const uint32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })

    /* AMOMAXU.W */
    _(amomaxuw, {
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const uint32_t a = rv->X[ir->rd];
        const uint32_t b = rv->X[ir->rs2];
        const uint32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
    })
#endif /* RV32_HAS(EXT_A) */

    /* RV32F Standard Extension */
#if RV32_HAS(EXT_F)
    /* FLW */
    _(flw, {
        /* copy into the float register */
        const uint32_t data = rv->io.mem_read_w(rv, rv->X[ir->rs1] + ir->imm);
        memcpy(rv->F + ir->rd, &data, 4);
    })

    /* FSW */
    _(fsw, {
        /* copy from float registers */
        uint32_t data;
        memcpy(&data, (const void *) (rv->F + ir->rs2), 4);
        rv->io.mem_write_w(rv, rv->X[ir->rs1] + ir->imm, data);
    })

    /* FMADD.S */
    _(fmadds, rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] + rv->F[ir->rs3];)

    /* FMSUB.S */
    _(fmsubs, rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] - rv->F[ir->rs3];)

    /* FNMSUB.S */
    _(fnmsubs,
      rv->F[ir->rd] = rv->F[ir->rs3] - (rv->F[ir->rs1] * rv->F[ir->rs2]);)

    /* FNMADD.S */
    _(fnmadds,
      rv->F[ir->rd] = -(rv->F[ir->rs1] * rv->F[ir->rs2]) - rv->F[ir->rs3];)

    /* FADD.S */
    _(fadds, {
        if (isnanf(rv->F[ir->rs1]) || isnanf(rv->F[ir->rs2]) ||
            isnanf(rv->F[ir->rs1] + rv->F[ir->rs2])) {
            /* raise invalid operation */
            rv->F_int[ir->rd] = RV_NAN;
            /* F_int is the integer shortcut of F */
            rv->csr_fcsr |= FFLAG_INVALID_OP;
        } else {
            rv->F[ir->rd] = rv->F[ir->rs1] + rv->F[ir->rs2];
        }
        if (isinff(rv->F[ir->rd])) {
            rv->csr_fcsr |= FFLAG_OVERFLOW;
            rv->csr_fcsr |= FFLAG_INEXACT;
        }
    })

    /* FSUB.S */
    _(fsubs, {
        if (isnanf(rv->F[ir->rs1]) || isnanf(rv->F[ir->rs2])) {
            rv->F_int[ir->rd] = RV_NAN;
        } else {
            rv->F[ir->rd] = rv->F[ir->rs1] - rv->F[ir->rs2];
        }
    })

    /* FMUL.S */
    _(fmuls, rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2];)

    /* FDIV.S */
    _(fdivs, rv->F[ir->rd] = rv->F[ir->rs1] / rv->F[ir->rs2];)

    /* FSQRT.S */
    _(fsqrts, rv->F[ir->rd] = sqrtf(rv->F[ir->rs1]);)

    /* FSGNJ.S */
    _(fsgnjs, {
        uint32_t f1;
        uint32_t f2;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        uint32_t res = (f1 & ~FMASK_SIGN) | (f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
    })

    /* FSGNJN.S */
    _(fsgnjns, {
        uint32_t f1;
        uint32_t f2;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        uint32_t res = (f1 & ~FMASK_SIGN) | (~f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
    })

    /* FSGNJX.S */
    _(fsgnjxs, {
        uint32_t f1;
        uint32_t f2;
        uint32_t res;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        res = f1 ^ (f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
    })

    /* FMIN.S */
    _(fmins, {
        /* In IEEE754-201x, fmin(x, y) return
         * - min(x,y) if both numbers are not NaN
         * - if one is NaN and another is a number, return the number
         * - if both are NaN, return NaN
         * When input is signaling NaN, raise invalid operation
         */
        uint32_t x;
        uint32_t y;
        memcpy(&x, rv->F + ir->rs1, 4);
        memcpy(&y, rv->F + ir->rs2, 4);
        if (is_nan(x) || is_nan(y)) {
            if (is_snan(x) || is_snan(y))
                rv->csr_fcsr |= FFLAG_INVALID_OP;
            if (is_nan(x) && !is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs2];
            } else if (!is_nan(x) && is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs1];
            } else {
                rv->F_int[ir->rd] = RV_NAN;
            }
        } else {
            uint32_t a_sign;
            uint32_t b_sign;
            a_sign = x & FMASK_SIGN;
            b_sign = y & FMASK_SIGN;
            if (a_sign != b_sign) {
                rv->F[ir->rd] = a_sign ? rv->F[ir->rs1] : rv->F[ir->rs2];
            } else {
                rv->F[ir->rd] = (rv->F[ir->rs1] < rv->F[ir->rs2])
                                    ? rv->F[ir->rs1]
                                    : rv->F[ir->rs2];
            }
        }
    })

    /* FMAX.S */
    _(fmaxs, {
        uint32_t x;
        uint32_t y;
        memcpy(&x, rv->F + ir->rs1, 4);
        memcpy(&y, rv->F + ir->rs2, 4);
        if (is_nan(x) || is_nan(y)) {
            if (is_snan(x) || is_snan(y))
                rv->csr_fcsr |= FFLAG_INVALID_OP;
            if (is_nan(x) && !is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs2];
            } else if (!is_nan(x) && is_nan(y)) {
                rv->F[ir->rd] = rv->F[ir->rs1];
            } else {
                rv->F_int[ir->rd] = RV_NAN;
            }
        } else {
            uint32_t a_sign;
            uint32_t b_sign;
            a_sign = x & FMASK_SIGN;
            b_sign = y & FMASK_SIGN;
            if (a_sign != b_sign) {
                rv->F[ir->rd] = a_sign ? rv->F[ir->rs2] : rv->F[ir->rs1];
            } else {
                rv->F[ir->rd] = (rv->F[ir->rs1] > rv->F[ir->rs2])
                                    ? rv->F[ir->rs1]
                                    : rv->F[ir->rs2];
            }
        }
    })

    /* FCVT.W.S */
    _(fcvtws, rv->X[ir->rd] = (int32_t) rv->F[ir->rs1];)

    /* FCVT.WU.S */
    _(fcvtwus, rv->X[ir->rd] = (uint32_t) rv->F[ir->rs1];)

    /* FMV.X.W */
    _(fmvxw, memcpy(rv->X + ir->rd, rv->F + ir->rs1, 4);)

    /* FEQ.S performs a quiet comparison: it only sets the invalid
     * operation exception flag if either input is a signaling NaN.
     */
    _(feqs, rv->X[ir->rd] = (rv->F[ir->rs1] == rv->F[ir->rs2]) ? 1 : 0;)

    /* FLT.S and FLE.S perform what the IEEE 754-2008 standard refers
     * to as signaling comparisons: that is, they set the invalid
     * operation exception flag if either input is NaN.
     */
    _(flts, rv->X[ir->rd] = (rv->F[ir->rs1] < rv->F[ir->rs2]) ? 1 : 0;)

    /* FLE.S */
    _(fles, rv->X[ir->rd] = (rv->F[ir->rs1] <= rv->F[ir->rs2]) ? 1 : 0;)

    /* FCLASS.S */
    _(fclasss, {
        uint32_t bits;
        memcpy(&bits, rv->F + ir->rs1, 4);
        rv->X[ir->rd] = calc_fclass(bits);
    })

    /* FCVT.S.W */
    _(fcvtsw, rv->F[ir->rd] = (float) (int32_t) rv->X[ir->rs1];)

    /* FCVT.S.WU */
    _(fcvtswu, rv->F[ir->rd] = (float) (uint32_t) rv->X[ir->rs1];)

    /* FMV.W.X */
    _(fmvwx, memcpy(rv->F + ir->rd, rv->X + ir->rs1, 4);)
#endif /* RV32_HAS(EXT_F) */

    /* RV32C Standard Extension */
#if RV32_HAS(EXT_C)
    /* C.ADDI4SPN is a CIW-format instruction that adds a zero-extended
     * non-zero immediate, scaledby 4, to the stack pointer, x2, and
     * writes the result to rd'. This instruction is used to generate
     * pointers to stack-allocated variables, and expands to addi rd',
     * x2, nzuimm[9:2].
     */
    _(caddi4spn, rv->X[ir->rd] = rv->X[2] + (uint16_t) ir->imm;)

    /* C.LW loads a 32-bit value from memory into register rd'. It
     * computes an ffective address by adding the zero-extended offset,
     * scaled by 4, to the base address in register rs1'. It expands to
     * # lw rd', offset[6:2](rs1').
     */
    _(clw, {
        const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
    })

    /* C.SW stores a 32-bit value in register rs2' to memory. It computes
     * an effective address by adding the zero-extended offset, scaled by
     * 4, to the base address in register rs1'.
     * It expands to sw rs2', offset[6:2](rs1')
     */
    _(csw, {
        const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
    })

    /* C.NOP */
    _(cnop, /* nothing */)

    /* C.ADDI adds the non-zero sign-extended 6-bit immediate to the
     * value in register rd then writes the result to rd. C.ADDI expands
     * into addi rd, rd, nzimm[5:0]. C.ADDI is only valid when rd̸=x0.
     * The code point with both rd=x0 and nzimm=0 encodes the C.NOP
     * instruction; the remaining code points with either rd=x0 or
     * nzimm=0 encode HINTs.
     */
    _(caddi, rv->X[ir->rd] += (int16_t) ir->imm;)

    /* C.JAL */
    _(cjal, {
        rv->X[1] = rv->PC + ir->insn_len;
        rv->PC += ir->imm;
        if (rv->PC & 0x1) {
            rv->compressed = true;
            rv_except_insn_misaligned(rv, rv->PC);
            return false;
        }
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* C.LI loads the sign-extended 6-bit immediate, imm, into
     * register rd.
     * C.LI expands into addi rd, x0, imm[5:0].
     * C.LI is only valid when rd=x0; the code points with rd=x0 encode
     * HINTs.
     */
    _(cli, rv->X[ir->rd] = ir->imm;)

    /* C.ADDI16SP is used to adjust the stack pointer in procedure
     * prologues and epilogues.
     * It expands into addi x2, x2, nzimm[9:4].
     * C.ADDI16SP is only valid when nzimm̸=0; the code point with
     * nzimm=0 is reserved.
     */
    _(caddi16sp, rv->X[ir->rd] += ir->imm;)

    /* C.LUI loads the non-zero 6-bit immediate field into bits 17–12 of
     * the destination register, clears the bottom 12 bits, and
     * sign-extends bit 17 into all higher bits of the destination.
     * C.LUI expands into lui rd, nzimm[17:12].
     * C.LUI is only valid when rd̸={x0, x2}, and when the immediate is
     * not equal to zero.
     */
    _(clui, rv->X[ir->rd] = ir->imm;)

    /* C.SRLI is a CB-format instruction that performs a logical right
     * shift of the value in register rd' then writes the result to rd'.
     * The shift amount is encoded in the shamt field. C.SRLI expands
     * into srli rd', rd', shamt[5:0].
     */
    _(csrli, rv->X[ir->rs1] >>= ir->shamt;)

    /* C.SRAI is defined analogously to C.SRLI, but instead performs an
     * arithmetic right shift.
     * C.SRAI expands to srai rd', rd', shamt[5:0].
     */
    _(csrai, {
        const uint32_t mask = 0x80000000 & rv->X[ir->rs1];
        rv->X[ir->rs1] >>= ir->shamt;
        for (unsigned int i = 0; i < ir->shamt; ++i)
            rv->X[ir->rs1] |= mask >> i;
    })

    /* C.ANDI is a CB-format instruction that computes the bitwise AND of
     * the value in register rd' and the sign-extended 6-bit immediate,
     * then writes the result to rd'.
     * C.ANDI expands to andi rd', rd', imm[5:0].
     */
    _(candi, rv->X[ir->rs1] &= ir->imm;)

    /* C.SUB */
    _(csub, rv->X[ir->rd] = rv->X[ir->rs1] - rv->X[ir->rs2];)

    /* C.XOR */
    _(cxor, rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2];)

    /* C.OR */
    _(cor, rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2];)

    /* C.AND */
    _(cand, rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2];)

    /* C.J performs an unconditional control transfer. The offset is
     * sign-extended and added to the pc to form the jump target address.
     * C.J can therefore target a ±2 KiB range.
     * C.J expands to jal x0, offset[11:1].
     */
    _(cj, {
        rv->PC += ir->imm;
        if (rv->PC & 0x1) {
            rv->compressed = true;
            rv_except_insn_misaligned(rv, rv->PC);
            return false;
        }
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* C.BEQZ performs conditional control transfers. The offset is
     * sign-extended and added to the pc to form the branch target
     * address. It can therefore target a ±256 B range. C.BEQZ takes the
     * branch if the value in register rs1' is zero.
     * It expands to beq rs1', x0, offset[8:1].
     */
    _(cbeqz, {
        rv->PC += (!rv->X[ir->rs1]) ? (uint32_t) ir->imm : ir->insn_len;
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    _(cbnez, {
        rv->PC += (rv->X[ir->rs1]) ? (uint32_t) ir->imm : ir->insn_len;
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* C.SLLI is a CI-format instruction that performs a logical left
     * shift of the value in register rd then writes the result to rd.
     * The shift amount is encoded in the shamt field.
     * C.SLLI expands into slli rd, rd, shamt[5:0].
     */
    _(cslli, rv->X[ir->rd] <<= (uint8_t) ir->imm;)

    /* C.LWSP */
    _(clwsp, {
        const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
    })

    /* C.JR */
    _(cjr, {
        rv->PC = rv->X[ir->rs1];
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* C.MV */
    _(cmv, rv->X[ir->rd] = rv->X[ir->rs2];)

    /* C.EBREAK */
    _(cebreak, {
        rv->io.on_ebreak(rv);
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* C.JALR */
    _(cjalr, {
        /* Unconditional jump and store PC+2 to ra */
        const int32_t jump_to = rv->X[ir->rs1];
        rv->X[rv_reg_ra] = rv->PC + ir->insn_len;
        rv->PC = jump_to;
        if (rv->PC & 0x1) {
            rv->compressed = true;
            rv_except_insn_misaligned(rv, rv->PC);
            return false;
        }
        /* increment the cycles csr */
        rv->csr_cycle++;
        /* can branch */
        return true;
    })

    /* C.ADD adds the values in registers rd and rs2 and writes the
     * result to register rd.
     * C.ADD expands into add rd, rd, rs2.
     * C.ADD is only valid when rs2=x0; the code points with rs2=x0
     * correspond to the C.JALR and C.EBREAK instructions. The code
     * points with rs2=x0 and rd=x0 are HINTs.
     */
    _(cadd, rv->X[ir->rd] = rv->X[ir->rs1] + rv->X[ir->rs2];)

    /* C.SWSP */
    _(cswsp, {
        const uint32_t addr = rv->X[2] + ir->imm;
        if (addr & 3) {
            rv->compressed = true;
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
    })
#endif /* RV32_HAS(EXT_C) */

#undef _

    EPILOGUE()
}

static bool insn_is_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_jal:
    case rv_insn_jalr:
    case rv_insn_beq:
    case rv_insn_bne:
    case rv_insn_blt:
    case rv_insn_bge:
    case rv_insn_bltu:
    case rv_insn_bgeu:
    case rv_insn_ecall:
    case rv_insn_ebreak:
    case rv_insn_mret:
#if RV32_HAS(EXT_C)
    case rv_insn_cj:
    case rv_insn_cjr:
    case rv_insn_cjal:
    case rv_insn_cjalr:
    case rv_insn_cbeqz:
    case rv_insn_cbnez:
    case rv_insn_cebreak:
#endif
#if RV32_HAS(Zifencei)
    case rv_insn_fencei:
#endif
        return true;
    }
    return false;
}

/* hash function is used when mapping address into the block map */
static uint32_t hash(size_t k)
{
    k ^= k << 21;
    k ^= k >> 17;
#if (SIZE_MAX > 0xFFFFFFFF)
    k ^= k >> 35;
    k ^= k >> 51;
#endif
    return k;
}

/* allocate a basic block */
static block_t *block_alloc(const uint8_t bits)
{
    block_t *block = malloc(sizeof(struct block));
    block->insn_capacity = 1 << bits;
    block->n_insn = 0;
    block->predict = NULL;
    block->ir = malloc(block->insn_capacity * sizeof(rv_insn_t));
    return block;
}

/* insert a block into block map */
static void block_insert(block_map_t *map, const block_t *block)
{
    assert(map && block);
    const uint32_t mask = map->block_capacity - 1;
    uint32_t index = hash(block->pc_start);

    /* insert into the block map */
    for (;; index++) {
        if (!map->map[index & mask]) {
            map->map[index & mask] = (block_t *) block;
            break;
        }
    }
    map->size++;
}

/* try to locate an already translated block in the block map */
static block_t *block_find(const block_map_t *map, const uint32_t addr)
{
    assert(map);
    uint32_t index = hash(addr);
    const uint32_t mask = map->block_capacity - 1;

    /* find block in block map */
    for (;; index++) {
        block_t *block = map->map[index & mask];
        if (!block)
            return NULL;

        if (block->pc_start == addr)
            return block;
    }
    return NULL;
}

static void block_translate(riscv_t *rv, block_t *block)
{
    block->pc_start = block->pc_end = rv->PC;

    /* translate the basic block */
    while (block->n_insn < block->insn_capacity) {
        rv_insn_t *ir = block->ir + block->n_insn;
        memset(ir, 0, sizeof(rv_insn_t));

        /* fetch the next instruction */
        const uint32_t insn = rv->io.mem_ifetch(rv, block->pc_end);

        /* decode the instruction */
        if (!rv_decode(ir, insn)) {
            rv->compressed = (ir->insn_len == INSN_16);
            rv_except_illegal_insn(rv, insn);
            break;
        }

        /* compute the end of pc */
        block->pc_end += ir->insn_len;
        block->n_insn++;

        /* stop on branch */
        if (insn_is_branch(ir->opcode))
            break;
    }
}

static block_t *block_find_or_translate(riscv_t *rv, block_t *prev)
{
    block_map_t *map = &rv->block_map;
    /* lookup the next block in the block map */
    block_t *next = block_find(map, rv->PC);

    if (!next) {
        if (map->size * 1.25 > map->block_capacity) {
            block_map_clear(map);
            prev = NULL;
        }

        /* allocate a new block */
        next = block_alloc(10);

        /* translate the basic block */
        block_translate(rv, next);

        /* insert the block into block map */
        block_insert(&rv->block_map, next);

        /* update the block prediction
         * When we translate a new block, the block predictor may benefit,
         * but when it is updated after we find a particular block, it may
         * penalize us significantly.
         */
        if (prev)
            prev->predict = next;
    }

    return next;
}

void rv_step(riscv_t *rv, int32_t cycles)
{
    assert(rv);

    /* find or translate a block for starting PC */
    block_t *prev = NULL;

    const uint64_t cycles_target = rv->csr_cycle + cycles;

    /* loop until we hit out cycle target */
    while (rv->csr_cycle < cycles_target && !rv->halt) {
        block_t *block;

        /* try to predict the next block */
        if (prev && prev->predict && prev->predict->pc_start == rv->PC) {
            block = prev->predict;
        } else {
            /* lookup the next block in block map or translate a new block,
             * and move onto the next block.
             */
            block = block_find_or_translate(rv, prev);
        }

        /* we should have a block by now */
        assert(block);

        /* execute the block */
        if (!emulate(rv, block))
            break;

        prev = block;
    }
}

void ebreak_handler(riscv_t *rv)
{
    assert(rv);
    rv_except_breakpoint(rv, rv->PC);
}

void ecall_handler(riscv_t *rv)
{
    assert(rv);
    rv_except_ecall_M(rv, 0);
    syscall_handler(rv);
}
