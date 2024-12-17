/* RV32I Base Instruction Set */

/* Internal */
CONSTOPT(nop, {})

/* LUI is used to build 32-bit constants and uses the U-type format. LUI
 * places the U-immediate value in the top 20 bits of the destination
 * register rd, filling in the lowest 12 bits with zeros. The 32-bit
 * result is sign-extended to 64 bits.
 */
CONSTOPT(lui, {
    info->is_constant[ir->rd] = true;
    info->const_val[ir->rd] = ir->imm;
})

/* AUIPC is used to build pc-relative addresses and uses the U-type format.
 * AUIPC forms a 32-bit offset from the 20-bit U-immediate, filling in the
 * lowest 12 bits with zeros, adds this offset to the address of the AUIPC
 * instruction, then places the result in register rd.
 */
CONSTOPT(auipc, {
    ir->imm += ir->pc;
    info->is_constant[ir->rd] = true;
    info->const_val[ir->rd] = ir->imm;
    ir->opcode = rv_insn_lui;
    ir->impl = dispatch_table[ir->opcode];
})

/* JAL: Jump and Link
 * store successor instruction address into rd.
 * add next J imm (offset) to pc.
 */
CONSTOPT(jal, {
    if (ir->rd) {
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->pc + 4;
    }
})

/* The indirect jump instruction JALR uses the I-type encoding. The target
 * address is obtained by adding the sign-extended 12-bit I-immediate to the
 * register rs1, then setting the least-significant bit of the result to zero.
 * The address of the instruction following the jump (pc+4) is written to
 * register rd. Register x0 can be used as the destination if the result is
 * not required.
 */
CONSTOPT(jalr, {
    if (ir->rd) {
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->pc + 4;
    }
})

/* clang-format off */
#define OPT_BRANCH_FUNC(type, cond)                                 \
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) { \
        if ((type) info->const_val[ir->rs1] cond                    \
            (type) info->const_val[ir->rs2])                        \
            ir->imm = 4;                                            \
        ir->opcode = rv_insn_jal;                                   \
        ir->impl = dispatch_table[ir->opcode];                      \
    }
/* clang-format on */

/* BEQ: Branch if Equal */
CONSTOPT(beq, { OPT_BRANCH_FUNC(uint32_t, !=); })

/* BNE: Branch if Not Equal */
CONSTOPT(bne, { OPT_BRANCH_FUNC(uint32_t, ==); })

/* BLT: Branch if Less Than */
CONSTOPT(blt, { OPT_BRANCH_FUNC(int32_t, >=); })

/* BGE: Branch if Greater Than */
CONSTOPT(bge, { OPT_BRANCH_FUNC(int32_t, <); })

/* BLTU: Branch if Less Than Unsigned */
CONSTOPT(bltu, { OPT_BRANCH_FUNC(uint32_t, >=); })

/* BGEU: Branch if Greater Than Unsigned */
CONSTOPT(bgeu, { OPT_BRANCH_FUNC(uint32_t, <); })

/* LB: Load Byte */
CONSTOPT(lb, { info->is_constant[ir->rd] = false; })

/* LH: Load Halfword */
CONSTOPT(lh, { info->is_constant[ir->rd] = false; })

/* LW: Load Word */
CONSTOPT(lw, { info->is_constant[ir->rd] = false; })

/* LBU: Load Byte Unsigned */
CONSTOPT(lbu, { info->is_constant[ir->rd] = false; })

/* LHU: Load Halfword Unsigned */
CONSTOPT(lhu, { info->is_constant[ir->rd] = false; })

/* SB: Store Byte */
CONSTOPT(sb, {})

/* SH: Store Halfword */
CONSTOPT(sh, {})

/* SW: Store Word */
CONSTOPT(sw, {})

/* ADDI adds the sign-extended 12-bit immediate to register rs1. Arithmetic
 * overflow is ignored and the result is simply the low XLEN bits of the
 * result. ADDI rd, rs1, 0 is used to implement the MV rd, rs1 assembler
 * pseudo-instruction.
 */
CONSTOPT(addi, {
    if (info->is_constant[ir->rs1]) {
        ir->imm += info->const_val[ir->rs1];
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SLTI place the value 1 in register rd if register rs1 is less than the
 * signextended immediate when both are treated as signed numbers, else 0 is
 * written to rd.
 */
CONSTOPT(slti, {
    if (info->is_constant[ir->rs1]) {
        ir->imm = (int32_t) info->const_val[ir->rs1] < ir->imm ? 1 : 0;
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SLTIU places the value 1 in register rd if register rs1 is less than the
 * immediate when both are treated as unsigned numbers, else 0 is written to rd.
 */
CONSTOPT(sltiu, {
    if (info->is_constant[ir->rs1]) {
        ir->imm = info->const_val[ir->rs1] < (uint32_t) ir->imm ? 1 : 0;
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* XORI: Exclusive OR Immediate */
CONSTOPT(xori, {
    if (info->is_constant[ir->rs1]) {
        ir->imm ^= info->const_val[ir->rs1];
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* ORI: OR Immediate */
CONSTOPT(ori, {
    if (info->is_constant[ir->rs1]) {
        ir->imm |= info->const_val[ir->rs1];
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* ANDI performs bitwise AND on register rs1 and the sign-extended 12-bit
 * immediate and place the result in rd.
 */
CONSTOPT(andi, {
    if (info->is_constant[ir->rs1]) {
        ir->imm &= info->const_val[ir->rs1];
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SLLI performs logical left shift on the value in register rs1 by the shift
 * amount held in the lower 5 bits of the immediate.
 */
CONSTOPT(slli, {
    if (info->is_constant[ir->rs1]) {
        ir->imm = info->const_val[ir->rs1] << (ir->imm & 0x1f);
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SRLI performs logical right shift on the value in register rs1 by the shift
 * amount held in the lower 5 bits of the immediate.
 */
CONSTOPT(srli, {
    if (info->is_constant[ir->rs1]) {
        ir->imm = info->const_val[ir->rs1] >> (ir->imm & 0x1f);
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SRAI performs arithmetic right shift on the value in register rs1 by the
 * shift amount held in the lower 5 bits of the immediate.
 */
CONSTOPT(srai, {
    if (info->is_constant[ir->rs1]) {
        ir->imm = (int32_t) info->const_val[ir->rs1] >> (ir->imm & 0x1f);
        info->is_constant[ir->rd] = true;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* ADD */
CONSTOPT(add, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] + info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SUB: Substract */
CONSTOPT(sub, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] - info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SLL: Shift Left Logical */
CONSTOPT(sll, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] << (info->const_val[ir->rs2] & 0x1f);
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SLT: Set on Less Than */
CONSTOPT(slt, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = (int32_t) info->const_val[ir->rs1] <
                          (int32_t) info->const_val[ir->rs2]
                      ? 1
                      : 0;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SLTU: Set on Less Than Unsigned */
CONSTOPT(sltu, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] < info->const_val[ir->rs2] ? 1 : 0;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* XOR: Exclusive OR */
CONSTOPT(xor, {
  if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
      info->is_constant[ir->rd] = true;
      ir->imm = (int32_t) info->const_val[ir->rs1] ^
                (int32_t) info->const_val[ir->rs2];
      info->const_val[ir->rd] = ir->imm;
      ir->opcode = rv_insn_lui;
      ir->impl = dispatch_table[ir->opcode];
  } else
      info->is_constant[ir->rd] = false;
})

/* SRL: Shift Right Logical */
CONSTOPT(srl, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] >> (info->const_val[ir->rs2] & 0x1f);
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* SRA: Shift Right Arithmetic */
CONSTOPT(sra, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = (int32_t) info->const_val[ir->rs1] >>
                  (info->const_val[ir->rs2] & 0x1f);
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* OR */
CONSTOPT(or, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = (int32_t) info->const_val[ir->rs1] |
                  (int32_t) info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* AND */
CONSTOPT(and, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = (int32_t) info->const_val[ir->rs1] &
                  (int32_t) info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/*
 * FENCE: order device I/O and memory accesses as viewed by other
 * RISC-V harts and external devices or coprocessors
 */
CONSTOPT(fence, {})

/* ECALL: Environment Call */
CONSTOPT(ecall, {})

/* EBREAK: Environment Break */
CONSTOPT(ebreak, {})

/* WFI: Wait for Interrupt */
CONSTOPT(wfi, {})

/* URET: return from traps in U-mode */
CONSTOPT(uret, {})

#if RV32_HAS(SYSTEM)
/* SRET: return from traps in S-mode */
CONSTOPT(sret, {})
#endif

/* HRET: return from traps in H-mode */
CONSTOPT(hret, {})

/* MRET: return from traps in M-mode */
CONSTOPT(mret, {})

/* SFENCE.VMA: synchronize updates to in-memory memory-management data
 * structures with current execution
 */
CONSTOPT(sfencevma, {})

#if RV32_HAS(Zifencei) /* RV32 Zifencei Standard Extension */
CONSTOPT(fencei, {})
#endif

#if RV32_HAS(Zicsr) /* RV32 Zicsr Standard Extension */
/* CSRRW: Atomic Read/Write CSR */
CONSTOPT(csrrw, { info->is_constant[ir->rd] = false; })

/* CSRRS: Atomic Read and Set Bits in CSR */
CONSTOPT(csrrs, { info->is_constant[ir->rd] = false; })

/* CSRRC: Atomic Read and Clear Bits in CSR */
CONSTOPT(csrrc, { info->is_constant[ir->rd] = false; })

/* CSRRWI */
CONSTOPT(csrrwi, { info->is_constant[ir->rd] = false; })

/* CSRRSI */
CONSTOPT(csrrsi, { info->is_constant[ir->rd] = false; })

/* CSRRCI */
CONSTOPT(csrrci, { info->is_constant[ir->rd] = false; })
#endif

/* RV32M Standard Extension */

#if RV32_HAS(EXT_M)
/* MUL: Multiply */
CONSTOPT(mul, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        const int64_t multiplicand = (int32_t) info->const_val[ir->rs1];
        const int64_t multiplier = (int32_t) info->const_val[ir->rs2];
        ir->imm = ((uint64_t) (multiplicand * multiplier)) & ((1ULL << 32) - 1);
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* MULH: Multiply High Signed Signed */
CONSTOPT(mulh, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        const int64_t a = (int32_t) info->const_val[ir->rs1];
        const int64_t b = (int32_t) info->const_val[ir->rs2];
        ir->imm = ((uint64_t) (a * b)) >> 32;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* MULHSU: Multiply High Signed Unsigned */
CONSTOPT(mulhsu, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        const int64_t a = (int32_t) info->const_val[ir->rs1];
        const int64_t b = info->const_val[ir->rs2];
        ir->imm = ((uint64_t) (a * b)) >> 32;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* MULHU: Multiply High Unsigned Unsigned */
CONSTOPT(mulhu, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = ((int64_t) info->const_val[ir->rs1] *
                   (int64_t) info->const_val[ir->rs2]) >>
                  32;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* DIV: Divide Signed */
/* +------------------------+-----------+----------+-----------+
 * |       Condition        |  Dividend |  Divisor |   DIV[W]  |
 * +------------------------+-----------+----------+-----------+
 * | Division by zero       |  x        |  0       |  −1       |
 * | Overflow (signed only) |  −2^{L−1} |  −1      |  −2^{L−1} |
 * +------------------------+-----------+----------+-----------+
 */
CONSTOPT(div, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        const int32_t dividend = (int32_t) info->const_val[ir->rs1];
        const int32_t divisor = (int32_t) info->const_val[ir->rs2];
        ir->imm = !divisor ? ~0U
                  : (divisor == -1 && info->const_val[ir->rs1] == 0x80000000U)
                      ? info->const_val[ir->rs1] /* overflow */
                      : (unsigned int) (dividend / divisor);
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* DIVU: Divide Unsigned */
/* +------------------------+-----------+----------+----------+
 * |       Condition        |  Dividend |  Divisor |  DIVU[W] |
 * +------------------------+-----------+----------+----------+
 * | Division by zero       |  x        |  0       |  2^L − 1 |
 * +------------------------+-----------+----------+----------+
 */
CONSTOPT(divu, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        const uint32_t dividend = info->const_val[ir->rs1];
        const uint32_t divisor = info->const_val[ir->rs2];
        ir->imm = !divisor ? ~0U : dividend / divisor;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* REM: Remainder Signed */
/* +------------------------+-----------+----------+---------+
 * |       Condition        |  Dividend |  Divisor |  REM[W] |
 * +------------------------+-----------+----------+---------+
 * | Division by zero       |  x        |  0       |  x      |
 * | Overflow (signed only) |  −2^{L−1} |  −1      |  0      |
 * +------------------------+-----------+----------+---------+
 */
CONSTOPT(rem, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        const int32_t dividend = info->const_val[ir->rs1];
        const int32_t divisor = info->const_val[ir->rs2];
        ir->imm = !divisor ? dividend
                  : (divisor == -1 && info->const_val[ir->rs1] == 0x80000000U)
                      ? 0 /* overflow */
                      : (dividend % divisor);
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* REMU: Remainder Unsigned */
/* +------------------------+-----------+----------+----------+
 * |       Condition        |  Dividend |  Divisor |  REMU[W] |
 * +------------------------+-----------+----------+----------+
 * | Division by zero       |  x        |  0       |  x       |
 * +------------------------+-----------+----------+----------+
 */
CONSTOPT(remu, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        const uint32_t dividend = info->const_val[ir->rs1];
        const uint32_t divisor = info->const_val[ir->rs2];
        ir->imm = !divisor ? dividend : dividend % divisor;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_lui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})
#endif

/* RV32A Standard Extension */
/* TODO: support constant optimization for A and F extension */
#if RV32_HAS(EXT_A)

/* LR.W: Load Reserved */
CONSTOPT(lrw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* SC.W: Store Conditional */
CONSTOPT(scw, {})

/* AMOSWAP.W: Atomic Swap */
CONSTOPT(amoswapw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOADD.W: Atomic ADD */
CONSTOPT(amoaddw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOXOR.W: Atomic XOR */
CONSTOPT(amoxorw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOAND.W: Atomic AND */
CONSTOPT(amoandw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOOR.W: Atomic OR */
CONSTOPT(amoorw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOMIN.W: Atomic MIN */
CONSTOPT(amominw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOMAX.W: Atomic MAX */
CONSTOPT(amomaxw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOMINU.W */
CONSTOPT(amominuw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* AMOMAXU.W */
CONSTOPT(amomaxuw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})
#endif /* RV32_HAS(EXT_A) */

/* RV32F Standard Extension */

#if RV32_HAS(EXT_F)
/* FLW */
CONSTOPT(flw, {})

/* FSW */
CONSTOPT(fsw, {})

/* FMADD.S */
CONSTOPT(fmadds, {})

/* FMSUB.S */
CONSTOPT(fmsubs, {})

/* FNMSUB.S */
CONSTOPT(fnmsubs, {})

/* FNMADD.S */
CONSTOPT(fnmadds, {})

/* FADD.S */
CONSTOPT(fadds, {})

/* FSUB.S */
CONSTOPT(fsubs, {})

/* FMUL.S */
CONSTOPT(fmuls, {})

/* FDIV.S */
CONSTOPT(fdivs, {})

/* FSQRT.S */
CONSTOPT(fsqrts, {})

/* FSGNJ.S */
CONSTOPT(fsgnjs, {})

/* FSGNJN.S */
CONSTOPT(fsgnjns, {})

/* FSGNJX.S */
CONSTOPT(fsgnjxs, {})

/* FMIN.S
 * In IEEE754-201x, fmin(x, y) return
 * - min(x,y) if both numbers are not NaN
 * - if one is NaN and another is a number, return the number
 * - if both are NaN, return NaN
 * When input is signaling NaN, raise invalid operation
 */
CONSTOPT(fmins, {})

/* FMAX.S */
CONSTOPT(fmaxs, {})

/* FCVT.W.S and FCVT.WU.S convert a floating point number to an integer,
 * the rounding mode is specified in rm field.
 */

/* FCVT.W.S */
CONSTOPT(fcvtws, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* FCVT.WU.S */
CONSTOPT(fcvtwus, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* FMV.X.W */
CONSTOPT(fmvxw, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* FEQ.S performs a quiet comparison: it only sets the invalid operation
 * exception flag if either input is a signaling NaN.
 */
CONSTOPT(feqs, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* FLT.S and FLE.S perform what the IEEE 754-2008 standard refers to as
 * signaling comparisons: that is, they set the invalid operation exception
 * flag if either input is NaN.
 */
CONSTOPT(flts, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

CONSTOPT(fles, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* FCLASS.S */
CONSTOPT(fclasss, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* FCVT.S.W */
CONSTOPT(fcvtsw, {})

/* FCVT.S.WU */
CONSTOPT(fcvtswu, {})

/* FMV.W.X */
CONSTOPT(fmvwx, {})
#endif

/* RV32C Standard Extension */

#if RV32_HAS(EXT_C)
/* C.ADDI4SPN is a CIW-format instruction that adds a zero-extended non-zero
 * immediate, scaledby 4, to the stack pointer, x2, and writes the result to
 * rd'.
 * This instruction is used to generate pointers to stack-allocated variables,
 * and expands to addi rd', x2, nzuimm[9:2].
 */
CONSTOPT(caddi4spn, {
    if (info->is_constant[rv_reg_sp]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[rv_reg_sp] + (uint16_t) ir->imm;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* C.LW loads a 32-bit value from memory into register rd'. It computes an
 * effective address by adding the zero-extended offset, scaled by 4, to the
 * base address in register rs1'. It expands to lw rd', offset[6:2](rs1').
 */
CONSTOPT(clw, { info->is_constant[ir->rd] = false; })

/* C.SW stores a 32-bit value in register rs2' to memory. It computes an
 * effective address by adding the zero-extended offset, scaled by 4, to the
 * base address in register rs1'.
 * It expands to sw rs2', offset[6:2](rs1').
 */
CONSTOPT(csw, {})

/* C.NOP */
CONSTOPT(cnop, {})

/* C.ADDI adds the non-zero sign-extended 6-bit immediate to the value in
 * register rd then writes the result to rd. C.ADDI expands into
 * addi rd, rd, nzimm[5:0]. C.ADDI is only valid when rd'=x0. The code point
 * with both rd=x0 and nzimm=0 encodes the C.NOP instruction; the remaining
 * code points with either rd=x0 or nzimm=0 encode HINTs.
 */
CONSTOPT(caddi, {
    if (info->is_constant[ir->rd]) {
        ir->imm = info->const_val[ir->rd] + (int16_t) ir->imm;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.JAL */
CONSTOPT(cjal, {
    info->is_constant[rv_reg_ra] = true;
    info->const_val[rv_reg_ra] = ir->pc + 2;
})

/* C.LI loads the sign-extended 6-bit immediate, imm, into register rd.
 * C.LI expands into addi rd, x0, imm[5:0].
 * C.LI is only valid when rd=x0; the code points with rd=x0 encode HINTs.
 */
CONSTOPT(cli, {
    info->is_constant[ir->rd] = true;
    info->const_val[ir->rd] = ir->imm;
})

/* C.ADDI16SP is used to adjust the stack pointer in procedure prologues
 * and epilogues. It expands into addi x2, x2, nzimm[9:4].
 * C.ADDI16SP is only valid when nzimm'=0; the code point with nzimm=0 is
 * reserved.
 */
CONSTOPT(caddi16sp, {
    if (info->is_constant[ir->rd]) {
        ir->imm = info->const_val[ir->rd] + ir->imm;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.LUI loads the non-zero 6-bit immediate field into bits 17–12 of the
 * destination register, clears the bottom 12 bits, and sign-extends bit
 * 17 into all higher bits of the destination.
 * C.LUI expands into lui rd, nzimm[17:12].
 * C.LUI is only valid when rd'={x0, x2}, and when the immediate is not equal
 * to zero.
 */
CONSTOPT(clui, {
    info->is_constant[ir->rd] = true;
    info->const_val[ir->rd] = ir->imm;
})

/* C.SRLI is a CB-format instruction that performs a logical right shift
 * of the value in register rd' then writes the result to rd'. The shift
 * amount is encoded in the shamt field. C.SRLI expands into srli rd',
 * rd', shamt[5:0].
 */
CONSTOPT(csrli, {
    if (info->is_constant[ir->rs1]) {
        ir->imm = info->const_val[ir->rs1] >> ir->shamt;
        info->const_val[ir->rs1] = ir->imm;
        ir->rd = ir->rs1;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.SRAI is defined analogously to C.SRLI, but instead performs an
 * arithmetic right shift. C.SRAI expands to srai rd', rd', shamt[5:0].
 */
CONSTOPT(csrai, {
    if (info->is_constant[ir->rs1]) {
        const uint32_t mask = 0x80000000 & info->const_val[ir->rs1];
        ir->imm = info->const_val[ir->rs1] >> ir->shamt;
        for (unsigned int i = 0; i < ir->shamt; ++i)
            ir->imm |= mask >> i;
        info->const_val[ir->rs1] = ir->imm;
        ir->rd = ir->rs1;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.ANDI is a CB-format instruction that computes the bitwise AND of the
 * value in register rd' and the sign-extended 6-bit immediate, then writes
 * the result to rd'. C.ANDI expands to andi rd', rd', imm[5:0].
 */
CONSTOPT(candi, {
    if (info->is_constant[ir->rs1]) {
        ir->imm = info->const_val[ir->rs1] & ir->imm;
        info->const_val[ir->rs1] = ir->imm;
        ir->rd = ir->rs1;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.SUB */
CONSTOPT(csub, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] - info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* C.XOR */
CONSTOPT(cxor, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] ^ info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

CONSTOPT(cor, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] | info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

CONSTOPT(cand, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] & info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* C.J performs an unconditional control transfer. The offset is sign-extended
 * and added to the pc to form the jump target address.
 * C.J can therefore target a ±2 KiB range.
 * C.J expands to jal x0, offset[11:1].
 */
CONSTOPT(cj, {})

/* C.BEQZ performs conditional control transfers. The offset is sign-extended
 * and added to the pc to form the branch target address.
 * It can therefore target a ±256 B range. C.BEQZ takes the branch if the
 * value in register rs1' is zero. It expands to beq rs1', x0, offset[8:1].
 */
CONSTOPT(cbeqz, {
    if (info->is_constant[ir->rs1]) {
        if (info->const_val[ir->rs1])
            ir->imm = 2;
        ir->opcode = rv_insn_cj;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.BEQZ */
CONSTOPT(cbnez, {
    if (info->is_constant[ir->rs1]) {
        if (!info->const_val[ir->rs1])
            ir->imm = 2;
        ir->opcode = rv_insn_cj;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.SLLI is a CI-format instruction that performs a logical left shift of
 * the value in register rd then writes the result to rd. The shift amount
 * is encoded in the shamt field. C.SLLI expands into slli rd, rd, shamt[5:0].
 */
CONSTOPT(cslli, {
    if (info->is_constant[ir->rd]) {
        ir->imm = info->const_val[ir->rd] << (uint8_t) ir->imm;
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    }
})

/* C.LWSP */
CONSTOPT(clwsp, { info->is_constant[ir->rd] = false; })

/* C.JR */
CONSTOPT(cjr, {})

/* C.MV */
CONSTOPT(cmv, {
    if (info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    } else {
        info->is_constant[ir->rd] = false;
    }
})

/* C.EBREAK */
CONSTOPT(cebreak, {})

/* C.JALR */
CONSTOPT(cjalr, {
    info->is_constant[rv_reg_ra] = true;
    info->const_val[ir->rd] = ir->pc + 2;
})

/* C.ADD adds the values in registers rd and rs2 and writes the result to
 * register rd.
 * C.ADD expands into add rd, rd, rs2.
 * C.ADD is only valid when rs2=x0; the code points with rs2=x0 correspond to
 * the C.JALR and C.EBREAK instructions. The code points with rs2=x0 and rd=x0
 * are HINTs.
 */
CONSTOPT(cadd, {
    if (info->is_constant[ir->rs1] && info->is_constant[ir->rs2]) {
        info->is_constant[ir->rd] = true;
        ir->imm = info->const_val[ir->rs1] + info->const_val[ir->rs2];
        info->const_val[ir->rd] = ir->imm;
        ir->opcode = rv_insn_clui;
        ir->impl = dispatch_table[ir->opcode];
    } else
        info->is_constant[ir->rd] = false;
})

/* C.SWSP */
CONSTOPT(cswsp, {})
#endif

/* RV32FC Standard Extension */

#if RV32_HAS(EXT_F) && RV32_HAS(EXT_C)
/* C.FLWSP */
CONSTOPT(cflwsp, {})

/* C.FSWSP */
CONSTOPT(cfswsp, {})

/* C.FLW */
CONSTOPT(cflw, {})

/* C.FSW */
CONSTOPT(cfsw, {})
#endif

#if RV32_HAS(Zba)
/* SH1ADD */
CONSTOPT(sh1add, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* SH2ADD */
CONSTOPT(sh2add, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* SH3ADD */
CONSTOPT(sh3add, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})
#endif

#if RV32_HAS(Zbb)
/* ANDN */
CONSTOPT(andn, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* ORN */
CONSTOPT(orn, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* XNOR */
CONSTOPT(xnor, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* CLZ */
CONSTOPT(clz, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* CTZ */
CONSTOPT(ctz, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* CPOP */
CONSTOPT(cpop, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* MAX */
CONSTOPT(max, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* MAXU */
CONSTOPT(maxu, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* MIN */
CONSTOPT(min, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* MINU */
CONSTOPT(minu, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* SEXT.B */
CONSTOPT(sextb, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* SEXT.H */
CONSTOPT(sexth, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* ZEXT.H */
CONSTOPT(zexth, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* ROL */
CONSTOPT(rol, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* ROR */
CONSTOPT(ror, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* RORI */
CONSTOPT(rori, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* ORCB */
CONSTOPT(orcb, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* REV8 */
CONSTOPT(rev8, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})
#endif

#if RV32_HAS(Zbc)
/* CLMUL */
CONSTOPT(clmul, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* CLMULH */
CONSTOPT(clmulh, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* CLMULR */
CONSTOPT(clmulr, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})
#endif

#if RV32_HAS(Zbs)
/* BCLR */
CONSTOPT(bclr, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* BCLRI */
CONSTOPT(bclri, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* BEXT */
CONSTOPT(bext, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* BEXTI */
CONSTOPT(bexti, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* BINV */
CONSTOPT(binv, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* BINVI */
CONSTOPT(binvi, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* BSET */
CONSTOPT(bset, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})

/* BSETI */
CONSTOPT(bseti, {
    if (ir->rd)
        info->is_constant[ir->rd] = false;
})
#endif

/* Vector Extension */
#if RV32_HAS(EXT_V)
CONSTOPT(vsetvli, {})
CONSTOPT(vsetivli, {})
CONSTOPT(vsetvl, {})
CONSTOPT(vle8_v, {})
CONSTOPT(vle16_v, {})
CONSTOPT(vle32_v, {})
CONSTOPT(vle64_v, {})
CONSTOPT(vlseg2e8_v, {})
CONSTOPT(vlseg3e8_v, {})
CONSTOPT(vlseg4e8_v, {})
CONSTOPT(vlseg5e8_v, {})
CONSTOPT(vlseg6e8_v, {})
CONSTOPT(vlseg7e8_v, {})
CONSTOPT(vlseg8e8_v, {})
CONSTOPT(vlseg2e16_v, {})
CONSTOPT(vlseg3e16_v, {})
CONSTOPT(vlseg4e16_v, {})
CONSTOPT(vlseg5e16_v, {})
CONSTOPT(vlseg6e16_v, {})
CONSTOPT(vlseg7e16_v, {})
CONSTOPT(vlseg8e16_v, {})
CONSTOPT(vlseg2e32_v, {})
CONSTOPT(vlseg3e32_v, {})
CONSTOPT(vlseg4e32_v, {})
CONSTOPT(vlseg5e32_v, {})
CONSTOPT(vlseg6e32_v, {})
CONSTOPT(vlseg7e32_v, {})
CONSTOPT(vlseg8e32_v, {})
CONSTOPT(vlseg2e64_v, {})
CONSTOPT(vlseg3e64_v, {})
CONSTOPT(vlseg4e64_v, {})
CONSTOPT(vlseg5e64_v, {})
CONSTOPT(vlseg6e64_v, {})
CONSTOPT(vlseg7e64_v, {})
CONSTOPT(vlseg8e64_v, {})
CONSTOPT(vl1re8_v, {})
CONSTOPT(vl1re16_v, {})
CONSTOPT(vl1re32_v, {})
CONSTOPT(vl1re64_v, {})
CONSTOPT(vl2re8_v, {})
CONSTOPT(vl2re16_v, {})
CONSTOPT(vl2re32_v, {})
CONSTOPT(vl2re64_v, {})
CONSTOPT(vl4re8_v, {})
CONSTOPT(vl4re16_v, {})
CONSTOPT(vl4re32_v, {})
CONSTOPT(vl4re64_v, {})
CONSTOPT(vl8re8_v, {})
CONSTOPT(vl8re16_v, {})
CONSTOPT(vl8re32_v, {})
CONSTOPT(vl8re64_v, {})
CONSTOPT(vlm_v, {})
CONSTOPT(vle8ff_v, {})
CONSTOPT(vle16ff_v, {})
CONSTOPT(vle32ff_v, {})
CONSTOPT(vle64ff_v, {})
CONSTOPT(vlseg2e8ff_v, {})
CONSTOPT(vlseg3e8ff_v, {})
CONSTOPT(vlseg4e8ff_v, {})
CONSTOPT(vlseg5e8ff_v, {})
CONSTOPT(vlseg6e8ff_v, {})
CONSTOPT(vlseg7e8ff_v, {})
CONSTOPT(vlseg8e8ff_v, {})
CONSTOPT(vlseg2e16ff_v, {})
CONSTOPT(vlseg3e16ff_v, {})
CONSTOPT(vlseg4e16ff_v, {})
CONSTOPT(vlseg5e16ff_v, {})
CONSTOPT(vlseg6e16ff_v, {})
CONSTOPT(vlseg7e16ff_v, {})
CONSTOPT(vlseg8e16ff_v, {})
CONSTOPT(vlseg2e32ff_v, {})
CONSTOPT(vlseg3e32ff_v, {})
CONSTOPT(vlseg4e32ff_v, {})
CONSTOPT(vlseg5e32ff_v, {})
CONSTOPT(vlseg6e32ff_v, {})
CONSTOPT(vlseg7e32ff_v, {})
CONSTOPT(vlseg8e32ff_v, {})
CONSTOPT(vlseg2e64ff_v, {})
CONSTOPT(vlseg3e64ff_v, {})
CONSTOPT(vlseg4e64ff_v, {})
CONSTOPT(vlseg5e64ff_v, {})
CONSTOPT(vlseg6e64ff_v, {})
CONSTOPT(vlseg7e64ff_v, {})
CONSTOPT(vlseg8e64ff_v, {})
CONSTOPT(vluxei8_v, {})
CONSTOPT(vluxei16_v, {})
CONSTOPT(vluxei32_v, {})
CONSTOPT(vluxei64_v, {})
CONSTOPT(vluxseg2ei8_v, {})
CONSTOPT(vluxseg3ei8_v, {})
CONSTOPT(vluxseg4ei8_v, {})
CONSTOPT(vluxseg5ei8_v, {})
CONSTOPT(vluxseg6ei8_v, {})
CONSTOPT(vluxseg7ei8_v, {})
CONSTOPT(vluxseg8ei8_v, {})
CONSTOPT(vluxseg2ei16_v, {})
CONSTOPT(vluxseg3ei16_v, {})
CONSTOPT(vluxseg4ei16_v, {})
CONSTOPT(vluxseg5ei16_v, {})
CONSTOPT(vluxseg6ei16_v, {})
CONSTOPT(vluxseg7ei16_v, {})
CONSTOPT(vluxseg8ei16_v, {})
CONSTOPT(vluxseg2ei32_v, {})
CONSTOPT(vluxseg3ei32_v, {})
CONSTOPT(vluxseg4ei32_v, {})
CONSTOPT(vluxseg5ei32_v, {})
CONSTOPT(vluxseg6ei32_v, {})
CONSTOPT(vluxseg7ei32_v, {})
CONSTOPT(vluxseg8ei32_v, {})
CONSTOPT(vluxseg2ei64_v, {})
CONSTOPT(vluxseg3ei64_v, {})
CONSTOPT(vluxseg4ei64_v, {})
CONSTOPT(vluxseg5ei64_v, {})
CONSTOPT(vluxseg6ei64_v, {})
CONSTOPT(vluxseg7ei64_v, {})
CONSTOPT(vluxseg8ei64_v, {})
CONSTOPT(vlse8_v, {})
CONSTOPT(vlse16_v, {})
CONSTOPT(vlse32_v, {})
CONSTOPT(vlse64_v, {})
CONSTOPT(vlsseg2e8_v, {})
CONSTOPT(vlsseg3e8_v, {})
CONSTOPT(vlsseg4e8_v, {})
CONSTOPT(vlsseg5e8_v, {})
CONSTOPT(vlsseg6e8_v, {})
CONSTOPT(vlsseg7e8_v, {})
CONSTOPT(vlsseg8e8_v, {})
CONSTOPT(vlsseg2e16_v, {})
CONSTOPT(vlsseg3e16_v, {})
CONSTOPT(vlsseg4e16_v, {})
CONSTOPT(vlsseg5e16_v, {})
CONSTOPT(vlsseg6e16_v, {})
CONSTOPT(vlsseg7e16_v, {})
CONSTOPT(vlsseg8e16_v, {})
CONSTOPT(vlsseg2e32_v, {})
CONSTOPT(vlsseg3e32_v, {})
CONSTOPT(vlsseg4e32_v, {})
CONSTOPT(vlsseg5e32_v, {})
CONSTOPT(vlsseg6e32_v, {})
CONSTOPT(vlsseg7e32_v, {})
CONSTOPT(vlsseg8e32_v, {})
CONSTOPT(vlsseg2e64_v, {})
CONSTOPT(vlsseg3e64_v, {})
CONSTOPT(vlsseg4e64_v, {})
CONSTOPT(vlsseg5e64_v, {})
CONSTOPT(vlsseg6e64_v, {})
CONSTOPT(vlsseg7e64_v, {})
CONSTOPT(vlsseg8e64_v, {})
CONSTOPT(vloxei8_v, {})
CONSTOPT(vloxei16_v, {})
CONSTOPT(vloxei32_v, {})
CONSTOPT(vloxei64_v, {})
CONSTOPT(vloxseg2ei8_v, {})
CONSTOPT(vloxseg3ei8_v, {})
CONSTOPT(vloxseg4ei8_v, {})
CONSTOPT(vloxseg5ei8_v, {})
CONSTOPT(vloxseg6ei8_v, {})
CONSTOPT(vloxseg7ei8_v, {})
CONSTOPT(vloxseg8ei8_v, {})
CONSTOPT(vloxseg2ei16_v, {})
CONSTOPT(vloxseg3ei16_v, {})
CONSTOPT(vloxseg4ei16_v, {})
CONSTOPT(vloxseg5ei16_v, {})
CONSTOPT(vloxseg6ei16_v, {})
CONSTOPT(vloxseg7ei16_v, {})
CONSTOPT(vloxseg8ei16_v, {})
CONSTOPT(vloxseg2ei32_v, {})
CONSTOPT(vloxseg3ei32_v, {})
CONSTOPT(vloxseg4ei32_v, {})
CONSTOPT(vloxseg5ei32_v, {})
CONSTOPT(vloxseg6ei32_v, {})
CONSTOPT(vloxseg7ei32_v, {})
CONSTOPT(vloxseg8ei32_v, {})
CONSTOPT(vloxseg2ei64_v, {})
CONSTOPT(vloxseg3ei64_v, {})
CONSTOPT(vloxseg4ei64_v, {})
CONSTOPT(vloxseg5ei64_v, {})
CONSTOPT(vloxseg6ei64_v, {})
CONSTOPT(vloxseg7ei64_v, {})
CONSTOPT(vloxseg8ei64_v, {})
CONSTOPT(vse8_v, {})
CONSTOPT(vse16_v, {})
CONSTOPT(vse32_v, {})
CONSTOPT(vse64_v, {})
CONSTOPT(vsseg2e8_v, {})
CONSTOPT(vsseg3e8_v, {})
CONSTOPT(vsseg4e8_v, {})
CONSTOPT(vsseg5e8_v, {})
CONSTOPT(vsseg6e8_v, {})
CONSTOPT(vsseg7e8_v, {})
CONSTOPT(vsseg8e8_v, {})
CONSTOPT(vsseg2e16_v, {})
CONSTOPT(vsseg3e16_v, {})
CONSTOPT(vsseg4e16_v, {})
CONSTOPT(vsseg5e16_v, {})
CONSTOPT(vsseg6e16_v, {})
CONSTOPT(vsseg7e16_v, {})
CONSTOPT(vsseg8e16_v, {})
CONSTOPT(vsseg2e32_v, {})
CONSTOPT(vsseg3e32_v, {})
CONSTOPT(vsseg4e32_v, {})
CONSTOPT(vsseg5e32_v, {})
CONSTOPT(vsseg6e32_v, {})
CONSTOPT(vsseg7e32_v, {})
CONSTOPT(vsseg8e32_v, {})
CONSTOPT(vsseg2e64_v, {})
CONSTOPT(vsseg3e64_v, {})
CONSTOPT(vsseg4e64_v, {})
CONSTOPT(vsseg5e64_v, {})
CONSTOPT(vsseg6e64_v, {})
CONSTOPT(vsseg7e64_v, {})
CONSTOPT(vsseg8e64_v, {})
CONSTOPT(vs1r_v, {})
CONSTOPT(vs2r_v, {})
CONSTOPT(vs4r_v, {})
CONSTOPT(vs8r_v, {})
CONSTOPT(vsm_v, {})
CONSTOPT(vsuxei8_v, {})
CONSTOPT(vsuxei16_v, {})
CONSTOPT(vsuxei32_v, {})
CONSTOPT(vsuxei64_v, {})
CONSTOPT(vsuxseg2ei8_v, {})
CONSTOPT(vsuxseg3ei8_v, {})
CONSTOPT(vsuxseg4ei8_v, {})
CONSTOPT(vsuxseg5ei8_v, {})
CONSTOPT(vsuxseg6ei8_v, {})
CONSTOPT(vsuxseg7ei8_v, {})
CONSTOPT(vsuxseg8ei8_v, {})
CONSTOPT(vsuxseg2ei16_v, {})
CONSTOPT(vsuxseg3ei16_v, {})
CONSTOPT(vsuxseg4ei16_v, {})
CONSTOPT(vsuxseg5ei16_v, {})
CONSTOPT(vsuxseg6ei16_v, {})
CONSTOPT(vsuxseg7ei16_v, {})
CONSTOPT(vsuxseg8ei16_v, {})
CONSTOPT(vsuxseg2ei32_v, {})
CONSTOPT(vsuxseg3ei32_v, {})
CONSTOPT(vsuxseg4ei32_v, {})
CONSTOPT(vsuxseg5ei32_v, {})
CONSTOPT(vsuxseg6ei32_v, {})
CONSTOPT(vsuxseg7ei32_v, {})
CONSTOPT(vsuxseg8ei32_v, {})
CONSTOPT(vsuxseg2ei64_v, {})
CONSTOPT(vsuxseg3ei64_v, {})
CONSTOPT(vsuxseg4ei64_v, {})
CONSTOPT(vsuxseg5ei64_v, {})
CONSTOPT(vsuxseg6ei64_v, {})
CONSTOPT(vsuxseg7ei64_v, {})
CONSTOPT(vsuxseg8ei64_v, {})
CONSTOPT(vsse8_v, {})
CONSTOPT(vsse16_v, {})
CONSTOPT(vsse32_v, {})
CONSTOPT(vsse64_v, {})
CONSTOPT(vssseg2e8_v, {})
CONSTOPT(vssseg3e8_v, {})
CONSTOPT(vssseg4e8_v, {})
CONSTOPT(vssseg5e8_v, {})
CONSTOPT(vssseg6e8_v, {})
CONSTOPT(vssseg7e8_v, {})
CONSTOPT(vssseg8e8_v, {})
CONSTOPT(vssseg2e16_v, {})
CONSTOPT(vssseg3e16_v, {})
CONSTOPT(vssseg4e16_v, {})
CONSTOPT(vssseg5e16_v, {})
CONSTOPT(vssseg6e16_v, {})
CONSTOPT(vssseg7e16_v, {})
CONSTOPT(vssseg8e16_v, {})
CONSTOPT(vssseg2e32_v, {})
CONSTOPT(vssseg3e32_v, {})
CONSTOPT(vssseg4e32_v, {})
CONSTOPT(vssseg5e32_v, {})
CONSTOPT(vssseg6e32_v, {})
CONSTOPT(vssseg7e32_v, {})
CONSTOPT(vssseg8e32_v, {})
CONSTOPT(vssseg2e64_v, {})
CONSTOPT(vssseg3e64_v, {})
CONSTOPT(vssseg4e64_v, {})
CONSTOPT(vssseg5e64_v, {})
CONSTOPT(vssseg6e64_v, {})
CONSTOPT(vssseg7e64_v, {})
CONSTOPT(vssseg8e64_v, {})
CONSTOPT(vsoxei8_v, {})
CONSTOPT(vsoxei16_v, {})
CONSTOPT(vsoxei32_v, {})
CONSTOPT(vsoxei64_v, {})
CONSTOPT(vsoxseg2ei8_v, {})
CONSTOPT(vsoxseg3ei8_v, {})
CONSTOPT(vsoxseg4ei8_v, {})
CONSTOPT(vsoxseg5ei8_v, {})
CONSTOPT(vsoxseg6ei8_v, {})
CONSTOPT(vsoxseg7ei8_v, {})
CONSTOPT(vsoxseg8ei8_v, {})
CONSTOPT(vsoxseg2ei16_v, {})
CONSTOPT(vsoxseg3ei16_v, {})
CONSTOPT(vsoxseg4ei16_v, {})
CONSTOPT(vsoxseg5ei16_v, {})
CONSTOPT(vsoxseg6ei16_v, {})
CONSTOPT(vsoxseg7ei16_v, {})
CONSTOPT(vsoxseg8ei16_v, {})
CONSTOPT(vsoxseg2ei32_v, {})
CONSTOPT(vsoxseg3ei32_v, {})
CONSTOPT(vsoxseg4ei32_v, {})
CONSTOPT(vsoxseg5ei32_v, {})
CONSTOPT(vsoxseg6ei32_v, {})
CONSTOPT(vsoxseg7ei32_v, {})
CONSTOPT(vsoxseg8ei32_v, {})
CONSTOPT(vsoxseg2ei64_v, {})
CONSTOPT(vsoxseg3ei64_v, {})
CONSTOPT(vsoxseg4ei64_v, {})
CONSTOPT(vsoxseg5ei64_v, {})
CONSTOPT(vsoxseg6ei64_v, {})
CONSTOPT(vsoxseg7ei64_v, {})
CONSTOPT(vsoxseg8ei64_v, {})
CONSTOPT(vadd_vv, {})
CONSTOPT(vadd_vx, {})
CONSTOPT(vadd_vi, {})
CONSTOPT(vsub_vv, {})
CONSTOPT(vsub_vx, {})
CONSTOPT(vrsub_vx, {})
CONSTOPT(vrsub_vi, {})
CONSTOPT(vminu_vv, {})
CONSTOPT(vminu_vx, {})
CONSTOPT(vmin_vv, {})
CONSTOPT(vmin_vx, {})
CONSTOPT(vmaxu_vv, {})
CONSTOPT(vmaxu_vx, {})
CONSTOPT(vmax_vv, {})
CONSTOPT(vmax_vx, {})
CONSTOPT(vand_vv, {})
CONSTOPT(vand_vx, {})
CONSTOPT(vand_vi, {})
CONSTOPT(vor_vv, {})
CONSTOPT(vor_vx, {})
CONSTOPT(vor_vi, {})
CONSTOPT(vxor_vv, {})
CONSTOPT(vxor_vx, {})
CONSTOPT(vxor_vi, {})
CONSTOPT(vrgather_vv, {})
CONSTOPT(vrgather_vx, {})
CONSTOPT(vrgather_vi, {})
CONSTOPT(vslideup_vx, {})
CONSTOPT(vslideup_vi, {})
CONSTOPT(vrgatherei16_vv, {})
CONSTOPT(vslidedown_vx, {})
CONSTOPT(vslidedown_vi, {})
CONSTOPT(vadc_vvm, {})
CONSTOPT(vadc_vxm, {})
CONSTOPT(vadc_vim, {})
CONSTOPT(vmadc_vv, {})
CONSTOPT(vmadc_vx, {})
CONSTOPT(vmadc_vi, {})
CONSTOPT(vsbc_vvm, {})
CONSTOPT(vsbc_vxm, {})
CONSTOPT(vmsbc_vv, {})
CONSTOPT(vmsbc_vx, {})
CONSTOPT(vmerge_vvm, {})
CONSTOPT(vmerge_vxm, {})
CONSTOPT(vmerge_vim, {})
CONSTOPT(vmv_v_v, {})
CONSTOPT(vmv_v_x, {})
CONSTOPT(vmv_v_i, {})
CONSTOPT(vmseq_vv, {})
CONSTOPT(vmseq_vx, {})
CONSTOPT(vmseq_vi, {})
CONSTOPT(vmsne_vv, {})
CONSTOPT(vmsne_vx, {})
CONSTOPT(vmsne_vi, {})
CONSTOPT(vmsltu_vv, {})
CONSTOPT(vmsltu_vx, {})
CONSTOPT(vmslt_vv, {})
CONSTOPT(vmslt_vx, {})
CONSTOPT(vmsleu_vv, {})
CONSTOPT(vmsleu_vx, {})
CONSTOPT(vmsleu_vi, {})
CONSTOPT(vmsle_vv, {})
CONSTOPT(vmsle_vx, {})
CONSTOPT(vmsle_vi, {})
CONSTOPT(vmsgtu_vx, {})
CONSTOPT(vmsgtu_vi, {})
CONSTOPT(vmsgt_vx, {})
CONSTOPT(vmsgt_vi, {})
CONSTOPT(vsaddu_vv, {})
CONSTOPT(vsaddu_vx, {})
CONSTOPT(vsaddu_vi, {})
CONSTOPT(vsadd_vv, {})
CONSTOPT(vsadd_vx, {})
CONSTOPT(vsadd_vi, {})
CONSTOPT(vssubu_vv, {})
CONSTOPT(vssubu_vx, {})
CONSTOPT(vssub_vv, {})
CONSTOPT(vssub_vx, {})
CONSTOPT(vsll_vv, {})
CONSTOPT(vsll_vx, {})
CONSTOPT(vsll_vi, {})
CONSTOPT(vsmul_vv, {})
CONSTOPT(vsmul_vx, {})
CONSTOPT(vsrl_vv, {})
CONSTOPT(vsrl_vx, {})
CONSTOPT(vsrl_vi, {})
CONSTOPT(vsra_vv, {})
CONSTOPT(vsra_vx, {})
CONSTOPT(vsra_vi, {})
CONSTOPT(vssrl_vv, {})
CONSTOPT(vssrl_vx, {})
CONSTOPT(vssrl_vi, {})
CONSTOPT(vssra_vv, {})
CONSTOPT(vssra_vx, {})
CONSTOPT(vssra_vi, {})
CONSTOPT(vnsrl_wv, {})
CONSTOPT(vnsrl_wx, {})
CONSTOPT(vnsrl_wi, {})
CONSTOPT(vnsra_wv, {})
CONSTOPT(vnsra_wx, {})
CONSTOPT(vnsra_wi, {})
CONSTOPT(vnclipu_wv, {})
CONSTOPT(vnclipu_wx, {})
CONSTOPT(vnclipu_wi, {})
CONSTOPT(vnclip_wv, {})
CONSTOPT(vnclip_wx, {})
CONSTOPT(vnclip_wi, {})
CONSTOPT(vwredsumu_vs, {})
CONSTOPT(vwredsum_vs, {})
CONSTOPT(vredsum_vs, {})
CONSTOPT(vredand_vs, {})
CONSTOPT(vredor_vs, {})
CONSTOPT(vredxor_vs, {})
CONSTOPT(vredminu_vs, {})
CONSTOPT(vredmin_vs, {})
CONSTOPT(vredmaxu_vs, {})
CONSTOPT(vredmax_vs, {})
CONSTOPT(vaaddu_vv, {})
CONSTOPT(vaaddu_vx, {})
CONSTOPT(vaadd_vv, {})
CONSTOPT(vaadd_vx, {})
CONSTOPT(vasubu_vv, {})
CONSTOPT(vasubu_vx, {})
CONSTOPT(vasub_vv, {})
CONSTOPT(vasub_vx, {})
CONSTOPT(vslide1up_vx, {})
CONSTOPT(vslide1down_vx, {})
CONSTOPT(vcompress_vm, {})
CONSTOPT(vmandn_mm, {})
CONSTOPT(vmand_mm, {})
CONSTOPT(vmor_mm, {})
CONSTOPT(vmxor_mm, {})
CONSTOPT(vmorn_mm, {})
CONSTOPT(vmnand_mm, {})
CONSTOPT(vmnor_mm, {})
CONSTOPT(vmxnor_mm, {})
CONSTOPT(vdivu_vv, {})
CONSTOPT(vdivu_vx, {})
CONSTOPT(vdiv_vv, {})
CONSTOPT(vdiv_vx, {})
CONSTOPT(vremu_vv, {})
CONSTOPT(vremu_vx, {})
CONSTOPT(vrem_vv, {})
CONSTOPT(vrem_vx, {})
CONSTOPT(vmulhu_vv, {})
CONSTOPT(vmulhu_vx, {})
CONSTOPT(vmul_vv, {})
CONSTOPT(vmul_vx, {})
CONSTOPT(vmulhsu_vv, {})
CONSTOPT(vmulhsu_vx, {})
CONSTOPT(vmulh_vv, {})
CONSTOPT(vmulh_vx, {})
CONSTOPT(vmadd_vv, {})
CONSTOPT(vmadd_vx, {})
CONSTOPT(vnmsub_vv, {})
CONSTOPT(vnmsub_vx, {})
CONSTOPT(vmacc_vv, {})
CONSTOPT(vmacc_vx, {})
CONSTOPT(vnmsac_vv, {})
CONSTOPT(vnmsac_vx, {})
CONSTOPT(vwaddu_vv, {})
CONSTOPT(vwaddu_vx, {})
CONSTOPT(vwadd_vv, {})
CONSTOPT(vwadd_vx, {})
CONSTOPT(vwsubu_vv, {})
CONSTOPT(vwsubu_vx, {})
CONSTOPT(vwsub_vv, {})
CONSTOPT(vwsub_vx, {})
CONSTOPT(vwaddu_wv, {})
CONSTOPT(vwaddu_wx, {})
CONSTOPT(vwadd_wv, {})
CONSTOPT(vwadd_wx, {})
CONSTOPT(vwsubu_wv, {})
CONSTOPT(vwsubu_wx, {})
CONSTOPT(vwsub_wv, {})
CONSTOPT(vwsub_wx, {})
CONSTOPT(vwmulu_vv, {})
CONSTOPT(vwmulu_vx, {})
CONSTOPT(vwmulsu_vv, {})
CONSTOPT(vwmulsu_vx, {})
CONSTOPT(vwmul_vv, {})
CONSTOPT(vwmul_vx, {})
CONSTOPT(vwmaccu_vv, {})
CONSTOPT(vwmaccu_vx, {})
CONSTOPT(vwmacc_vv, {})
CONSTOPT(vwmacc_vx, {})
CONSTOPT(vwmaccus_vx, {})
CONSTOPT(vwmaccsu_vv, {})
CONSTOPT(vwmaccsu_vx, {})
CONSTOPT(vmv_s_x, {})
CONSTOPT(vmv_x_s, {})
CONSTOPT(vcpop_m, {})
CONSTOPT(vfirst_m, {})
CONSTOPT(vmsbf_m, {})
CONSTOPT(vmsof_m, {})
CONSTOPT(vmsif_m, {})
CONSTOPT(viota_m, {})
CONSTOPT(vid_v, {})
CONSTOPT(vfadd_vv, {})
CONSTOPT(vfadd_vf, {})
CONSTOPT(vfredusum_vs, {})
CONSTOPT(vfsub_vv, {})
CONSTOPT(vfsub_vf, {})
CONSTOPT(vfredosum_vs, {})
CONSTOPT(vfmin_vv, {})
CONSTOPT(vfmin_vf, {})
CONSTOPT(vfredmin_vs, {})
CONSTOPT(vfmax_vv, {})
CONSTOPT(vfmax_vf, {})
CONSTOPT(vfredmax_vs, {})
CONSTOPT(vfsgnj_vv, {})
CONSTOPT(vfsgnj_vf, {})
CONSTOPT(vfsgnjn_vv, {})
CONSTOPT(vfsgnjn_vf, {})
CONSTOPT(vfsgnjx_vv, {})
CONSTOPT(vfsgnjx_vf, {})
CONSTOPT(vfslide1up_vf, {})
CONSTOPT(vfslide1down_vf, {})
CONSTOPT(vfmerge_vfm, {})
CONSTOPT(vfmv_v_f, {})
CONSTOPT(vmfeq_vv, {})
CONSTOPT(vmfeq_vf, {})
CONSTOPT(vmfle_vv, {})
CONSTOPT(vmfle_vf, {})
CONSTOPT(vmflt_vv, {})
CONSTOPT(vmflt_vf, {})
CONSTOPT(vmfne_vv, {})
CONSTOPT(vmfne_vf, {})
CONSTOPT(vmfgt_vf, {})
CONSTOPT(vmfge_vf, {})
CONSTOPT(vfdiv_vv, {})
CONSTOPT(vfdiv_vf, {})
CONSTOPT(vfrdiv_vf, {})
CONSTOPT(vfmul_vv, {})
CONSTOPT(vfmul_vf, {})
CONSTOPT(vfrsub_vf, {})
CONSTOPT(vfmadd_vv, {})
CONSTOPT(vfmadd_vf, {})
CONSTOPT(vfnmadd_vv, {})
CONSTOPT(vfnmadd_vf, {})
CONSTOPT(vfmsub_vv, {})
CONSTOPT(vfmsub_vf, {})
CONSTOPT(vfnmsub_vv, {})
CONSTOPT(vfnmsub_vf, {})
CONSTOPT(vfmacc_vv, {})
CONSTOPT(vfmacc_vf, {})
CONSTOPT(vfnmacc_vv, {})
CONSTOPT(vfnmacc_vf, {})
CONSTOPT(vfmsac_vv, {})
CONSTOPT(vfmsac_vf, {})
CONSTOPT(vfnmsac_vv, {})
CONSTOPT(vfnmsac_vf, {})
CONSTOPT(vfwadd_vv, {})
CONSTOPT(vfwadd_vf, {})
CONSTOPT(vfwredusum_vs, {})
CONSTOPT(vfwsub_vv, {})
CONSTOPT(vfwsub_vf, {})
CONSTOPT(vfwredosum_vs, {})
CONSTOPT(vfwadd_wv, {})
CONSTOPT(vfwadd_wf, {})
CONSTOPT(vfwsub_wv, {})
CONSTOPT(vfwsub_wf, {})
CONSTOPT(vfwmul_vv, {})
CONSTOPT(vfwmul_vf, {})
CONSTOPT(vfwmacc_vv, {})
CONSTOPT(vfwmacc_vf, {})
CONSTOPT(vfwnmacc_vv, {})
CONSTOPT(vfwnmacc_vf, {})
CONSTOPT(vfwmsac_vv, {})
CONSTOPT(vfwmsac_vf, {})
CONSTOPT(vfwnmsac_vv, {})
CONSTOPT(vfwnmsac_vf, {})

#endif
