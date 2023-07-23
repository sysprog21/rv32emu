/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Internal */
RVOP(nop, {/* no operation */})

/* LUI is used to build 32-bit constants and uses the U-type format. LUI
 * places the U-immediate value in the top 20 bits of the destination
 * register rd, filling in the lowest 12 bits with zeros. The 32-bit
 * result is sign-extended to 64 bits.
 */
RVOP(lui, { rv->X[ir->rd] = ir->imm; })

/* AUIPC is used to build pc-relative addresses and uses the U-type format.
 * AUIPC forms a 32-bit offset from the 20-bit U-immediate, filling in the
 * lowest 12 bits with zeros, adds this offset to the address of the AUIPC
 * instruction, then places the result in register rd.
 */
RVOP(auipc, { rv->X[ir->rd] = ir->imm + rv->PC; })

/* JAL: Jump and Link
 * store successor instruction address into rd.
 * add next J imm (offset) to pc.
 */
RVOP(jal, {
    const uint32_t pc = rv->PC;
    /* Jump */
    rv->PC += ir->imm;
    /* link with return address */
    if (ir->rd)
        rv->X[ir->rd] = pc + ir->insn_len;
    /* check instruction misaligned */
    RV_EXC_MISALIGN_HANDLER(pc, insn, false, 0);
    return ir->branch_taken->impl(rv, ir->branch_taken);
})

/* The indirect jump instruction JALR uses the I-type encoding. The target
 * address is obtained by adding the sign-extended 12-bit I-immediate to the
 * register rs1, then setting the least-significant bit of the result to zero.
 * The address of the instruction following the jump (pc+4) is written to
 * register rd. Register x0 can be used as the destination if the result is
 * not required.
 */
RVOP(jalr, {
    const uint32_t pc = rv->PC;
    /* jump */
    rv->PC = (rv->X[ir->rs1] + ir->imm) & ~1U;
    /* link */
    if (ir->rd)
        rv->X[ir->rd] = pc + ir->insn_len;
    /* check instruction misaligned */
    RV_EXC_MISALIGN_HANDLER(pc, insn, false, 0);
    return true;
})

/* clang-format off */
#define BRANCH_FUNC(type, cond)                                       \
    const uint32_t pc = rv->PC;                                       \
    if ((type) rv->X[ir->rs1] cond (type)rv->X[ir->rs2]) {            \
        branch_taken = false;                                         \
        if (!ir->branch_untaken)                                      \
            goto nextop;                                              \
        IIF(RV32_HAS(JIT))                                            \
        (                                                             \
            if (ir->branch_untaken->pc != rv->PC + ir->insn_len ||    \
                !cache_get(rv->block_cache, rv->PC + ir->insn_len)) { \
                clear_flag = true;                                    \
                goto nextop;                                          \
            }, );                                                     \
        rv->PC += ir->insn_len;                                       \
        last_pc = rv->PC;                                             \
        return ir->branch_untaken->impl(rv, ir->branch_untaken);      \
    }                                                                 \
    branch_taken = true;                                              \
    rv->PC += ir->imm;                                                \
    /* check instruction misaligned */                                \
    RV_EXC_MISALIGN_HANDLER(pc, insn, false, 0);                      \
    if (ir->branch_taken) {                                           \
        IIF(RV32_HAS(JIT))                                            \
        (                                                             \
            if (ir->branch_taken->pc != rv->PC ||                     \
                !cache_get(rv->block_cache, rv->PC)) {                \
                clear_flag = true;                                    \
                return true;                                          \
            }, );                                                     \
        last_pc = rv->PC;                                             \
        return ir->branch_taken->impl(rv, ir->branch_taken);          \
    }                                                                 \
    return true;
/* clang-format on */

/* BEQ: Branch if Equal */
RVOP(beq, { BRANCH_FUNC(uint32_t, !=); })

/* BNE: Branch if Not Equal */
RVOP(bne, { BRANCH_FUNC(uint32_t, ==); })

/* BLT: Branch if Less Than */
RVOP(blt, { BRANCH_FUNC(int32_t, >=); })

/* BGE: Branch if Greater Than */
RVOP(bge, { BRANCH_FUNC(int32_t, <); })

/* BLTU: Branch if Less Than Unsigned */
RVOP(bltu, { BRANCH_FUNC(uint32_t, >=); })

/* BGEU: Branch if Greater Than Unsigned */
RVOP(bgeu, { BRANCH_FUNC(uint32_t, <); })

/* LB: Load Byte */
RVOP(lb, {
    rv->X[ir->rd] = sign_extend_b(rv->io.mem_read_b(rv->X[ir->rs1] + ir->imm));
})

/* LH: Load Halfword */
RVOP(lh, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(1, load, false, 1);
    rv->X[ir->rd] = sign_extend_h(rv->io.mem_read_s(addr));
})

/* LW: Load Word */
RVOP(lw, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, load, false, 1);
    rv->X[ir->rd] = rv->io.mem_read_w(addr);
})

/* LBU: Load Byte Unsigned */
RVOP(lbu, { rv->X[ir->rd] = rv->io.mem_read_b(rv->X[ir->rs1] + ir->imm); })

/* LHU: Load Halfword Unsigned */
RVOP(lhu, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(1, load, false, 1);
    rv->X[ir->rd] = rv->io.mem_read_s(addr);
})

/* SB: Store Byte */
RVOP(sb, { rv->io.mem_write_b(rv->X[ir->rs1] + ir->imm, rv->X[ir->rs2]); })

/* SH: Store Halfword */
RVOP(sh, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(1, store, false, 1);
    rv->io.mem_write_s(addr, rv->X[ir->rs2]);
})

/* SW: Store Word */
RVOP(sw, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, store, false, 1);
    rv->io.mem_write_w(addr, rv->X[ir->rs2]);
})

/* ADDI adds the sign-extended 12-bit immediate to register rs1. Arithmetic
 * overflow is ignored and the result is simply the low XLEN bits of the
 * result. ADDI rd, rs1, 0 is used to implement the MV rd, rs1 assembler
 * pseudo-instruction.
 */
RVOP(addi, { rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + ir->imm; })

/* SLTI place the value 1 in register rd if register rs1 is less than the
 * signextended immediate when both are treated as signed numbers, else 0 is
 * written to rd.
 */
RVOP(slti, { rv->X[ir->rd] = ((int32_t) (rv->X[ir->rs1]) < ir->imm) ? 1 : 0; })

/* SLTIU places the value 1 in register rd if register rs1 is less than the
 * immediate when both are treated as unsigned numbers, else 0 is written to rd.
 */
RVOP(sltiu, { rv->X[ir->rd] = (rv->X[ir->rs1] < (uint32_t) ir->imm) ? 1 : 0; })

/* XORI: Exclusive OR Immediate */
RVOP(xori, { rv->X[ir->rd] = rv->X[ir->rs1] ^ ir->imm; })

/* ORI: OR Immediate */
RVOP(ori, { rv->X[ir->rd] = rv->X[ir->rs1] | ir->imm; })

/* ANDI performs bitwise AND on register rs1 and the sign-extended 12-bit
 * immediate and place the result in rd.
 */
RVOP(andi, { rv->X[ir->rd] = rv->X[ir->rs1] & ir->imm; })

/* SLLI performs logical left shift on the value in register rs1 by the shift
 * amount held in the lower 5 bits of the immediate.
 */
RVOP(slli, { rv->X[ir->rd] = rv->X[ir->rs1] << (ir->imm & 0x1f); })

/* SRLI performs logical right shift on the value in register rs1 by the shift
 * amount held in the lower 5 bits of the immediate.
 */
RVOP(srli, { rv->X[ir->rd] = rv->X[ir->rs1] >> (ir->imm & 0x1f); })

/* SRAI performs arithmetic right shift on the value in register rs1 by the
 * shift amount held in the lower 5 bits of the immediate.
 */
RVOP(srai, { rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (ir->imm & 0x1f); })

/* ADD */
RVOP(add, {
    rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + (int32_t) (rv->X[ir->rs2]);
})

/* SUB: Substract */
RVOP(sub, {
    rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) - (int32_t) (rv->X[ir->rs2]);
})

/* SLL: Shift Left Logical */
RVOP(sll, { rv->X[ir->rd] = rv->X[ir->rs1] << (rv->X[ir->rs2] & 0x1f); })

/* SLT: Set on Less Than */
RVOP(slt, {
    rv->X[ir->rd] =
        ((int32_t) (rv->X[ir->rs1]) < (int32_t) (rv->X[ir->rs2])) ? 1 : 0;
})

/* SLTU: Set on Less Than Unsigned */
RVOP(sltu, { rv->X[ir->rd] = (rv->X[ir->rs1] < rv->X[ir->rs2]) ? 1 : 0; })

/* XOR: Exclusive OR */
RVOP(xor, {
  rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2];
})

/* SRL: Shift Right Logical */
RVOP(srl, { rv->X[ir->rd] = rv->X[ir->rs1] >> (rv->X[ir->rs2] & 0x1f); })

/* SRA: Shift Right Arithmetic */
RVOP(sra,
     { rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (rv->X[ir->rs2] & 0x1f); })

/* OR */
RVOP(or, { rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2]; })

/* AND */
RVOP(and, { rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2]; })

/* ECALL: Environment Call */
RVOP(ecall, {
    rv->compressed = false;
    rv->io.on_ecall(rv);
    return true;
})

/* EBREAK: Environment Break */
RVOP(ebreak, {
    rv->compressed = false;
    rv->io.on_ebreak(rv);
    return true;
})

/* WFI: Wait for Interrupt */
RVOP(wfi, {
    /* FIXME: Implement */
    return false;
})

/* URET: return from traps in U-mode */
RVOP(uret, {
    /* FIXME: Implement */
    return false;
})

/* SRET: return from traps in S-mode */
RVOP(sret, {
    /* FIXME: Implement */
    return false;
})

/* HRET: return from traps in H-mode */
RVOP(hret, {
    /* FIXME: Implement */
    return false;
})

/* MRET: return from traps in U-mode */
RVOP(mret, {
    rv->PC = rv->csr_mepc;
    return true;
})

#if RV32_HAS(Zifencei) /* RV32 Zifencei Standard Extension */
RVOP(fencei, {
    rv->PC += ir->insn_len;
    /* FIXME: fill real implementations */
    return true;
})
#endif

#if RV32_HAS(Zicsr) /* RV32 Zicsr Standard Extension */
/* CSRRW: Atomic Read/Write CSR */
RVOP(csrrw, {
    uint32_t tmp = csr_csrrw(rv, ir->imm, rv->X[ir->rs1]);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRS: Atomic Read and Set Bits in CSR */
RVOP(csrrs, {
    uint32_t tmp =
        csr_csrrs(rv, ir->imm, (ir->rs1 == rv_reg_zero) ? 0U : rv->X[ir->rs1]);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRC: Atomic Read and Clear Bits in CSR */
RVOP(csrrc, {
    uint32_t tmp =
        csr_csrrc(rv, ir->imm, (ir->rs1 == rv_reg_zero) ? ~0U : rv->X[ir->rs1]);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRWI */
RVOP(csrrwi, {
    uint32_t tmp = csr_csrrw(rv, ir->imm, ir->rs1);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRSI */
RVOP(csrrsi, {
    uint32_t tmp = csr_csrrs(rv, ir->imm, ir->rs1);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRCI */
RVOP(csrrci, {
    uint32_t tmp = csr_csrrc(rv, ir->imm, ir->rs1);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})
#endif

#if RV32_HAS(EXT_M) /* RV32M Standard Extension */
/* MUL: Multiply */
RVOP(mul,
     { rv->X[ir->rd] = (int32_t) rv->X[ir->rs1] * (int32_t) rv->X[ir->rs2]; })

/* MULH: Multiply High Signed Signed */
RVOP(mulh, {
    const int64_t multiplicand = (int32_t) rv->X[ir->rs1];
    const int64_t multiplier = (int32_t) rv->X[ir->rs2];
    rv->X[ir->rd] = ((uint64_t) (multiplicand * multiplier)) >> 32;
})

/* MULHSU: Multiply High Signed Unsigned */
RVOP(mulhsu, {
    const int64_t multiplicand = (int32_t) rv->X[ir->rs1];
    const uint64_t umultiplier = rv->X[ir->rs2];
    rv->X[ir->rd] = ((uint64_t) (multiplicand * umultiplier)) >> 32;
})

/* MULHU: Multiply High Unsigned Unsigned */
RVOP(mulhu, {
    rv->X[ir->rd] =
        ((uint64_t) rv->X[ir->rs1] * (uint64_t) rv->X[ir->rs2]) >> 32;
})

/* DIV: Divide Signed */
/* +------------------------+-----------+----------+-----------+
 * |       Condition        |  Dividend |  Divisor |   DIV[W]  |
 * +------------------------+-----------+----------+-----------+
 * | Division by zero       |  x        |  0       |  −1       |
 * | Overflow (signed only) |  −2^{L−1} |  −1      |  −2^{L−1} |
 * +------------------------+-----------+----------+-----------+
 */
RVOP(div, {
    const int32_t dividend = (int32_t) rv->X[ir->rs1];
    const int32_t divisor = (int32_t) rv->X[ir->rs2];
    rv->X[ir->rd] = !divisor ? ~0U
                    : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                        ? rv->X[ir->rs1] /* overflow */
                        : (unsigned int) (dividend / divisor);
})

/* DIVU: Divide Unsigned */
/* +------------------------+-----------+----------+----------+
 * |       Condition        |  Dividend |  Divisor |  DIVU[W] |
 * +------------------------+-----------+----------+----------+
 * | Division by zero       |  x        |  0       |  2^L − 1 |
 * +------------------------+-----------+----------+----------+
 */
RVOP(divu, {
    const uint32_t udividend = rv->X[ir->rs1];
    const uint32_t udivisor = rv->X[ir->rs2];
    rv->X[ir->rd] = !udivisor ? ~0U : udividend / udivisor;
})

/* REM: Remainder Signed */
/* +------------------------+-----------+----------+---------+
 * |       Condition        |  Dividend |  Divisor |  REM[W] |
 * +------------------------+-----------+----------+---------+
 * | Division by zero       |  x        |  0       |  x      |
 * | Overflow (signed only) |  −2^{L−1} |  −1      |  0      |
 * +------------------------+-----------+----------+---------+
 */
RVOP(rem, {
    const int32_t dividend = rv->X[ir->rs1];
    const int32_t divisor = rv->X[ir->rs2];
    rv->X[ir->rd] = !divisor ? dividend
                    : (divisor == -1 && rv->X[ir->rs1] == 0x80000000U)
                        ? 0 /* overflow */
                        : (dividend % divisor);
})

/* REMU: Remainder Unsigned */
/* +------------------------+-----------+----------+----------+
 * |       Condition        |  Dividend |  Divisor |  REMU[W] |
 * +------------------------+-----------+----------+----------+
 * | Division by zero       |  x        |  0       |  x       |
 * +------------------------+-----------+----------+----------+
 */
RVOP(remu, {
    const uint32_t udividend = rv->X[ir->rs1];
    const uint32_t udivisor = rv->X[ir->rs2];
    rv->X[ir->rd] = !udivisor ? udividend : udividend % udivisor;
})
#endif

#if RV32_HAS(EXT_A) /* RV32A Standard Extension */
/* At present, AMO is not implemented atomically because the emulated RISC-V
 * core just runs on single thread, and no out-of-order execution happens.
 * In addition, rl/aq are not handled.
 */

/* LR.W: Load Reserved */
RVOP(lrw, {
    rv->X[ir->rd] = rv->io.mem_read_w(rv->X[ir->rs1]);
    /* skip registration of the 'reservation set'
     * FIXME: uimplemented
     */
})

/* SC.W: Store Conditional */
RVOP(scw, {
    /* assume the 'reservation set' is valid
     * FIXME: unimplemented
     */
    rv->io.mem_write_w(rv->X[ir->rs1], rv->X[ir->rs2]);
    rv->X[ir->rd] = 0;
})

/* AMOSWAP.W: Atomic Swap */
RVOP(amoswapw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    rv->io.mem_write_s(ir->rs1, rv->X[ir->rs2]);
})

/* AMOADD.W: Atomic ADD */
RVOP(amoaddw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const int32_t res = (int32_t) rv->X[ir->rd] + (int32_t) rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, res);
})

/* AMOXOR.W: Atomix XOR */
RVOP(amoxorw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const int32_t res = rv->X[ir->rd] ^ rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, res);
})

/* AMOAND.W: Atomic AND */
RVOP(amoandw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const int32_t res = rv->X[ir->rd] & rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, res);
})

/* AMOOR.W: Atomic OR */
RVOP(amoorw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const int32_t res = rv->X[ir->rd] | rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, res);
})

/* AMOMIN.W: Atomic MIN */
RVOP(amominw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const int32_t res =
        rv->X[ir->rd] < rv->X[ir->rs2] ? rv->X[ir->rd] : rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, res);
})

/* AMOMAX.W: Atomic MAX */
RVOP(amomaxw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const int32_t res =
        rv->X[ir->rd] > rv->X[ir->rs2] ? rv->X[ir->rd] : rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, res);
})

/* AMOMINU.W */
RVOP(amominuw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const uint32_t ures =
        rv->X[ir->rd] < rv->X[ir->rs2] ? rv->X[ir->rd] : rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, ures);
})

/* AMOMAXU.W */
RVOP(amomaxuw, {
    rv->X[ir->rd] = rv->io.mem_read_w(ir->rs1);
    const uint32_t ures =
        rv->X[ir->rd] > rv->X[ir->rs2] ? rv->X[ir->rd] : rv->X[ir->rs2];
    rv->io.mem_write_s(ir->rs1, ures);
})
#endif /* RV32_HAS(EXT_A) */

#if RV32_HAS(EXT_F) /* RV32F Standard Extension */
/* FLW */
RVOP(flw, {
    /* copy into the float register */
    const uint32_t data = rv->io.mem_read_w(rv->X[ir->rs1] + ir->imm);
    rv->F_int[ir->rd] = data;
})

/* FSW */
RVOP(fsw, {
    /* copy from float registers */
    uint32_t data = rv->F_int[ir->rs2];
    rv->io.mem_write_w(rv->X[ir->rs1] + ir->imm, data);
})

/* FMADD.S */
RVOP(fmadds,
     { rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] + rv->F[ir->rs3]; })

/* FMSUB.S */
RVOP(fmsubs,
     { rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2] - rv->F[ir->rs3]; })

/* FNMSUB.S */
RVOP(fnmsubs,
     { rv->F[ir->rd] = rv->F[ir->rs3] - (rv->F[ir->rs1] * rv->F[ir->rs2]); })

/* FNMADD.S */
RVOP(fnmadds,
     { rv->F[ir->rd] = -(rv->F[ir->rs1] * rv->F[ir->rs2]) - rv->F[ir->rs3]; })

/* FADD.S */
RVOP(fadds, {
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
})

/* FSUB.S */
RVOP(fsubs, {
    if (isnanf(rv->F[ir->rs1]) || isnanf(rv->F[ir->rs2])) {
        rv->F_int[ir->rd] = RV_NAN;
    } else {
        rv->F[ir->rd] = rv->F[ir->rs1] - rv->F[ir->rs2];
    }
})

/* FMUL.S */
RVOP(fmuls, { rv->F[ir->rd] = rv->F[ir->rs1] * rv->F[ir->rs2]; })

/* FDIV.S */
RVOP(fdivs, { rv->F[ir->rd] = rv->F[ir->rs1] / rv->F[ir->rs2]; })

/* FSQRT.S */
RVOP(fsqrts, { rv->F[ir->rd] = sqrtf(rv->F[ir->rs1]); })

/* FSGNJ.S */
RVOP(fsgnjs, {
    const uint32_t ures = (((uint32_t) rv->F_int[ir->rs1]) & ~FMASK_SIGN) |
                          (((uint32_t) rv->F_int[ir->rs2]) & FMASK_SIGN);
    rv->F_int[ir->rd] = ures;
})

/* FSGNJN.S */
RVOP(fsgnjns, {
    const uint32_t ures = (((uint32_t) rv->F_int[ir->rs1]) & ~FMASK_SIGN) |
                          (~((uint32_t) rv->F_int[ir->rs2]) & FMASK_SIGN);
    rv->F_int[ir->rd] = ures;
})

/* FSGNJX.S */
RVOP(fsgnjxs, {
    const uint32_t ures = ((uint32_t) rv->F_int[ir->rs1]) ^
                          (((uint32_t) rv->F_int[ir->rs2]) & FMASK_SIGN);
    rv->F_int[ir->rd] = ures;
})

/* FMIN.S
 * In IEEE754-201x, fmin(x, y) return
 * - min(x,y) if both numbers are not NaN
 * - if one is NaN and another is a number, return the number
 * - if both are NaN, return NaN
 * When input is signaling NaN, raise invalid operation
 */
RVOP(fmins, {
    uint32_t a = rv->F_int[ir->rs1];
    uint32_t b = rv->F_int[ir->rs2];
    if (is_nan(a) || is_nan(b)) {
        if (is_snan(a) || is_snan(b))
            rv->csr_fcsr |= FFLAG_INVALID_OP;
        if (is_nan(a) && !is_nan(b)) {
            rv->F[ir->rd] = rv->F[ir->rs2];
        } else if (!is_nan(a) && is_nan(b)) {
            rv->F[ir->rd] = rv->F[ir->rs1];
        } else {
            rv->F_int[ir->rd] = RV_NAN;
        }
    } else {
        uint32_t a_sign = a & FMASK_SIGN;
        uint32_t b_sign = b & FMASK_SIGN;
        if (a_sign != b_sign) {
            rv->F[ir->rd] = a_sign ? rv->F[ir->rs1] : rv->F[ir->rs2];
        } else {
            rv->F[ir->rd] = (rv->F[ir->rs1] < rv->F[ir->rs2]) ? rv->F[ir->rs1]
                                                              : rv->F[ir->rs2];
        }
    }
})

/* FMAX.S */
RVOP(fmaxs, {
    uint32_t a = rv->F_int[ir->rs1];
    uint32_t b = rv->F_int[ir->rs2];
    if (is_nan(a) || is_nan(b)) {
        if (is_snan(a) || is_snan(b))
            rv->csr_fcsr |= FFLAG_INVALID_OP;
        if (is_nan(a) && !is_nan(b)) {
            rv->F[ir->rd] = rv->F[ir->rs2];
        } else if (!is_nan(a) && is_nan(b)) {
            rv->F[ir->rd] = rv->F[ir->rs1];
        } else {
            rv->F_int[ir->rd] = RV_NAN;
        }
    } else {
        uint32_t a_sign = a & FMASK_SIGN;
        uint32_t b_sign = b & FMASK_SIGN;
        if (a_sign != b_sign) {
            rv->F[ir->rd] = a_sign ? rv->F[ir->rs2] : rv->F[ir->rs1];
        } else {
            rv->F[ir->rd] = (rv->F[ir->rs1] > rv->F[ir->rs2]) ? rv->F[ir->rs1]
                                                              : rv->F[ir->rs2];
        }
    }
})

/* FCVT.W.S and FCVT.WU.S convert a floating point number to an integer,
 * the rounding mode is specified in rm field.
 */

/* FCVT.W.S */
RVOP(fcvtws, { rv->X[ir->rd] = (int32_t) rv->F[ir->rs1]; })

/* FCVT.WU.S */
RVOP(fcvtwus, { rv->X[ir->rd] = (uint32_t) rv->F[ir->rs1]; })

/* FMV.X.W */
RVOP(fmvxw, { rv->X[ir->rd] = rv->F_int[ir->rs1]; })

/* FEQ.S performs a quiet comparison: it only sets the invalid operation
 * exception flag if either input is a signaling NaN.
 */
RVOP(feqs, {
    rv->X[ir->rd] = (rv->F[ir->rs1] == rv->F[ir->rs2]) ? 1 : 0;
    if (is_snan(rv->F_int[ir->rs1]) || is_snan(rv->F_int[ir->rs2]))
        rv->csr_fcsr |= FFLAG_INVALID_OP;
})

/* FLT.S and FLE.S perform what the IEEE 754-2008 standard refers to as
 * signaling comparisons: that is, they set the invalid operation exception
 * flag if either input is NaN.
 */
RVOP(flts, {
    rv->X[ir->rd] = (rv->F[ir->rs1] < rv->F[ir->rs2]) ? 1 : 0;
    if (is_nan(rv->F_int[ir->rs1]) || is_nan(rv->F_int[ir->rs2]))
        rv->csr_fcsr |= FFLAG_INVALID_OP;
})

RVOP(fles, {
    rv->X[ir->rd] = (rv->F[ir->rs1] <= rv->F[ir->rs2]) ? 1 : 0;
    if (is_nan(rv->F_int[ir->rs1]) || is_nan(rv->F_int[ir->rs2]))
        rv->csr_fcsr |= FFLAG_INVALID_OP;
})

/* FCLASS.S */
RVOP(fclasss, {
    uint32_t bits = rv->F_int[ir->rs1];
    rv->X[ir->rd] = calc_fclass(bits);
})

/* FCVT.S.W */
RVOP(fcvtsw, { rv->F[ir->rd] = (int32_t) rv->X[ir->rs1]; })

/* FCVT.S.WU */
RVOP(fcvtswu, { rv->F[ir->rd] = rv->X[ir->rs1]; })

/* FMV.W.X */
RVOP(fmvwx, { rv->F_int[ir->rd] = rv->X[ir->rs1]; })
#endif

#if RV32_HAS(EXT_C) /* RV32C Standard Extension */
/* C.ADDI4SPN is a CIW-format instruction that adds a zero-extended non-zero
 * immediate, scaledby 4, to the stack pointer, x2, and writes the result to
 * rd'.
 * This instruction is used to generate pointers to stack-allocated variables,
 * and expands to addi rd', x2, nzuimm[9:2].
 */
RVOP(caddi4spn, { rv->X[ir->rd] = rv->X[2] + (uint16_t) ir->imm; })

/* C.LW loads a 32-bit value from memory into register rd'. It computes an
 * effective address by adding the zero-extended offset, scaled by 4, to the
 * base address in register rs1'. It expands to lw rd', offset[6:2](rs1').
 */
RVOP(clw, {
    const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, load, true, 1);
    rv->X[ir->rd] = rv->io.mem_read_w(addr);
})

/* C.SW stores a 32-bit value in register rs2' to memory. It computes an
 * effective address by adding the zero-extended offset, scaled by 4, to the
 * base address in register rs1'.
 * It expands to sw rs2', offset[6:2](rs1').
 */
RVOP(csw, {
    const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, store, true, 1);
    rv->io.mem_write_w(addr, rv->X[ir->rs2]);
})

/* C.NOP */
RVOP(cnop, {/* no operation */})

/* C.ADDI adds the non-zero sign-extended 6-bit immediate to the value in
 * register rd then writes the result to rd. C.ADDI expands into
 * addi rd, rd, nzimm[5:0]. C.ADDI is only valid when rd'=x0. The code point
 * with both rd=x0 and nzimm=0 encodes the C.NOP instruction; the remaining
 * code points with either rd=x0 or nzimm=0 encode HINTs.
 */
RVOP(caddi, { rv->X[ir->rd] += (int16_t) ir->imm; })

/* C.JAL */
RVOP(cjal, {
    rv->X[1] = rv->PC + ir->insn_len;
    rv->PC += ir->imm;
    RV_EXC_MISALIGN_HANDLER(rv->PC, insn, true, 0);
    return ir->branch_taken->impl(rv, ir->branch_taken);
})

/* C.LI loads the sign-extended 6-bit immediate, imm, into register rd.
 * C.LI expands into addi rd, x0, imm[5:0].
 * C.LI is only valid when rd=x0; the code points with rd=x0 encode HINTs.
 */
RVOP(cli, { rv->X[ir->rd] = ir->imm; })

/* C.ADDI16SP is used to adjust the stack pointer in procedure prologues
 * and epilogues. It expands into addi x2, x2, nzimm[9:4].
 * C.ADDI16SP is only valid when nzimm'=0; the code point with nzimm=0 is
 * reserved.
 */
RVOP(caddi16sp, { rv->X[ir->rd] += ir->imm; })

/* C.LUI loads the non-zero 6-bit immediate field into bits 17–12 of the
 * destination register, clears the bottom 12 bits, and sign-extends bit
 * 17 into all higher bits of the destination.
 * C.LUI expands into lui rd, nzimm[17:12].
 * C.LUI is only valid when rd'={x0, x2}, and when the immediate is not equal
 * to zero.
 */
RVOP(clui, { rv->X[ir->rd] = ir->imm; })

/* C.SRLI is a CB-format instruction that performs a logical right shift
 * of the value in register rd' then writes the result to rd'. The shift
 * amount is encoded in the shamt field. C.SRLI expands into srli rd',
 * rd', shamt[5:0].
 */
RVOP(csrli, { rv->X[ir->rs1] >>= ir->shamt; })

/* C.SRAI is defined analogously to C.SRLI, but instead performs an
 * arithmetic right shift. C.SRAI expands to srai rd', rd', shamt[5:0].
 */
RVOP(csrai, {
    const uint32_t mask = 0x80000000 & rv->X[ir->rs1];
    rv->X[ir->rs1] >>= ir->shamt;
    for (unsigned int i = 0; i < ir->shamt; ++i)
        rv->X[ir->rs1] |= mask >> i;
})

/* C.ANDI is a CB-format instruction that computes the bitwise AND of the
 * value in register rd' and the sign-extended 6-bit immediate, then writes
 * the result to rd'. C.ANDI expands to andi rd', rd', imm[5:0].
 */
RVOP(candi, { rv->X[ir->rs1] &= ir->imm; })

/* C.SUB */
RVOP(csub, { rv->X[ir->rd] = rv->X[ir->rs1] - rv->X[ir->rs2]; })

/* C.XOR */
RVOP(cxor, { rv->X[ir->rd] = rv->X[ir->rs1] ^ rv->X[ir->rs2]; })

RVOP(cor, { rv->X[ir->rd] = rv->X[ir->rs1] | rv->X[ir->rs2]; })

RVOP(cand, { rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2]; })

/* C.J performs an unconditional control transfer. The offset is sign-extended
 * and added to the pc to form the jump target address.
 * C.J can therefore target a ±2 KiB range.
 * C.J expands to jal x0, offset[11:1].
 */
RVOP(cj, {
    rv->PC += ir->imm;
    RV_EXC_MISALIGN_HANDLER(rv->PC, insn, true, 0);
    return ir->branch_taken->impl(rv, ir->branch_taken);
})

/* C.BEQZ performs conditional control transfers. The offset is sign-extended
 * and added to the pc to form the branch target address.
 * It can therefore target a ±256 B range. C.BEQZ takes the branch if the
 * value in register rs1' is zero. It expands to beq rs1', x0, offset[8:1].
 */
RVOP(cbeqz, {
    if (rv->X[ir->rs1]) {
        branch_taken = false;
        if (!ir->branch_untaken)
            goto nextop;
#if RV32_HAS(JIT)
        if (ir->branch_untaken->pc != rv->PC + ir->insn_len ||
            !cache_get(rv->block_cache, rv->PC + ir->insn_len)) {
            clear_flag = true;
            goto nextop;
        }
#endif
        rv->PC += ir->insn_len;
        last_pc = rv->PC;
        return ir->branch_untaken->impl(rv, ir->branch_untaken);
    }
    branch_taken = true;
    rv->PC += ir->imm;
    if (ir->branch_taken) {
#if RV32_HAS(JIT)
        if (ir->branch_taken->pc != rv->PC ||
            !cache_get(rv->block_cache, rv->PC)) {
            clear_flag = true;
            return true;
        }
#endif
        last_pc = rv->PC;
        return ir->branch_taken->impl(rv, ir->branch_taken);
    }
    return true;
})

/* C.BEQZ */
RVOP(cbnez, {
    if (!rv->X[ir->rs1]) {
        branch_taken = false;
        if (!ir->branch_untaken)
            goto nextop;
#if RV32_HAS(JIT)
        if (ir->branch_untaken->pc != rv->PC + ir->insn_len ||
            !cache_get(rv->block_cache, rv->PC + ir->insn_len)) {
            clear_flag = true;
            goto nextop;
        }
#endif
        rv->PC += ir->insn_len;
        last_pc = rv->PC;
        return ir->branch_untaken->impl(rv, ir->branch_untaken);
    }
    branch_taken = true;
    rv->PC += ir->imm;
    if (ir->branch_taken) {
#if RV32_HAS(JIT)
        if (ir->branch_taken->pc != rv->PC ||
            !cache_get(rv->block_cache, rv->PC)) {
            clear_flag = true;
            return true;
        }
#endif
        last_pc = rv->PC;
        return ir->branch_taken->impl(rv, ir->branch_taken);
    }
    return true;
})

/* C.SLLI is a CI-format instruction that performs a logical left shift of
 * the value in register rd then writes the result to rd. The shift amount
 * is encoded in the shamt field. C.SLLI expands into slli rd, rd, shamt[5:0].
 */
RVOP(cslli, { rv->X[ir->rd] <<= (uint8_t) ir->imm; })

/* C.LWSP */
RVOP(clwsp, {
    const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, load, true, 1);
    rv->X[ir->rd] = rv->io.mem_read_w(addr);
})

/* C.JR */
RVOP(cjr, {
    rv->PC = rv->X[ir->rs1];
    return true;
})

/* C.MV */
RVOP(cmv, { rv->X[ir->rd] = rv->X[ir->rs2]; })

/* C.EBREAK */
RVOP(cebreak, {
    rv->compressed = true;
    rv->io.on_ebreak(rv);
    return true;
})

/* C.JALR */
RVOP(cjalr, {
    /* Unconditional jump and store PC+2 to ra */
    const int32_t jump_to = rv->X[ir->rs1];
    rv->X[rv_reg_ra] = rv->PC + ir->insn_len;
    rv->PC = jump_to;
    RV_EXC_MISALIGN_HANDLER(rv->PC, insn, true, 0);
    return true;
})

/* C.ADD adds the values in registers rd and rs2 and writes the result to
 * register rd.
 * C.ADD expands into add rd, rd, rs2.
 * C.ADD is only valid when rs2=x0; the code points with rs2=x0 correspond to
 * the C.JALR and C.EBREAK instructions. The code points with rs2=x0 and rd=x0
 * are HINTs.
 */
RVOP(cadd, { rv->X[ir->rd] = rv->X[ir->rs1] + rv->X[ir->rs2]; })

/* C.SWSP */
RVOP(cswsp, {
    const uint32_t addr = rv->X[2] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, store, true, 1);
    rv->io.mem_write_w(addr, rv->X[ir->rs2]);
})
#endif

/* auipc + addi */
RVOP(fuse1, { rv->X[ir->rd] = (int32_t) (rv->PC + ir->imm + ir->imm2); })

/* auipc + add */
RVOP(fuse2, {
    rv->X[ir->rd] = (int32_t) (rv->X[ir->rs1]) + (int32_t) (rv->PC + ir->imm);
})

/* multiple sw */
RVOP(fuse3, {
    const opcode_fuse_t *fuse = ir->fuse;
    uint32_t addr = rv->X[fuse[0].rs1] + fuse[0].imm;
    /* the memory addresses of the sw instructions are contiguous, so we only
     * need to check the first sw instruction to determine if its memory address
     * is misaligned or if the memory chunk does not exist.
     */
    RV_EXC_MISALIGN_HANDLER(3, store, false, 1);
    rv->io.mem_write_w(addr, rv->X[fuse[0].rs2]);
    for (int i = 1; i < ir->imm2; i++) {
        addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        rv->io.mem_write_w(addr, rv->X[fuse[i].rs2]);
    }
})

/* multiple lw */
RVOP(fuse4, {
    const opcode_fuse_t *fuse = ir->fuse;
    uint32_t addr = rv->X[fuse[0].rs1] + fuse[0].imm;
    /* the memory addresses of the lw instructions are contiguous, so we only
     * need to check the first lw instruction to determine if its memory address
     * is misaligned or if the memory chunk does not exist.
     */
    RV_EXC_MISALIGN_HANDLER(3, load, false, 1);
    rv->X[fuse[0].rd] = rv->io.mem_read_w(addr);
    for (int i = 1; i < ir->imm2; i++) {
        addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        rv->X[fuse[i].rd] = rv->io.mem_read_w(addr);
    }
})
