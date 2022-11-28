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

static void rv_exception_default_handler(struct riscv_t *rv)
{
    rv->csr_mepc += rv->compressed ? INSN_16 : INSN_32;
    rv->PC = rv->csr_mepc; /* mret */
}

#define EXCEPTION_HANDLER_IMPL(type, code)                                \
    static void rv_except_##type(struct riscv_t *rv, uint32_t mtval)      \
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
static inline void update_time(struct riscv_t *rv)
{
    struct timeval tv;
    rv_gettimeofday(&tv);

    rv->csr_time = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
}

#if RV32_HAS(Zicsr)
/* get a pointer to a CSR */
static uint32_t *csr_get_ptr(struct riscv_t *rv, uint32_t csr)
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

static bool csr_is_writable(uint32_t csr)
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
static uint32_t csr_csrrw(struct riscv_t *rv, uint32_t csr, uint32_t val)
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
static uint32_t csr_csrrs(struct riscv_t *rv, uint32_t csr, uint32_t val)
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
static uint32_t csr_csrrc(struct riscv_t *rv, uint32_t csr, uint32_t val)
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
void rv_debug(struct riscv_t *rv)
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

static bool insn_is_misaligned(uint32_t pc)
{
    return (pc &
#if RV32_HAS(EXT_C)
            0x1
#else
            0x3
#endif
    );
}

static bool emulate(struct riscv_t *rv, struct rv_insn_t *ir)
{
    /* check instruction is compressed or not */
    rv->compressed = (ir->insn_len == INSN_16);

    switch (ir->opcode) {
    /* RV32I Base Instruction Set */
    case rv_insn_lui: /* LUI: Load Upper Immediate */
        /* LUI is used to build 32-bit constants and uses the U-type format. LUI
         * places the U-immediate value in the top 20 bits of the destination
         * register rd, filling in the lowest 12 bits with zeros. The 32-bit
         * result is sign-extended to 64 bits.
         */
        rv->X[ir->rd] = ir->imm;
        break;
    case rv_insn_auipc: /* AUIPC: Add Upper Immediate to PC */
        /* AUIPC is used to build pc-relative addresses and uses the U-type
         * format. AUIPC forms a 32-bit offset from the 20-bit U-immediate,
         * filling in the lowest 12 bits with zeros, adds this offset to the
         * address of the AUIPC instruction, then places the result in register
         * rd.
         */
        rv->X[ir->rd] = ir->imm + rv->PC;
        break;
    case rv_insn_jal: { /* JAL: Jump and Link */
        /* store successor instruction address into rd.
         * add next J imm (offset) to pc.
         */
        const uint32_t pc = rv->PC;
        /* Jump */
        rv->PC += ir->imm;
        /* link with return address */
        if (ir->rd)
            rv->X[ir->rd] = pc + ir->insn_len;
        /* check instruction misaligned */
        if (insn_is_misaligned(rv->PC)) {
            rv_except_insn_misaligned(rv, pc);
            return false;
        }
        /* can branch */
        return true;
    }
    case rv_insn_jalr: { /* JALR: Jump and Link Register */
        /*The indirect jump instruction JALR uses the I-type encoding. The
         * target address is obtained by adding the sign-extended 12-bit
         * I-immediate to the register rs1, then setting the least-significant
         * bit of the result to zero. The address of the instruction following
         * the jump (pc+4) is written to register rd. Register x0 can be used as
         * the destination if the result is not required.
         */
        const uint32_t pc = rv->PC;
        /* jump */
        rv->PC = (rv->X[ir->rs1] + ir->imm) & ~1U;
        /* link */
        if (ir->rd)
            rv->X[ir->rd] = pc + ir->insn_len;
        /* check instruction misaligned */
        if (insn_is_misaligned(rv->PC)) {
            rv_except_insn_misaligned(rv, pc);
            return false;
        }
        /* can branch */
        return true;
    }
    case rv_insn_beq: { /* BEQ: Branch if Equal */
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] == rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (insn_is_misaligned(rv->PC)) {
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* can branch */
            return true;
        }
        break;
    }
    case rv_insn_bne: { /* BNE: Branch if Not Equal */
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] != rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (insn_is_misaligned(rv->PC)) {
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* can branch */
            return true;
        }
        break;
    }
    case rv_insn_blt: { /* BLT: Branch if Less Than */
        const uint32_t pc = rv->PC;
        if ((int32_t) rv->X[ir->rs1] < (int32_t) rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (insn_is_misaligned(rv->PC)) {
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* can branch */
            return true;
        }
        break;
    }
    case rv_insn_bge: { /* BGE: Branch if Greater Than */
        const uint32_t pc = rv->PC;
        if ((int32_t) rv->X[ir->rs1] >= (int32_t) rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (insn_is_misaligned(rv->PC)) {
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* can branch */
            return true;
        }
        break;
    }
    case rv_insn_bltu: { /* BLTU: Branch if Less Than Unsigned */
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] < rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (insn_is_misaligned(rv->PC)) {
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* can branch */
            return true;
        }
        break;
    }
    case rv_insn_bgeu: { /* BGEU: Branch if Greater Than Unsigned */
        const uint32_t pc = rv->PC;
        if (rv->X[ir->rs1] >= rv->X[ir->rs2]) {
            rv->PC += ir->imm;
            /* check instruction misaligned */
            if (insn_is_misaligned(rv->PC)) {
                rv_except_insn_misaligned(rv, pc);
                return false;
            }
            /* can branch */
            return true;
        }
        break;
    }
    case rv_insn_lb: /* LB: Load Byte */
        rv->X[ir->rd] =
            sign_extend_b(rv->io.mem_read_b(rv, rv->X[ir->rs1] + ir->imm));
        break;
    case rv_insn_lh: { /* LH: Load Halfword */
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (addr & 1) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = sign_extend_h(rv->io.mem_read_s(rv, addr));
        break;
    }
    case rv_insn_lw: { /* LW: Load Word */
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (addr & 3) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
        break;
    }
    case rv_insn_lbu: /* LBU: Load Byte Unsigned */
        rv->X[ir->rd] = rv->io.mem_read_b(rv, rv->X[ir->rs1] + ir->imm);
        break;
    case rv_insn_lhu: { /* LHU: Load Halfword Unsigned */
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (addr & 1) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_s(rv, addr);
        break;
    }
    case rv_insn_sb: /* SB: Store Byte */
        rv->io.mem_write_b(rv, rv->X[ir->rs1] + ir->imm, rv->X[ir->rs2]);
        break;
    case rv_insn_sh: { /* SH: Store Halfword */
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (addr & 1) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_s(rv, addr, rv->X[ir->rs2]);
        break;
    }
    case rv_insn_sw: { /* SW: Store Word */
        const uint32_t addr = rv->X[ir->rs1] + ir->imm;
        if (addr & 3) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
        break;
    }
    case rv_insn_addi: /* ADDI: Add Immediate */
        /* Adds the sign-extended 12-bit immediate to register rs1. Arithmetic
         * overflow is ignored and the result is simply the low XLEN bits of the
         * result. ADDI rd, rs1, 0 is used to implement the MV rd, rs1 assembler
         * pseudo-instruction.
         */
        rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + ir->imm;
        break;
    case rv_insn_slti: /* SLTI: Set on Less Than Immediate */
        /* Place the value 1 in register rd if register rs1 is less than the
         * signextended immediate when both are treated as signed numbers, else
         * 0 is written to rd.
         */
        rv->X[ir->rd] = ((int32_t) (rv->X[ir->rs1]) < ir->imm) ? 1 : 0;
        break;
    case rv_insn_sltiu: /* SLTIU: Set on Less Than Immediate Unsigned */
        /* Place the value 1 in register rd if register rs1 is less than the
         * immediate when both are treated as unsigned numbers, else 0 is
         * written to rd.
         */
        rv->X[ir->rd] = (rv->X[ir->rs1] < (uint32_t) ir->imm) ? 1 : 0;
        break;
    case rv_insn_xori: /* XORI: Exclusive OR Immediate */
        rv->X[ir->rd] = rv->X[ir->rs1] ^ ir->imm;
        break;
    case rv_insn_ori: /* ORI: OR Immediate */
        rv->X[ir->rd] = rv->X[ir->rs1] | ir->imm;
        break;
    case rv_insn_andi: /* ANDI: AND Immediate */
        /* Performs bitwise AND on register rs1 and the sign-extended 12-bit
         * immediate and place the result in rd.
         */
        rv->X[ir->rd] = rv->X[ir->rs1] & ir->imm;
        break;
    case rv_insn_slli: /* SLLI: Shift Left Logical */
        /* Performs logical left shift on the value in register rs1 by the shift
         * amount held in the lower 5 bits of the immediate.
         */
        rv->X[ir->rd] = rv->X[ir->rs1] << (ir->imm & 0x1f);
        break;
    case rv_insn_srli: /* SRLI: Shift Right Logical */
        /* Performs logical right shift on the value in register rs1 by the
         * shift amount held in the lower 5 bits of the immediate.
         */
        rv->X[ir->rd] = rv->X[ir->rs1] >> (ir->imm & 0x1f);
        break;
    case rv_insn_srai: /* SRAI: Shift Right Arithmetic */
        /* Performs arithmetic right shift on the value in register rs1 by
         * the shift amount held in the lower 5 bits of the immediate.
         */
        rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (ir->imm & 0x1f);
        break;
    case rv_insn_add: /* ADD */
        rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + (int32_t) (rv->X[ir->rs2]);
        break;
    case rv_insn_sub: /* SUB: Substract */
        rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) - (int32_t) (rv->X[ir->rs2]);
        break;
    case rv_insn_sll: /* SLL: Shift Left Logical */
        rv->X[ir->rd] = rv->X[ir->rs1] << (rv->X[ir->rs2] & 0x1f);
        break;
    case rv_insn_slt: /* SLT: Set on Less Than */
        rv->X[ir->rd] =
            ((int32_t) (rv->X[ir->rs1]) < (int32_t) (rv->X[ir->rs2])) ? 1 : 0;
        break;
    case rv_insn_sltu: /* SLTU: Set on Less Than Unsigned */
        rv->X[ir->rd] = (rv->X[ir->rs1] < rv->X[ir->rs2]) ? 1 : 0;
        break;
    case rv_insn_xor: /* XOR: Exclusive OR */
        rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2];
        break;
    case rv_insn_srl: /* SRL: Shift Right Logical */
        rv->X[ir->rd] = rv->X[ir->rs1] >> (rv->X[ir->rs2] & 0x1f);
        break;
    case rv_insn_sra: /* SRA: Shift Right Arithmetic */
        rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (rv->X[ir->rs2] & 0x1f);
        break;
    case rv_insn_or: /* OR */
        rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2];
        break;
    case rv_insn_and: /* AND */
        rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2];
        break;
    case rv_insn_ecall: /* ECALL: Environment Call */
        rv->io.on_ecall(rv);
        return true;
    case rv_insn_ebreak: /* EBREAK: Environment Break */
        rv->io.on_ebreak(rv);
        return true;
    case rv_insn_wfi:  /* WFI: Wait for Interrupt */
    case rv_insn_uret: /* URET: return from traps in U-mode */
    case rv_insn_sret: /* SRET: return from traps in S-mode */
    case rv_insn_hret: /* HRET: return from traps in H-mode */
        /* Not support */
        return false;
    case rv_insn_mret: /* MRET: return from traps in U-mode */
        rv->PC = rv->csr_mepc;
        /* this is a branch */
        return true;

#if RV32_HAS(Zifencei)
    /* RV32 Zifencei Standard Extension */
    case rv_insn_fencei:
        /* FIXME: fill real implementations */
        break;
#endif

#if RV32_HAS(Zicsr)
    /* RV32 Zicsr Standard Extension */
    case rv_insn_csrrw: { /* CSRRW: Atomic Read/Write CSR */
        uint32_t tmp = csr_csrrw(rv, ir->imm, rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
        break;
    }
    case rv_insn_csrrs: { /* CSRRS: Atomic Read and Set Bits in CSR */
        uint32_t tmp = csr_csrrs(
            rv, ir->imm, (ir->rs1 == rv_reg_zero) ? 0U : rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
        break;
    }
    case rv_insn_csrrc: { /* CSRRC: Atomic Read and Clear Bits in CSR */
        uint32_t tmp = csr_csrrc(
            rv, ir->imm, (ir->rs1 == rv_reg_zero) ? ~0U : rv->X[ir->rs1]);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
        break;
    }
    case rv_insn_csrrwi: { /* CSRRWI */
        uint32_t tmp = csr_csrrw(rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
        break;
    }
    case rv_insn_csrrsi: { /* CSRRSI */
        uint32_t tmp = csr_csrrs(rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
        break;
    }
    case rv_insn_csrrci: { /* CSRRCI */
        uint32_t tmp = csr_csrrc(rv, ir->imm, ir->rs1);
        rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
        break;
    }
#endif

#if RV32_HAS(EXT_M)
    /* RV32M Standard Extension */
    case rv_insn_mul: /* MUL: Multiply */
        rv->X[ir->rd] = (int32_t) rv->X[ir->rs1] * (int32_t) rv->X[ir->rs2];
        break;
    case rv_insn_mulh: { /* MULH: Multiply High Signed Signed */
        const int64_t a = (int32_t) rv->X[ir->rs1],
                      b = (int32_t) rv->X[ir->rs2];
        rv->X[ir->rd] = ((uint64_t) (a * b)) >> 32;
        break;
    }
    case rv_insn_mulhsu: { /* MULHSU: Multiply High Signed Unsigned */
        const int64_t a = (int32_t) rv->X[ir->rs1];
        const uint64_t b = rv->X[ir->rs2];
        rv->X[ir->rd] = ((uint64_t) (a * b)) >> 32;
        break;
    }
    case rv_insn_mulhu: /* MULHU: Multiply High Unsigned Unsigned */
        rv->X[ir->rd] =
            ((uint64_t) rv->X[ir->rs1] * (uint64_t) rv->X[ir->rs2]) >> 32;
        break;
    case rv_insn_div: { /* DIV: Divide Signed */
        const int32_t dividend = (int32_t) rv->X[ir->rs1];
        const int32_t divisor = (int32_t) rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? ~0U
                        : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                            ? rv->X[ir->rs1] /* overflow */
                            : (unsigned int) (dividend / divisor);
        break;
    }
    case rv_insn_divu: { /* DIVU: Divide Unsigned */
        const uint32_t dividend = rv->X[ir->rs1], divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? ~0U : dividend / divisor;
        break;
    }
    case rv_insn_rem: { /* REM: Remainder Signed */
        const int32_t dividend = rv->X[ir->rs1], divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? dividend
                        : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                            ? 0 /* overflow */
                            : (dividend % divisor);
        break;
    }
    case rv_insn_remu: { /* REMU: Remainder Unsigned */
        const uint32_t dividend = rv->X[ir->rs1], divisor = rv->X[ir->rs2];
        rv->X[ir->rd] = !divisor ? dividend : dividend % divisor;
        break;
    }
#endif

#if RV32_HAS(EXT_A)
    /* At present, AMO is not implemented atomically because the emulated
     * RISC-V core just runs on single thread, and no out-of-order execution
     * happens. In addition, rl/aq are not handled.
     */
    /* RV32A Standard Extension */
    case rv_insn_lrw: /* LR.W: Load Reserved */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, rv->X[ir->rs1]);
        /* skip registration of the 'reservation set'
         * FIXME: uimplemented
         */
        break;
    case rv_insn_scw: /* SC.W: Store Conditional */
        /* assume the 'reservation set' is valid
         * FIXME: unimplemented
         */
        rv->io.mem_write_w(rv, rv->X[ir->rs1], rv->X[ir->rs2]);
        rv->X[ir->rd] = 0;
        break;
    case rv_insn_amoswapw: /* AMOSWAP.W: Atomic Swap */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        rv->io.mem_write_s(rv, ir->rs1, rv->X[ir->rs2]);
        break;
    case rv_insn_amoaddw: { /* AMOADD.W: Atomic ADD */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = (int32_t) rv->X[ir->rd] + (int32_t) rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
    case rv_insn_amoxorw: { /* AMOXOR.W: Atomix XOR */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] ^ rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
    case rv_insn_amoandw: { /* AMOAND.W: Atomic AND */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] & rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
    case rv_insn_amoorw: { /* AMOOR.W: Atomic OR */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t res = rv->X[ir->rd] | rv->X[ir->rs2];
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
    case rv_insn_amominw: { /* AMOMIN.W: Atomic MIN */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t a = rv->X[ir->rd], b = rv->X[ir->rs2];
        const int32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
    case rv_insn_amomaxw: { /* AMOMAX.W: Atomic MAX */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const int32_t a = rv->X[ir->rd], b = rv->X[ir->rs2];
        const int32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
    case rv_insn_amominuw: { /* AMOMINU.W */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const uint32_t a = rv->X[ir->rd], b = rv->X[ir->rs2];
        const uint32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
    case rv_insn_amomaxuw: { /* AMOMAXU.W */
        rv->X[ir->rd] = rv->io.mem_read_w(rv, ir->rs1);
        const uint32_t a = rv->X[ir->rd], b = rv->X[ir->rs2];
        const uint32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, ir->rs1, res);
        break;
    }
#endif /* RV32_HAS(EXT_A) */

#if RV32_HAS(EXT_F)
    /* RV32F Standard Extension */
    case rv_insn_flw: { /* FLW */
        /* copy into the float register */
        const uint32_t data = rv->io.mem_read_w(rv, rv->X[ir->rs1] + ir->imm);
        memcpy(rv->F + ir->rd, &data, 4);
        break;
    }
    case rv_insn_fsw: { /* FSW */
        /* copy from float registers */
        uint32_t data;
        memcpy(&data, (const void *) (rv->F + ir->rs2), 4);
        rv->io.mem_write_w(rv, rv->X[ir->rs1] + ir->imm, data);
        break;
    }
    case rv_insn_fmadds: /* FMADD.S */
        rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] + rv->F[ir->rs3];
        break;
    case rv_insn_fmsubs: /* FMSUB.S */
        rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] - rv->F[ir->rs3];
        break;
    case rv_insn_fnmsubs: /* FNMSUB.S */
        rv->F[ir->rd] = rv->F[ir->rs3] - (rv->F[ir->rs1] * rv->F[ir->rs2]);
        break;
    case rv_insn_fnmadds: /* FNMADD.S */
        rv->F[ir->rd] = -(rv->F[ir->rs1] * rv->F[ir->rs2]) - rv->F[ir->rs3];
        break;
    case rv_insn_fadds: /* FADD.S */
        if (isnanf(rv->F[ir->rs1]) || isnanf(rv->F[ir->rs2]) ||
            isnanf(rv->F[ir->rs1] + rv->F[ir->rs2])) {
            /* raise invalid operation */
            rv->F_int[ir->rd] = RV_NAN; /* F_int is the integer shortcut of F */
            rv->csr_fcsr |= FFLAG_INVALID_OP;
        } else {
            rv->F[ir->rd] = rv->F[ir->rs1] + rv->F[ir->rs2];
        }
        if (isinff(rv->F[ir->rd])) {
            rv->csr_fcsr |= FFLAG_OVERFLOW;
            rv->csr_fcsr |= FFLAG_INEXACT;
        }
        break;
    case rv_insn_fsubs: /* FSUB.S */
        if (isnanf(rv->F[ir->rs1]) || isnanf(rv->F[ir->rs2])) {
            rv->F_int[ir->rd] = RV_NAN;
        } else {
            rv->F[ir->rd] = rv->F[ir->rs1] - rv->F[ir->rs2];
        }
        break;
    case rv_insn_fmuls: /* FMUL.S */
        rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2];
        break;
    case rv_insn_fdivs: /* FDIV.S */
        rv->F[ir->rd] = rv->F[ir->rs1] / rv->F[ir->rs2];
        break;
    case rv_insn_fsqrts: /* FSQRT.S */
        rv->F[ir->rd] = sqrtf(rv->F[ir->rs1]);
        break;
    case rv_insn_fsgnjs: { /* FSGNJ.S */
        uint32_t f1, f2, res;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        res = (f1 & ~FMASK_SIGN) | (f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
        break;
    }
    case rv_insn_fsgnjns: { /* FSGNJN.S */
        uint32_t f1, f2, res;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        res = (f1 & ~FMASK_SIGN) | (~f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
        break;
    }
    case rv_insn_fsgnjxs: { /* FSGNJX.S */
        uint32_t f1, f2, res;
        memcpy(&f1, rv->F + ir->rs1, 4);
        memcpy(&f2, rv->F + ir->rs2, 4);
        res = f1 ^ (f2 & FMASK_SIGN);
        memcpy(rv->F + ir->rd, &res, 4);
        break;
    }
    case rv_insn_fmins: { /* FMIN.S */
        /*
         * In IEEE754-201x, fmin(x, y) return
         * - min(x,y) if both numbers are not NaN
         * - if one is NaN and another is a number, return the number
         * - if both are NaN, return NaN
         * When input is signaling NaN, raise invalid operation
         */
        uint32_t x, y;
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
            uint32_t a_sign, b_sign;
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
        break;
    }
    case rv_insn_fmaxs: { /* FMAX.S */
        uint32_t x, y;
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
            uint32_t a_sign, b_sign;
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
        break;
    }
    case rv_insn_fcvtws: /* FCVT.W.S */
        rv->X[ir->rd] = (int32_t) rv->F[ir->rs1];
        break;
    case rv_insn_fcvtwus: /* FCVT.WU.S */
        rv->X[ir->rd] = (uint32_t) rv->F[ir->rs1];
        break;
    case rv_insn_fmvxw: /* FMV.X.W */
        memcpy(rv->X + ir->rd, rv->F + ir->rs1, 4);
        break;
    case rv_insn_feqs: /* FEQ.S */
        /* FEQ.S performs a quiet comparison: it only sets the invalid
         * operation exception flag if either input is a signaling NaN.
         */
        rv->X[ir->rd] = (rv->F[ir->rs1] == rv->F[ir->rs2]) ? 1 : 0;
        break;
    case rv_insn_flts: /* FLT.S */
        /* FLT.S and FLE.S perform what the IEEE 754-2008 standard refers
         * to as signaling comparisons: that is, they set the invalid
         * operation exception flag if either input is NaN.
         */
        rv->X[ir->rd] = (rv->F[ir->rs1] < rv->F[ir->rs2]) ? 1 : 0;
        break;
    case rv_insn_fles:
        rv->X[ir->rd] = (rv->F[ir->rs1] <= rv->F[ir->rs2]) ? 1 : 0;
        break;
    case rv_insn_fclasss: { /* FCLASS.S */
        uint32_t bits;
        memcpy(&bits, rv->F + ir->rs1, 4);
        rv->X[ir->rd] = calc_fclass(bits);
        break;
    }
    case rv_insn_fcvtsw: /* FCVT.S.W */
        rv->F[ir->rd] = (float) (int32_t) rv->X[ir->rs1];
        break;
    case rv_insn_fcvtswu: /* FCVT.S.WU */
        rv->F[ir->rd] = (float) (uint32_t) rv->X[ir->rs1];
        break;
    case rv_insn_fmvwx: /* FMV.W.X */
        memcpy(rv->F + ir->rd, rv->X + ir->rs1, 4);
        break;
#endif

#if RV32_HAS(EXT_C)
    /* RV32C Standard Extension */
    case rv_insn_caddi4spn:
        /* C.ADDI4SPN is a CIW-format instruction that adds a zero-extended
         * non-zero immediate, scaledby 4, to the stack pointer, x2, and writes
         * the result to rd'. This instruction is used to generate pointers to
         * stack-allocated variables, and expands to addi rd', x2, nzuimm[9:2].
         */
        rv->X[ir->rd] = rv->X[2] + (uint16_t) ir->imm;
        break;
    case rv_insn_clw: { /* C.LW */
        /* C.LW loads a 32-bit value from memory into register rd'. It computes
         * an ffective address by adding the zero-extended offset, scaled by 4,
         * to the base address in register rs1'. It expands to  # lw rd',
         * offset[6:2](rs1').
         */
        const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
        if (addr & 3) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
        break;
    }
    case rv_insn_csw: { /* C.SW */
        /* C.SW stores a 32-bit value in register rs2' to memory. It computes an
         * effective address by adding the zero-extended offset, scaled by 4, to
         * the base address in register rs1'.
         * It expands to sw rs2', offset[6:2](rs1')
         */
        const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
        if (addr & 3) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
        break;
    }
    case rv_insn_cnop: /* C.NOP */
        /* nothing */
        break;
    case rv_insn_caddi: /* C.ADDI */
        /* C.ADDI adds the non-zero sign-extended 6-bit immediate to the value
         * in register rd then writes the result to rd. C.ADDI expands into addi
         * rd, rd, nzimm[5:0]. C.ADDI is only valid when rd̸=x0. The code point
         * with both rd=x0 and nzimm=0 encodes the C.NOP instruction; the
         * remaining code points with either rd=x0 or nzimm=0 encode HINTs.
         */
        rv->X[ir->rd] += (int16_t) ir->imm;
        break;
    case rv_insn_cjal:
        rv->X[1] = rv->PC + ir->insn_len;
        rv->PC += ir->imm;
        if (rv->PC & 0x1) {
            rv_except_insn_misaligned(rv, rv->PC);
            return false;
        }
        /* can branch */
        return true;
    case rv_insn_cli: /* C.LI */
        /* C.LI loads the sign-extended 6-bit immediate, imm, into register rd.
         * C.LI expands into addi rd, x0, imm[5:0].
         * C.LI is only valid when rd=x0; the code points with rd=x0 encode
         * HINTs.
         */
        rv->X[ir->rd] = ir->imm;
        break;
    case rv_insn_caddi16sp: /* C.ADDI16SP */
        /* C.ADDI16SP is used to adjust the stack pointer in procedure
         * prologues and epilogues.
         * It expands into addi x2, x2, nzimm[9:4].
         * C.ADDI16SP is only valid when nzimm̸=0; the code point with nzimm=0
         * is reserved.
         */
        rv->X[ir->rd] += ir->imm;
        break;
    case rv_insn_clui: /* C.CLUI */
        /* C.LUI loads the non-zero 6-bit immediate field into bits 17–12 of the
         * destination register, clears the bottom 12 bits, and sign-extends bit
         * 17 into all higher bits of the destination.
         * C.LUI expands into lui rd, nzimm[17:12].
         * C.LUI is only valid when rd̸={x0, x2}, and when the immediate is not
         * equal to zero.
         */
        rv->X[ir->rd] = ir->imm;
        break;
    case rv_insn_csrli: /* C.SRLI */
        /* C.SRLI is a CB-format instruction that performs a logical right shift
         * of the value in register rd' then writes the result to rd'. The shift
         * amount is encoded in the shamt field. C.SRLI expands into srli rd',
         * rd', shamt[5:0].
         */
        rv->X[ir->rs1] >>= ir->shamt;
        break;
    case rv_insn_csrai: { /* C.SRAI */
        /* C.SRAI is defined analogously to C.SRLI, but instead performs an
         * arithmetic right shift. C.SRAI expands to srai rd', rd', shamt[5:0].
         */
        const uint32_t mask = 0x80000000 & rv->X[ir->rs1];
        rv->X[ir->rs1] >>= ir->shamt;
        for (unsigned int i = 0; i < ir->shamt; ++i)
            rv->X[ir->rs1] |= mask >> i;
        break;
    }
    case rv_insn_candi: /* C.ANDI */
        /* C.ANDI is a CB-format instruction that computes the bitwise AND of
         * the value in register rd' and the sign-extended 6-bit immediate, then
         * writes the result to rd'. C.ANDI expands to andi rd', rd', imm[5:0].
         */
        rv->X[ir->rs1] &= ir->imm;
        break;
    case rv_insn_csub: /* C.SUB */
        rv->X[ir->rd] = rv->X[ir->rs1] - rv->X[ir->rs2];
        break;
    case rv_insn_cxor: /* C.XOR */
        rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2];
        break;
    case rv_insn_cor:
        rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2];
        break;
    case rv_insn_cand:
        rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2];
        break;
    case rv_insn_cj: /* C.J */
        /* C.J performs an unconditional control transfer. The offset is
         * sign-extended and added to the pc to form the jump target address.
         * C.J can therefore target a ±2 KiB range.
         * C.J expands to jal x0, offset[11:1].
         */
        rv->PC += ir->imm;
        if (rv->PC & 0x1) {
            rv_except_insn_misaligned(rv, rv->PC);
            return false;
        }
        /* can branch */
        return true;
    case rv_insn_cbeqz: /* C.BEQZ */
        /* BEQZ performs conditional control transfers. The offset is
         * sign-extended and added to the pc to form the branch target address.
         * It can therefore target a ±256 B range. C.BEQZ takes the branch if
         * the value in register rs1' is zero. It expands to beq rs1', x0,
         * offset[8:1].
         */
        rv->PC += (!rv->X[ir->rs1]) ? (uint32_t) ir->imm : ir->insn_len;
        /* can branch */
        return true;
    case rv_insn_cbnez: /* C.BEQZ */
        rv->PC += (rv->X[ir->rs1]) ? (uint32_t) ir->imm : ir->insn_len;
        /* can branch */
        return true;
    case rv_insn_cslli: /* C.SLLI */
        /* C.SLLI is a CI-format instruction that performs a logical left shift
         * of the value in register rd then writes the result to rd. The shift
         * amount is encoded in the shamt field. C.SLLI expands into slli rd,
         * rd, shamt[5:0].
         */
        rv->X[ir->rd] <<= (uint8_t) ir->imm;
        break;
    case rv_insn_clwsp: { /* C.LWSP */
        const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
        if (addr & 3) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[ir->rd] = rv->io.mem_read_w(rv, addr);
        break;
    }
    case rv_insn_cjr: /* C.JR */
        rv->PC = rv->X[ir->rs1];
        /* can branch */
        return true;
    case rv_insn_cmv: /* C.MV */
        rv->X[ir->rd] = rv->X[ir->rs2];
        break;
    case rv_insn_cebreak: /* C.EBREAK */
        rv->io.on_ebreak(rv);
        /* can branch */
        return true;
    case rv_insn_cjalr: { /* C.JALR */
        /* Unconditional jump and store PC+2 to ra */
        const int32_t jump_to = rv->X[ir->rs1];
        rv->X[rv_reg_ra] = rv->PC + ir->insn_len;
        rv->PC = jump_to;
        if (rv->PC & 0x1) {
            rv_except_insn_misaligned(rv, rv->PC);
            return false;
        }
        /* can branch */
        return true;
    }
    case rv_insn_cadd: /* C.ADD */
        /* C.ADD adds the values in registers rd and rs2 and writes the
         * result to register rd.
         * C.ADD expands into add rd, rd, rs2.
         * C.ADD is only valid when rs2=x0; the code points with rs2=x0
         * correspond to the C.JALR and C.EBREAK instructions. The code
         * points with rs2=x0 and rd=x0 are HINTs.
         */
        rv->X[ir->rd] = rv->X[ir->rs1] + rv->X[ir->rs2];
        break;
    case rv_insn_cswsp: { /* C.SWSP */
        const uint32_t addr = rv->X[2] + ir->imm;
        if (addr & 3) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, rv->X[ir->rs2]);
        break;
    }
#endif
    }

    /* step over instruction */
    rv->PC += ir->insn_len;
    return true;
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
static struct block *block_alloc(const uint8_t bits)
{
    struct block *block = malloc(sizeof(struct block));
    block->insn_capacity = 1 << bits;
    block->insn_number = 0;
    block->predict = NULL;
    block->ir = malloc(block->insn_capacity * sizeof(struct rv_insn_t));
    return block;
}

/* insert a block into block map */
static void block_insert(struct block_map *map, const struct block *block)
{
    assert(map && block);
    const uint32_t mask = map->block_capacity - 1;
    uint32_t index = hash(block->pc_start);

    /* insert into the block map */
    for (;; index++) {
        if (!map->map[index & mask]) {
            map->map[index & mask] = (struct block *) block;
            break;
        }
    }
    map->size++;
}

/* try to locate an already translated block in the block map */
static struct block *block_find(const struct block_map *map, const uint32_t pc)
{
    assert(map);
    uint32_t index = hash(pc);
    const uint32_t mask = map->block_capacity - 1;

    /* find block in block map */
    for (;; index++) {
        struct block *block = map->map[index & mask];
        if (!block)
            return NULL;

        if (block->pc_start == pc)
            return block;
    }
    return NULL;
}

/* execute a basic block */
static bool block_emulate(struct riscv_t *rv, const struct block *block)
{
    /* execute the block */
    for (uint32_t i = 0; i < block->insn_number; i++) {
        /* enforce zero register */
        rv->X[rv_reg_zero] = 0;

        /* execute the instruction */
        if (!emulate(rv, block->ir + i))
            return false;

        /* increment the cycles csr */
        rv->csr_cycle++;
    }
    return true;
}

static void block_translate(struct riscv_t *rv, struct block *block)
{
    block->pc_start = rv->PC;
    block->pc_end = rv->PC;

    /* translate the basic block */
    while (block->insn_number < block->insn_capacity) {
        struct rv_insn_t *ir = block->ir + block->insn_number;
        memset(ir, 0, sizeof(struct rv_insn_t));

        /* fetch the next instruction */
        const uint32_t insn = rv->io.mem_ifetch(rv, block->pc_end);

        /* decode the instruction */
        if (!rv_decode(ir, insn)) {
            rv_except_illegal_insn(rv, insn);
            break;
        }

        /* compute the end of pc */
        block->pc_end += ir->insn_len;
        block->insn_number++;

        /* stop on branch */
        if (insn_is_branch(ir->opcode))
            break;
    }
}

static struct block *block_find_or_translate(struct riscv_t *rv,
                                             struct block *prev)
{
    struct block_map *map = &rv->block_map;
    /* lookup the next block in the block map */
    struct block *next = block_find(map, rv->PC);

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

        /* update the block prediction */
        if (prev)
            prev->predict = next;
    }

    return next;
}

void rv_step(struct riscv_t *rv, int32_t cycles)
{
    assert(rv);

    /* find or translate a block for starting PC */
    struct block *prev = NULL, *next = NULL;
    const uint64_t cycles_target = rv->csr_cycle + cycles;

    while (rv->csr_cycle < cycles_target && !rv->halt) {
        /* check the block prediction first */
        if (prev && prev->predict && prev->predict->pc_start == rv->PC) {
            next = prev->predict;
        } else {
            next = block_find_or_translate(rv, prev);
        }

        assert(next);

        /* execute the block */
        if (!block_emulate(rv, next))
            break;

        prev = next;
    }
}

void ebreak_handler(struct riscv_t *rv)
{
    assert(rv);
    rv_except_breakpoint(rv, rv->PC);
}

void ecall_handler(struct riscv_t *rv)
{
    assert(rv);
    rv_except_ecall_M(rv, 0);
    syscall_handler(rv);
}
