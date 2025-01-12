/* RV32I Base Instruction Set
 *
 * Conforming to the instructions specified in chapter 2 of the RISC-V
 * unprivileged specification version 20191213.
 */

/* Interpreter instruction implementations
 *
 * This file contains the purely semantic implementations of RISC-V instructions
 * for the interpreter. It uses the RVOP macro to define the behavior of each
 * instruction by directly manipulating the emulator state.
 *
 * Architecture:
 * - RVOP(name, { body }): Defines an interpreter handler function.
 * - Parameters: rv (emulator state), ir (decoded instruction),
 *   cycle (cycle counter), PC (program counter).
 * - Return: 'bool' indicating whether to continue execution.
 *
 * Example:
 *   RVOP(addi, { rv->X[ir->rd] = rv->X[ir->rs1] + ir->imm; })
 *
 * Implementation notes:
 * - Changes to instruction semantics should be applied here, while JIT-specific
 *   optimizations should be applied in src/rv32_jit.c.
 */

/* Internal */
RVOP(nop, { rv->X[rv_reg_zero] = 0; })

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
RVOP(auipc, { rv->X[ir->rd] = ir->imm + PC; })

/* JAL: Jump and Link
 * store successor instruction address into rd.
 * add next J imm (offset) to pc.
 */
RVOP(jal, {
    const uint32_t pc = PC;
    /* Jump */
    PC += ir->imm;
    /* link with return address */
    if (ir->rd)
        rv->X[ir->rd] = pc + 4;
    /* check instruction misaligned */
#if !RV32_HAS(EXT_C)
    RV_EXC_MISALIGN_HANDLER(pc, INSN, false, 0);
#endif
    struct rv_insn *taken = ir->branch_taken;
    if (taken) {
#if RV32_HAS(JIT)
        IIF(RV32_HAS(SYSTEM)(if (!rv->is_trapped && !reloc_enable_mmu), ))
        {
            IIF(RV32_HAS(SYSTEM))(block_t *next =, )
                cache_get(rv->block_cache, PC, true);
            IIF(RV32_HAS(SYSTEM))(
                if (next->satp == rv->csr_satp && !next->invalidated), )
            {
                if (!set_add(&pc_set, PC))
                    has_loops = true;
                if (cache_hot(rv->block_cache, PC))
                    goto end_op;
            }
        }
#endif
#if RV32_HAS(SYSTEM)
        if (!rv->is_trapped)
#endif
        {
            /* The last_pc should only be updated when not in the trap path.
             * Updating it during the trap path could lead to incorrect
             * block chaining in rv_step(). Specifically, an interrupt might
             * occur before locating the previous block with last_pc, and
             * since __trap_handler() uses the same RVOP, the last_pc could
             * be updated incorrectly during the trap path.
             *
             * This rule also applies to same statements elsewhere in this
             * file.
             */
            last_pc = PC;

            MUST_TAIL return taken->impl(rv, taken, cycle, PC);
        }
    }
    goto end_op;
})

/* The branch history table records historical data pertaining to indirect jump
 * targets. This functionality alleviates the need to invoke block_find() and
 * incurs overhead only when the indirect jump targets are not previously
 * recorded. Additionally, this table lets the interpreter fast-path indirect
 * jumps without repeatedly calling block_find().
 */
#if !RV32_HAS(JIT)
#define LOOKUP_OR_UPDATE_BRANCH_HISTORY_TABLE()                                \
    /*                                                                         \
     * Direct-mapped branch history table lookup.                              \
     *                                                                         \
     * When handling trap, the branch history table should not be lookup since \
     * it causes return from the trap_handler.                                 \
     *                                                                         \
     * In addition, before relocate_enable_mmu, the block maybe retranslated,  \
     * thus the branch history lookup table should not be updated too.         \
     */                                                                        \
    IIF(RV32_HAS(GDBSTUB)(if (!rv->debug_mode), ))                             \
    {                                                                          \
        IIF(RV32_HAS(SYSTEM)(if (!rv->is_trapped && !reloc_enable_mmu), ))     \
        {                                                                      \
            /* Direct-mapped lookup: O(1) instead of O(n) linear search */     \
            const uint32_t bht_idx = (PC >> 2) & (HISTORY_SIZE - 1);           \
            if (ir->branch_table->PC[bht_idx] == PC &&                         \
                ir->branch_table->target[bht_idx]) {                           \
                MUST_TAIL return ir->branch_table->target[bht_idx]->impl(      \
                    rv, ir->branch_table->target[bht_idx], cycle, PC);         \
            }                                                                  \
            block_t *block = block_find(&rv->block_map, PC);                   \
            if (block) {                                                       \
                /* Direct replacement at computed index */                     \
                ir->branch_table->PC[bht_idx] = PC;                            \
                ir->branch_table->target[bht_idx] = block->ir_head;            \
                MUST_TAIL return block->ir_head->impl(rv, block->ir_head,      \
                                                      cycle, PC);              \
            }                                                                  \
        }                                                                      \
    }
#else
#define LOOKUP_OR_UPDATE_BRANCH_HISTORY_TABLE()                              \
    IIF(RV32_HAS(SYSTEM))(if (!rv->is_trapped && !reloc_enable_mmu), )       \
    {                                                                        \
        block_t *block = cache_get(rv->block_cache, PC, true);               \
        if (block) {                                                         \
            /* Direct-mapped lookup: O(1) instead of O(n) linear search */   \
            const uint32_t bht_idx = (PC >> 2) & (HISTORY_SIZE - 1);         \
            if (ir->branch_table->PC[bht_idx] == PC) {                       \
                IIF(RV32_HAS(SYSTEM))(                                       \
                    if (ir->branch_table->satp[bht_idx] == rv->csr_satp), )  \
                {                                                            \
                    ir->branch_table->times[bht_idx]++;                      \
                    if (cache_hot(rv->block_cache, PC))                      \
                        goto end_op;                                         \
                }                                                            \
            }                                                                \
            /* Direct replacement at computed index */                       \
            ir->branch_table->times[bht_idx] = 1;                            \
            ir->branch_table->PC[bht_idx] = PC;                              \
            IIF(RV32_HAS(SYSTEM))(                                           \
                ir->branch_table->satp[bht_idx] = rv->csr_satp, );           \
            if (cache_hot(rv->block_cache, PC))                              \
                goto end_op;                                                 \
            MUST_TAIL return block->ir_head->impl(rv, block->ir_head, cycle, \
                                                  PC);                       \
        }                                                                    \
    }
#endif

/* The indirect jump instruction JALR uses the I-type encoding. The target
 * address is obtained by adding the sign-extended 12-bit I-immediate to the
 * register rs1, then setting the least-significant bit of the result to zero.
 * The address of the instruction following the jump (pc+4) is written to
 * register rd. Register x0 can be used as the destination if the result is
 * not required.
 */
RVOP(jalr, {
    const uint32_t pc = PC;
    /* jump */
    PC = (rv->X[ir->rs1] + ir->imm) & ~1U;
    /* link */
    if (ir->rd)
        rv->X[ir->rd] = pc + 4;
    /* check instruction misaligned */
#if !RV32_HAS(EXT_C)
    RV_EXC_MISALIGN_HANDLER(pc, INSN, false, 0);
#endif
    LOOKUP_OR_UPDATE_BRANCH_HISTORY_TABLE();

#if RV32_HAS(SYSTEM)
    /*
     * relocate_enable_mmu is the first function called to set up the MMU.
     * Inside the function, at address 0x98, an invalid PTE is accessed,
     * causing a fetch page fault and trapping into the trap_handler, and
     * it will not return via sret.
     *
     * After the jalr instruction at physical address 0xc00000b4
     * (the final instruction of relocate_enable_mmu), the MMU becomes
     * available.
     *
     * Based on this, we need to manually escape from the trap_handler after
     * the jalr instruction is executed.
     */
    if (!reloc_enable_mmu && reloc_enable_mmu_jalr_addr == 0xc00000b4) {
        reloc_enable_mmu = true;
        need_retranslate = true;
        rv->is_trapped = false;
    }

#endif /* RV32_HAS(SYSTEM) */

    goto end_op;
})

/* clang-format off */
#define BRANCH_COND(type, x, y, cond) \
    (type) x cond (type) y
/* clang-format on */

#define BRANCH_FUNC(type, cond)                                                \
    IIF(RV32_HAS(EXT_C))(, const uint32_t pc = PC;);                           \
    if (BRANCH_COND(type, rv->X[ir->rs1], rv->X[ir->rs2], cond)) {             \
        IIF(RV32_HAS(SYSTEM))(                                                 \
            {                                                                  \
                if (!rv->is_trapped) {                                         \
                    is_branch_taken = false;                                   \
                }                                                              \
            },                                                                 \
            is_branch_taken = false;);                                         \
        struct rv_insn *untaken = ir->branch_untaken;                          \
        if (!untaken)                                                          \
            goto nextop;                                                       \
        IIF(RV32_HAS(JIT))(                                                    \
            {                                                                  \
                block_t *next = cache_get(rv->block_cache, PC + 4, true);      \
                if (next IIF(RV32_HAS(SYSTEM))(&&next->satp == rv->csr_satp && \
                                                   !next->invalidated, )) {    \
                    if (!set_add(&pc_set, PC + 4))                             \
                        has_loops = true;                                      \
                    if (cache_hot(rv->block_cache, PC + 4))                    \
                        goto nextop;                                           \
                }                                                              \
            }, );                                                              \
        PC += 4;                                                               \
        IIF(RV32_HAS(SYSTEM))(                                                 \
            {                                                                  \
                if (!rv->is_trapped) {                                         \
                    last_pc = PC;                                              \
                    MUST_TAIL return untaken->impl(rv, untaken, cycle, PC);    \
                }                                                              \
            }, );                                                              \
        goto end_op;                                                           \
    }                                                                          \
    IIF(RV32_HAS(SYSTEM))(                                                     \
        {                                                                      \
            if (!rv->is_trapped) {                                             \
                is_branch_taken = true;                                        \
            }                                                                  \
        },                                                                     \
        is_branch_taken = true;);                                              \
    PC += ir->imm;                                                             \
    /* check instruction misaligned */                                         \
    IIF(RV32_HAS(EXT_C))(, RV_EXC_MISALIGN_HANDLER(pc, INSN, false, 0););      \
    struct rv_insn *taken = ir->branch_taken;                                  \
    if (taken) {                                                               \
        IIF(RV32_HAS(JIT))(                                                    \
            {                                                                  \
                block_t *next = cache_get(rv->block_cache, PC, true);          \
                if (next IIF(RV32_HAS(SYSTEM))(&&next->satp == rv->csr_satp && \
                                                   !next->invalidated, )) {    \
                    if (!set_add(&pc_set, PC))                                 \
                        has_loops = true;                                      \
                    if (cache_hot(rv->block_cache, PC))                        \
                        goto end_op;                                           \
                }                                                              \
            }, );                                                              \
        IIF(RV32_HAS(SYSTEM))(                                                 \
            {                                                                  \
                if (!rv->is_trapped) {                                         \
                    last_pc = PC;                                              \
                    MUST_TAIL return taken->impl(rv, taken, cycle, PC);        \
                }                                                              \
            }, );                                                              \
    }                                                                          \
    goto end_op;

/* In RV32I and RV64I, if the branch is taken, set pc = pc + offset, where
 * offset is a multiple of two; else do nothing. The offset is 13 bits long.
 *
 * The condition for branch taken depends on the value in mnemonic, which is
 * one of:
 * - "beq": src1 == src2
 * - "bne": src1 != src2
 * - "blt": src1 < src2 as signed integers
 * - "bge": src1 >= src2 as signed integers
 * - "bltu": src1 < src2 as unsigned integers
 * - "bgeu": src1 >= src2 as unsigned integers
 *
 * On branch taken, an instruction-address-misaligned exception is generated
 * if the target pc is not 4-byte aligned.
 */

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

/* There are 5 types of loads: two for byte and halfword sizes, and one for word
 * size. Two instructions are required for byte and halfword loads because they
 * can be either zero-extended or sign-extended to fill the register. However,
 * for word-sized loads, an entire register's worth of data is read from memory,
 * and no extension is needed.
 */

/* RAM fast-path memory access macros
 *
 * In non-SYSTEM mode, bypass io callback indirection for direct RAM access.
 * This eliminates function pointer dispatch overhead per memory operation.
 * In SYSTEM mode, use io callbacks for MMU/TLB handling.
 */
#if !RV32_HAS(SYSTEM)
#define MEM_READ_W(rv, addr) ram_read_w(rv, addr)
#define MEM_READ_S(rv, addr) ram_read_s(rv, addr)
#define MEM_READ_B(rv, addr) ram_read_b(rv, addr)
#define MEM_WRITE_W(rv, addr, val) ram_write_w(rv, addr, val)
#define MEM_WRITE_S(rv, addr, val) ram_write_s(rv, addr, val)
#define MEM_WRITE_B(rv, addr, val) ram_write_b(rv, addr, val)
#else
#define MEM_READ_W(rv, addr) (rv)->io.mem_read_w(rv, addr)
#define MEM_READ_S(rv, addr) (rv)->io.mem_read_s(rv, addr)
#define MEM_READ_B(rv, addr) (rv)->io.mem_read_b(rv, addr)
#define MEM_WRITE_W(rv, addr, val) (rv)->io.mem_write_w(rv, addr, val)
#define MEM_WRITE_S(rv, addr, val) (rv)->io.mem_write_s(rv, addr, val)
#define MEM_WRITE_B(rv, addr, val) (rv)->io.mem_write_b(rv, addr, val)
#endif

/* LB: Load Byte */
RVOP(lb, {
    uint32_t addr = rv->X[ir->rs1] + ir->imm;
    rv->X[ir->rd] = sign_extend_b(MEM_READ_B(rv, addr));
})

/* LH: Load Halfword */
RVOP(lh, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
    rv->X[ir->rd] = sign_extend_h(MEM_READ_S(rv, addr));
})

/* LW: Load Word */
RVOP(lw, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->X[ir->rd] = MEM_READ_W(rv, addr);
})

/* LBU: Load Byte Unsigned */
RVOP(lbu, {
    uint32_t addr = rv->X[ir->rs1] + ir->imm;
    rv->X[ir->rd] = MEM_READ_B(rv, addr);
})

/* LHU: Load Halfword Unsigned */
RVOP(lhu, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
    rv->X[ir->rd] = MEM_READ_S(rv, addr);
})

/* There are 3 types of stores: byte, halfword, and word-sized. Unlike loads,
 * there are no signed or unsigned variants, as stores to memory write exactly
 * the number of bytes specified, and there is no sign or zero extension
 * involved.
 */

/* SB: Store Byte */
RVOP(sb, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    const uint32_t value = rv->X[ir->rs2];
    MEM_WRITE_B(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})

/* SH: Store Halfword */
RVOP(sh, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(1, STORE, false, 1);
    const uint32_t value = rv->X[ir->rs2];
    MEM_WRITE_S(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})

/* SW: Store Word */
RVOP(sw, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    const uint32_t value = rv->X[ir->rs2];
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})

/* ADDI adds the sign-extended 12-bit immediate to register rs1. Arithmetic
 * overflow is ignored and the result is simply the low XLEN bits of the
 * result. ADDI rd, rs1, 0 is used to implement the MV rd, rs1 assembler
 * pseudo-instruction.
 */
RVOP(addi, { rv->X[ir->rd] = rv->X[ir->rs1] + ir->imm; })

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

FORCE_INLINE void shift_func(riscv_t *rv, const rv_insn_t *ir)
{
    switch (ir->opcode) {
    case rv_insn_slli:
        rv->X[ir->rd] = rv->X[ir->rs1] << (ir->imm & 0x1f);
        break;
    case rv_insn_srli:
        rv->X[ir->rd] = rv->X[ir->rs1] >> (ir->imm & 0x1f);
        break;
    case rv_insn_srai:
        rv->X[ir->rd] = ((int32_t) rv->X[ir->rs1]) >> (ir->imm & 0x1f);
        break;
    default:
        __UNREACHABLE;
        break;
    }
};

/* SLLI performs logical left shift on the value in register rs1 by the shift
 * amount held in the lower 5 bits of the immediate.
 */
RVOP(slli, { shift_func(rv, ir); })

/* SRLI performs logical right shift on the value in register rs1 by the shift
 * amount held in the lower 5 bits of the immediate.
 */
RVOP(srli, { shift_func(rv, ir); })

/* SRAI performs arithmetic right shift on the value in register rs1 by the
 * shift amount held in the lower 5 bits of the immediate.
 */
RVOP(srai, { shift_func(rv, ir); })

/* ADD */
RVOP(add, { rv->X[ir->rd] = rv->X[ir->rs1] + rv->X[ir->rs2]; })

/* SUB: Subtract */
RVOP(sub, { rv->X[ir->rd] = rv->X[ir->rs1] - rv->X[ir->rs2]; })

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
/* clang-format off */
RVOP(
     and,
     { rv->X[ir->rd] = rv->X[ir->rs1] & rv->X[ir->rs2]; })
/* clang-format on */

/*
 * FENCE: order device I/O and memory accesses as viewed by other
 * RISC-V harts and external devices or coprocessors
 */
RVOP(fence, {
    PC += 4;
    /* FIXME: fill real implementations */
    goto end_op;
})

/* ECALL: Environment Call */
RVOP(ecall, {
    rv->compressed = false;
    rv->csr_cycle = cycle;
    rv->PC = PC;
    rv->io.on_ecall(rv);
    return true;
})

/* EBREAK: Environment Break */
RVOP(ebreak, {
    rv->compressed = false;
    rv->csr_cycle = cycle;
    rv->PC = PC;
    rv->io.on_ebreak(rv);
    return true;
})

/* WFI: Wait for Interrupt */
RVOP(wfi, {
    PC += 4;
    /* FIXME: Implement */
    goto end_op;
})

/* URET: return from traps in U-mode */
RVOP(uret, {
    /* FIXME: Implement */
    return false;
})

/* SRET: return from traps in S-mode */
#if RV32_HAS(SYSTEM)
RVOP(sret, {
    rv->is_trapped = false;
    rv->priv_mode = (rv->csr_sstatus & SSTATUS_SPP) >> SSTATUS_SPP_SHIFT;
    rv->csr_sstatus &= ~(SSTATUS_SPP);

    const uint32_t sstatus_spie =
        (rv->csr_sstatus & SSTATUS_SPIE) >> SSTATUS_SPIE_SHIFT;
    rv->csr_sstatus |= (sstatus_spie << SSTATUS_SIE_SHIFT);
    rv->csr_sstatus |= SSTATUS_SPIE;

    rv->PC = rv->csr_sepc;

    return true;
})
#endif

/* HRET: return from traps in H-mode */
RVOP(hret, {
    /* FIXME: Implement */
    return false;
})

/* MRET: return from traps in M-mode */
RVOP(mret, {
    rv->priv_mode = (rv->csr_mstatus & MSTATUS_MPP) >> MSTATUS_MPP_SHIFT;
    rv->csr_mstatus &= ~(MSTATUS_MPP);

    const uint32_t mstatus_mpie =
        (rv->csr_mstatus & MSTATUS_MPIE) >> MSTATUS_MPIE_SHIFT;
    rv->csr_mstatus |= (mstatus_mpie << MSTATUS_MIE_SHIFT);
    rv->csr_mstatus |= MSTATUS_MPIE;

    rv->PC = rv->csr_mepc;
    return true;
})

/* SFENCE.VMA: synchronize updates to in-memory memory-management data
 * structures with current execution.
 * This instruction invalidates TLB entries:
 * - rs1 = 0: all TLB entries (global flush)
 * - rs1 != 0: only the entry for virtual address in rs1
 * The rs2 field specifies ASID (not implemented, treated as global).
 *
 * For JIT mode, we also invalidate compiled blocks that may contain stale
 * VA→PA mappings. This is necessary when PTEs are modified without changing
 * SATP (e.g., munmap + mmap to different PA, or mprotect changes).
 */
RVOP(sfencevma, {
    PC += 4;
#if RV32_HAS(SYSTEM)
    if (ir->rs1 == 0) {
        /* Global flush: invalidate all TLB entries */
        mmu_tlb_flush_all(rv);
#if RV32_HAS(JIT)
#if RV32_HAS(T2C)
        /* Hold cache_lock during invalidation to prevent race with T2C
         * compilation thread. This ensures the invalidated flag and hot2 reset
         * are seen atomically by the T2C thread.
         */
        pthread_mutex_lock(&rv->cache_lock);
#endif
        /* Invalidate JIT blocks with current SATP */
        cache_invalidate_satp(rv->block_cache, rv->csr_satp);
#if RV32_HAS(T2C)
        jit_cache_clear(rv->jit_cache);
        inline_cache_clear(rv->inline_cache);
        pthread_mutex_unlock(&rv->cache_lock);
#endif
#endif
    } else {
        /* Selective flush: invalidate TLB entry for specific VA */
        uint32_t va = rv->X[ir->rs1];
        mmu_tlb_flush(rv, va);
#if RV32_HAS(JIT)
#if RV32_HAS(T2C)
        /* Hold cache_lock during invalidation to prevent race with T2C
         * compilation thread.
         */
        pthread_mutex_lock(&rv->cache_lock);
#endif
        /* Invalidate JIT blocks in the target VA page */
        cache_invalidate_va(rv->block_cache, va, rv->csr_satp);
#if RV32_HAS(T2C)
        /* Selectively clear only jit_cache entries matching the VA page */
        jit_cache_clear_page(rv->jit_cache, va, rv->csr_satp);
        inline_cache_clear_page(rv->inline_cache, va, rv->csr_satp);
        pthread_mutex_unlock(&rv->cache_lock);
#endif
#endif
    }
#endif
    goto end_op;
})

#if RV32_HAS(Zifencei) /* RV32 Zifencei Standard Extension */
/* FENCE.I: Instruction fence for self-modifying code synchronization.
 * Ensures that stores to instruction memory are visible to instruction fetches.
 * Must invalidate all cached/JITed code since instruction stream may have
 * changed.
 *
 * Unlike SFENCE.VMA which handles virtual memory changes, FENCE.I handles
 * instruction cache coherence - required when code modifies itself or loads
 * new code (e.g., dynamic linkers, JIT compilers running inside the guest).
 */
RVOP(fencei, {
    PC += 4;
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
#if RV32_HAS(T2C)
    /* Hold cache_lock during invalidation to prevent race with T2C
     * compilation thread. Same locking protocol as SFENCE.VMA.
     */
    pthread_mutex_lock(&rv->cache_lock);
#endif
    /* Invalidate all JIT blocks for current address space.
     * FENCE.I is a global instruction cache barrier - must clear all cached
     * code since we don't know which addresses were modified.
     * Uses same invalidation as global SFENCE.VMA (rs1=0).
     */
    cache_invalidate_satp(rv->block_cache, rv->csr_satp);
#if RV32_HAS(T2C)
    jit_cache_clear(rv->jit_cache);
    inline_cache_clear(rv->inline_cache);
    pthread_mutex_unlock(&rv->cache_lock);
#endif
#endif
    /* Note: In non-system JIT mode, self-modifying code is rare and blocks
     * will be naturally evicted. Full cache invalidation is not implemented
     * for this case as it would require additional infrastructure.
     */
    rv->csr_cycle = cycle;
    rv->PC = PC;
    return true;
})
#endif

#if RV32_HAS(Zicsr) /* RV32 Zicsr Standard Extension */
/* CSRRW: Atomic Read/Write CSR */
RVOP(csrrw, {
    uint32_t tmp = csr_csrrw(rv, ir->imm, rv->X[ir->rs1], cycle);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRS: Atomic Read and Set Bits in CSR */
/* The initial value in integer register rs1 is treated as a bit mask that
 * specifies the bit positions to be set in the CSR. Any bit that is set in
 * rs1 will result in the corresponding bit being set in the CSR, provided
 * that the CSR bit is writable. Other bits in the CSR remain unaffected,
 * although some CSRs might exhibit side effects when written to.
 *
 * See Page 56 of the RISC-V Unprivileged Specification.
 */
RVOP(csrrs, {
    uint32_t tmp = csr_csrrs(
        rv, ir->imm, (ir->rs1 == rv_reg_zero) ? 0U : rv->X[ir->rs1], cycle);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRC: Atomic Read and Clear Bits in CSR */
RVOP(csrrc, {
    uint32_t tmp = csr_csrrc(
        rv, ir->imm, (ir->rs1 == rv_reg_zero) ? 0U : rv->X[ir->rs1], cycle);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRWI */
RVOP(csrrwi, {
    uint32_t tmp = csr_csrrw(rv, ir->imm, ir->rs1, cycle);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRSI */
RVOP(csrrsi, {
    uint32_t tmp = csr_csrrs(rv, ir->imm, ir->rs1, cycle);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})

/* CSRRCI */
RVOP(csrrci, {
    uint32_t tmp = csr_csrrc(rv, ir->imm, ir->rs1, cycle);
    rv->X[ir->rd] = ir->rd ? tmp : rv->X[ir->rd];
})
#endif

/* RV32M Standard Extension */

#if RV32_HAS(EXT_M)
/* MUL: Multiply */
RVOP(mul, {
    const int64_t multiplicand = (int32_t) rv->X[ir->rs1];
    const int64_t multiplier = (int32_t) rv->X[ir->rs2];
    rv->X[ir->rd] =
        ((uint64_t) (multiplicand * multiplier)) & ((1ULL << 32) - 1);
})

/* MULH: Multiply High Signed Signed */
/* It is important to first cast rs1 and rs2 to i32 so that the subsequent
 * cast to i64 sign-extends the register values.
 */
RVOP(mulh, {
    const int64_t multiplicand = (int32_t) rv->X[ir->rs1];
    const int64_t multiplier = (int32_t) rv->X[ir->rs2];
    rv->X[ir->rd] = ((uint64_t) (multiplicand * multiplier)) >> 32;
})

/* MULHSU: Multiply High Signed Unsigned */
/* It is essential to perform an initial cast of rs1 to i32, ensuring that the
 * subsequent cast to i64 results in sign extension of the register value.
 * Additionally, rs2 should not undergo sign extension.
 */
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

/* clang-format off */
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
                        ? 0  : (dividend
                        % divisor);
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
    rv->X[ir->rd] = !udivisor ? udividend : udividend
    % udivisor;
})
/* clang-format on */
#endif

/* RV32A Standard Extension */

#if RV32_HAS(EXT_A)
/* The Atomic Memory Operation (AMO) instructions execute read-modify-write
 * operations to synchronize multiple processors and are encoded in an R-type
 * instruction format.
 *
 * These AMO instructions guarantee atomicity when loading a data value from
 * the memory address stored in the register rs1. The loaded value is then
 * transferred to the register rd, where a binary operator is applied to this
 * value and the original value stored in the register rs2. Finally, the
 * resulting value is stored back to the memory address in rs1, ensuring
 * atomicity.
 *
 * AMOs support the manipulation of 64-bit words exclusively in RV64, whereas
 * both 64-bit and 32-bit words can be manipulated in other systems. In RV64,
 * when performing 32-bit AMOs, the value placed in the register rd is always
 * sign-extended.
 *
 * At present, AMO is not implemented atomically because the emulated RISC-V
 * core just runs on single thread, and no out-of-order execution happens.
 * In addition, rl/aq are not handled.
 */

/* LR.W: Load Reserved */
RVOP(lrw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    if (ir->rd)
        rv->X[ir->rd] = MEM_READ_W(rv, addr);
    /* skip registration of the 'reservation set'
     * FIXME: unimplemented
     */
})

/* SC.W: Store Conditional */
RVOP(scw, {
    /* assume the 'reservation set' is valid
     * FIXME: unimplemented
     */
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    const uint32_t value = rv->X[ir->rs2];
    MEM_WRITE_W(rv, addr, value);
    rv->X[ir->rd] = 0;
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})

/* AMOSWAP.W: Atomic Swap */
RVOP(amoswapw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    MEM_WRITE_W(rv, addr, value2);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value2);
#endif
})

/* AMOADD.W: Atomic ADD */
RVOP(amoaddw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const uint32_t res = value1 + value2;
    MEM_WRITE_W(rv, addr, res);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, res);
#endif
})

/* AMOXOR.W: Atomic XOR */
RVOP(amoxorw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const uint32_t res = value1 ^ value2;
    MEM_WRITE_W(rv, addr, res);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, res);
#endif
})

/* AMOAND.W: Atomic AND */
RVOP(amoandw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const uint32_t res = value1 & value2;
    MEM_WRITE_W(rv, addr, res);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, res);
#endif
})

/* AMOOR.W: Atomic OR */
RVOP(amoorw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const uint32_t res = value1 | value2;
    MEM_WRITE_W(rv, addr, res);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, res);
#endif
})

/* AMOMIN.W: Atomic MIN */
RVOP(amominw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const int32_t a = value1;
    const int32_t b = value2;
    const uint32_t res = a < b ? value1 : value2;
    MEM_WRITE_W(rv, addr, res);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, res);
#endif
})

/* AMOMAX.W: Atomic MAX */
RVOP(amomaxw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const int32_t a = value1;
    const int32_t b = value2;
    const uint32_t res = a > b ? value1 : value2;
    MEM_WRITE_W(rv, addr, res);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, res);
#endif
})

/* AMOMINU.W */
RVOP(amominuw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const uint32_t ures = value1 < value2 ? value1 : value2;
    MEM_WRITE_W(rv, addr, ures);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, ures);
#endif
})

/* AMOMAXU.W */
RVOP(amomaxuw, {
    const uint32_t addr = rv->X[ir->rs1];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value1 = MEM_READ_W(rv, addr);
    const uint32_t value2 = rv->X[ir->rs2];
    if (ir->rd)
        rv->X[ir->rd] = value1;
    const uint32_t ures = value1 > value2 ? value1 : value2;
    MEM_WRITE_W(rv, addr, ures);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, ures);
#endif
})
#endif /* RV32_HAS(EXT_A) */

/* RV32F Standard Extension */

#if RV32_HAS(EXT_F)
/* FLW */
RVOP(flw, {
    /* copy into the float register */
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->F[ir->rd].v = MEM_READ_W(rv, addr);
})

/* FSW */
RVOP(fsw, {
    /* copy from float registers */
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    const uint32_t value = rv->F[ir->rs2].v;
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})

/* FMADD.S */
RVOP(fmadds, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = f32_mulAdd(rv->F[ir->rs1], rv->F[ir->rs2], rv->F[ir->rs3]);
    set_fflag(rv);
})

/* FMSUB.S */
RVOP(fmsubs, {
    set_rounding_mode(rv, ir->rm);
    riscv_float_t tmp = rv->F[ir->rs3];
    tmp.v ^= FMASK_SIGN;
    rv->F[ir->rd] = f32_mulAdd(rv->F[ir->rs1], rv->F[ir->rs2], tmp);
    set_fflag(rv);
})

/* FNMSUB.S */
RVOP(fnmsubs, {
    set_rounding_mode(rv, ir->rm);
    riscv_float_t tmp = rv->F[ir->rs1];
    tmp.v ^= FMASK_SIGN;
    rv->F[ir->rd] = f32_mulAdd(tmp, rv->F[ir->rs2], rv->F[ir->rs3]);
    set_fflag(rv);
})

/* FNMADD.S */
RVOP(fnmadds, {
    set_rounding_mode(rv, ir->rm);
    riscv_float_t tmp1 = rv->F[ir->rs1];
    riscv_float_t tmp2 = rv->F[ir->rs3];
    tmp1.v ^= FMASK_SIGN;
    tmp2.v ^= FMASK_SIGN;
    rv->F[ir->rd] = f32_mulAdd(tmp1, rv->F[ir->rs2], tmp2);
    set_fflag(rv);
})

/* FADD.S */
RVOP(fadds, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = f32_add(rv->F[ir->rs1], rv->F[ir->rs2]);
    set_fflag(rv);
})

/* FSUB.S */
RVOP(fsubs, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = f32_sub(rv->F[ir->rs1], rv->F[ir->rs2]);
    set_fflag(rv);
})

/* FMUL.S */
RVOP(fmuls, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = f32_mul(rv->F[ir->rs1], rv->F[ir->rs2]);
    set_fflag(rv);
})

/* FDIV.S */
RVOP(fdivs, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = f32_div(rv->F[ir->rs1], rv->F[ir->rs2]);
    set_fflag(rv);
})

/* FSQRT.S */
RVOP(fsqrts, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = f32_sqrt(rv->F[ir->rs1]);
    set_fflag(rv);
})

/* FSGNJ.S */
RVOP(fsgnjs, {
    rv->F[ir->rd].v =
        (rv->F[ir->rs1].v & ~FMASK_SIGN) | (rv->F[ir->rs2].v & FMASK_SIGN);
})

/* FSGNJN.S */
RVOP(fsgnjns, {
    rv->F[ir->rd].v =
        (rv->F[ir->rs1].v & ~FMASK_SIGN) | (~rv->F[ir->rs2].v & FMASK_SIGN);
})

/* FSGNJX.S */
RVOP(fsgnjxs,
     { rv->F[ir->rd].v = rv->F[ir->rs1].v ^ (rv->F[ir->rs2].v & FMASK_SIGN); })

/* FMIN.S
 * In IEEE754-201x, fmin(x, y) return
 * - min(x,y) if both numbers are not NaN
 * - if one is NaN and another is a number, return the number
 * - if both are NaN, return NaN
 * When input is signaling NaN, raise invalid operation
 */
RVOP(fmins, {
    if (f32_isSignalingNaN(rv->F[ir->rs1]) ||
        f32_isSignalingNaN(rv->F[ir->rs2]))
        rv->csr_fcsr |= FFLAG_INVALID_OP;
    bool less = f32_lt_quiet(rv->F[ir->rs1], rv->F[ir->rs2]) ||
                (f32_eq(rv->F[ir->rs1], rv->F[ir->rs2]) &&
                 (rv->F[ir->rs1].v & FMASK_SIGN));
    if (is_nan(rv->F[ir->rs1].v) && is_nan(rv->F[ir->rs2].v))
        rv->F[ir->rd].v = RV_NAN;
    else
        rv->F[ir->rd] = (less || is_nan(rv->F[ir->rs2].v) ? rv->F[ir->rs1]
                                                          : rv->F[ir->rs2]);
})

/* FMAX.S */
RVOP(fmaxs, {
    if (f32_isSignalingNaN(rv->F[ir->rs1]) ||
        f32_isSignalingNaN(rv->F[ir->rs2]))
        rv->csr_fcsr |= FFLAG_INVALID_OP;
    bool greater = f32_lt_quiet(rv->F[ir->rs2], rv->F[ir->rs1]) ||
                   (f32_eq(rv->F[ir->rs1], rv->F[ir->rs2]) &&
                    (rv->F[ir->rs2].v & FMASK_SIGN));
    if (is_nan(rv->F[ir->rs1].v) && is_nan(rv->F[ir->rs2].v))
        rv->F[ir->rd].v = RV_NAN;
    else
        rv->F[ir->rd] = (greater || is_nan(rv->F[ir->rs2].v) ? rv->F[ir->rs1]
                                                             : rv->F[ir->rs2]);
})

/* FCVT.W.S and FCVT.WU.S convert a floating point number to an integer,
 * the rounding mode is specified in rm field.
 */

/* FCVT.W.S */
RVOP(fcvtws, {
    set_rounding_mode(rv, ir->rm);
    uint32_t ret = f32_to_i32(rv->F[ir->rs1], softfloat_roundingMode, true);
    if (ir->rd)
        rv->X[ir->rd] = ret;
    set_fflag(rv);
})

/* FCVT.WU.S */
RVOP(fcvtwus, {
    set_rounding_mode(rv, ir->rm);
    uint32_t ret = f32_to_ui32(rv->F[ir->rs1], softfloat_roundingMode, true);
    if (ir->rd)
        rv->X[ir->rd] = ret;
    set_fflag(rv);
})

/* FMV.X.W */
RVOP(fmvxw, {
    if (ir->rd)
        rv->X[ir->rd] = rv->F[ir->rs1].v;
})

/* FEQ.S performs a quiet comparison: it only sets the invalid operation
 * exception flag if either input is a signaling NaN.
 */
RVOP(feqs, {
    uint32_t ret = f32_eq(rv->F[ir->rs1], rv->F[ir->rs2]);
    if (ir->rd)
        rv->X[ir->rd] = ret;
    set_fflag(rv);
})

/* FLT.S and FLE.S perform what the IEEE 754-2008 standard refers to as
 * signaling comparisons: that is, they set the invalid operation exception
 * flag if either input is NaN.
 */
RVOP(flts, {
    uint32_t ret = f32_lt(rv->F[ir->rs1], rv->F[ir->rs2]);
    if (ir->rd)
        rv->X[ir->rd] = ret;
    set_fflag(rv);
})

RVOP(fles, {
    uint32_t ret = f32_le(rv->F[ir->rs1], rv->F[ir->rs2]);
    if (ir->rd)
        rv->X[ir->rd] = ret;
    set_fflag(rv);
})

/* FCLASS.S */
RVOP(fclasss, {
    if (ir->rd)
        rv->X[ir->rd] = calc_fclass(rv->F[ir->rs1].v);
})

/* FCVT.S.W */
RVOP(fcvtsw, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = i32_to_f32(rv->X[ir->rs1]);
    set_fflag(rv);
})

/* FCVT.S.WU */
RVOP(fcvtswu, {
    set_rounding_mode(rv, ir->rm);
    rv->F[ir->rd] = ui32_to_f32(rv->X[ir->rs1]);
    set_fflag(rv);
})

/* FMV.W.X */
RVOP(fmvwx, { rv->F[ir->rd].v = rv->X[ir->rs1]; })
#endif

/* RV32C Standard Extension */

#if RV32_HAS(EXT_C)
/* C.ADDI4SPN is a CIW-format instruction that adds a zero-extended non-zero
 * immediate, scaledby 4, to the stack pointer, x2, and writes the result to
 * rd'.
 * This instruction is used to generate pointers to stack-allocated variables,
 * and expands to addi rd', x2, nzuimm[9:2].
 */
RVOP(caddi4spn, { rv->X[ir->rd] = rv->X[rv_reg_sp] + (uint16_t) ir->imm; })

/* C.LW loads a 32-bit value from memory into register rd'. It computes an
 * effective address by adding the zero-extended offset, scaled by 4, to the
 * base address in register rs1'. It expands to lw rd', offset[6:2](rs1').
 */
RVOP(clw, {
    const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, true, 1);
    rv->X[ir->rd] = MEM_READ_W(rv, addr);
})

/* C.SW stores a 32-bit value in register rs2' to memory. It computes an
 * effective address by adding the zero-extended offset, scaled by 4, to the
 * base address in register rs1'.
 * It expands to sw rs2', offset[6:2](rs1').
 */
RVOP(csw, {
    const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, STORE, true, 1);
    const uint32_t value = rv->X[ir->rs2];
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
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
    rv->X[rv_reg_ra] = PC + 2;
    PC += ir->imm;
    struct rv_insn *taken = ir->branch_taken;
    if (taken) {
#if RV32_HAS(JIT)
        IIF(RV32_HAS(SYSTEM))(block_t *next =, )
            cache_get(rv->block_cache, PC, true);
        IIF(RV32_HAS(SYSTEM))(
            if (next->satp == rv->csr_satp && !next->invalidated), )
        {
            if (!set_add(&pc_set, PC))
                has_loops = true;
            if (cache_hot(rv->block_cache, PC))
                goto end_op;
        }
#endif

#if RV32_HAS(SYSTEM)
        if (!rv->is_trapped)
#endif
        {
            last_pc = PC;
            MUST_TAIL return taken->impl(rv, taken, cycle, PC);
        }
    }
    goto end_op;
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
    PC += ir->imm;
    struct rv_insn *taken = ir->branch_taken;
    if (taken) {
#if RV32_HAS(JIT)
        IIF(RV32_HAS(SYSTEM))(block_t *next =, )
            cache_get(rv->block_cache, PC, true);
        IIF(RV32_HAS(SYSTEM))(
            if (next->satp == rv->csr_satp && !next->invalidated), )
        {
            if (!set_add(&pc_set, PC))
                has_loops = true;
            if (cache_hot(rv->block_cache, PC))
                goto end_op;
        }
#endif
#if RV32_HAS(SYSTEM)
        if (!rv->is_trapped)
#endif
        {
            last_pc = PC;
            MUST_TAIL return taken->impl(rv, taken, cycle, PC);
        }
    }
    goto end_op;
})

/* C.BEQZ performs conditional control transfers. The offset is sign-extended
 * and added to the pc to form the branch target address.
 * It can therefore target a ±256 B range. C.BEQZ takes the branch if the
 * value in register rs1' is zero. It expands to beq rs1', x0, offset[8:1].
 */
RVOP(cbeqz, {
    if (rv->X[ir->rs1]) {
        is_branch_taken = false;
        struct rv_insn *untaken = ir->branch_untaken;
        if (!untaken)
            goto nextop;
#if RV32_HAS(JIT)
        IIF(RV32_HAS(SYSTEM))(block_t *next =, )
            cache_get(rv->block_cache, PC + 2, true);
        IIF(RV32_HAS(SYSTEM))(
            if (next->satp == rv->csr_satp && !next->invalidated), )
        {
            if (!set_add(&pc_set, PC + 2))
                has_loops = true;
            if (cache_hot(rv->block_cache, PC + 2))
                goto nextop;
        }
#endif
        PC += 2;
#if RV32_HAS(SYSTEM)
        if (!rv->is_trapped)
#endif
        {
            last_pc = PC;
            MUST_TAIL return untaken->impl(rv, untaken, cycle, PC);
        }

        goto end_op;
    }
    is_branch_taken = true;
    PC += ir->imm;
    struct rv_insn *taken = ir->branch_taken;
    if (taken) {
#if RV32_HAS(JIT)
        IIF(RV32_HAS(SYSTEM))(block_t *next =, )
            cache_get(rv->block_cache, PC, true);
        IIF(RV32_HAS(SYSTEM))(
            if (next->satp == rv->csr_satp && !next->invalidated), )
        {
            if (!set_add(&pc_set, PC))
                has_loops = true;
            if (cache_hot(rv->block_cache, PC))
                goto end_op;
        }
#endif
#if RV32_HAS(SYSTEM)
        if (!rv->is_trapped)
#endif
        {
            last_pc = PC;
            MUST_TAIL return taken->impl(rv, taken, cycle, PC);
        }
    }
    goto end_op;
})

/* C.BEQZ */
RVOP(cbnez, {
    if (!rv->X[ir->rs1]) {
        is_branch_taken = false;
        struct rv_insn *untaken = ir->branch_untaken;
        if (!untaken)
            goto nextop;
#if RV32_HAS(JIT)
        IIF(RV32_HAS(SYSTEM))(block_t *next =, )
            cache_get(rv->block_cache, PC + 2, true);
        IIF(RV32_HAS(SYSTEM))(
            if (next->satp == rv->csr_satp && !next->invalidated), )
        {
            if (!set_add(&pc_set, PC + 2))
                has_loops = true;
            if (cache_hot(rv->block_cache, PC + 2))
                goto nextop;
        }
#endif
        PC += 2;
#if RV32_HAS(SYSTEM)
        if (!rv->is_trapped)
#endif
        {
            last_pc = PC;
            MUST_TAIL return untaken->impl(rv, untaken, cycle, PC);
        }

        goto end_op;
    }
    is_branch_taken = true;
    PC += ir->imm;
    struct rv_insn *taken = ir->branch_taken;
    if (taken) {
#if RV32_HAS(JIT)
        IIF(RV32_HAS(SYSTEM))(block_t *next =, )
            cache_get(rv->block_cache, PC, true);
        IIF(RV32_HAS(SYSTEM))(
            if (next->satp == rv->csr_satp && !next->invalidated), )
        {
            if (!set_add(&pc_set, PC))
                has_loops = true;
            if (cache_hot(rv->block_cache, PC))
                goto end_op;
        }
#endif
#if RV32_HAS(SYSTEM)
        if (!rv->is_trapped)
#endif
        {
            last_pc = PC;
            MUST_TAIL return taken->impl(rv, taken, cycle, PC);
        }
    }
    goto end_op;
})

/* C.SLLI is a CI-format instruction that performs a logical left shift of
 * the value in register rd then writes the result to rd. The shift amount
 * is encoded in the shamt field. C.SLLI expands into slli rd, rd, shamt[5:0].
 */
RVOP(cslli, { rv->X[ir->rd] <<= (uint8_t) ir->imm; })

/* C.LWSP */
RVOP(clwsp, {
    const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, true, 1);
    rv->X[ir->rd] = MEM_READ_W(rv, addr);
})

/* C.JR */
RVOP(cjr, {
    PC = rv->X[ir->rs1];
    LOOKUP_OR_UPDATE_BRANCH_HISTORY_TABLE();
    goto end_op;
})

/* C.MV */
RVOP(cmv, { rv->X[ir->rd] = rv->X[ir->rs2]; })

/* C.EBREAK */
RVOP(cebreak, {
    rv->compressed = true;
    rv->csr_cycle = cycle;
    rv->PC = PC;
    rv->io.on_ebreak(rv);
    return true;
})

/* C.JALR */
RVOP(cjalr, {
    /* Unconditional jump and store PC+2 to ra */
    const int32_t jump_to = rv->X[ir->rs1];
    rv->X[rv_reg_ra] = PC + 2;
    PC = jump_to;
    LOOKUP_OR_UPDATE_BRANCH_HISTORY_TABLE();
    goto end_op;
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
    const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, STORE, true, 1);
    const uint32_t value = rv->X[ir->rs2];
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})
#endif

#if RV32_HAS(EXT_C) && RV32_HAS(EXT_F)
/* C.FLWSP */
RVOP(cflwsp, {
    const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->F[ir->rd].v = MEM_READ_W(rv, addr);
})

/* C.FSWSP */
RVOP(cfswsp, {
    const uint32_t addr = rv->X[rv_reg_sp] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    const uint32_t value = rv->F[ir->rs2].v;
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})

/* C.FLW */
RVOP(cflw, {
    const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->F[ir->rd].v = MEM_READ_W(rv, addr);
})

/* C.FSW */
RVOP(cfsw, {
    const uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    const uint32_t value = rv->F[ir->rs2].v;
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
})
#endif

/* RV32Zba Standard Extension */

#if RV32_HAS(Zba)

/* SH1ADD */
RVOP(sh1add, { rv->X[ir->rd] = (rv->X[ir->rs1] << 1) + rv->X[ir->rs2]; })

/* SH2ADD */
RVOP(sh2add, { rv->X[ir->rd] = (rv->X[ir->rs1] << 2) + rv->X[ir->rs2]; })

/* SH3ADD */
RVOP(sh3add, { rv->X[ir->rd] = (rv->X[ir->rs1] << 3) + rv->X[ir->rs2]; })

#endif

/* RV32Zbb Standard Extension */

#if RV32_HAS(Zbb)

/* ANDN */
RVOP(andn, { rv->X[ir->rd] = rv->X[ir->rs1] & (~rv->X[ir->rs2]); })

/* ORN */
RVOP(orn, { rv->X[ir->rd] = rv->X[ir->rs1] | (~rv->X[ir->rs2]); })

/* XNOR */
RVOP(xnor, { rv->X[ir->rd] = ~(rv->X[ir->rs1] ^ rv->X[ir->rs2]); })

/* CLZ */
RVOP(clz, {
    if (rv->X[ir->rs1])
        rv->X[ir->rd] = rv_clz(rv->X[ir->rs1]);
    else
        rv->X[ir->rd] = 32;
})

/* CTZ */
RVOP(ctz, {
    if (rv->X[ir->rs1])
        rv->X[ir->rd] = rv_ctz(rv->X[ir->rs1]);
    else
        rv->X[ir->rd] = 32;
})

/* CPOP */
RVOP(cpop, { rv->X[ir->rd] = rv_popcount(rv->X[ir->rs1]); })

/* MAX */
RVOP(max, {
    const int32_t x = rv->X[ir->rs1];
    const int32_t y = rv->X[ir->rs2];
    rv->X[ir->rd] = x > y ? rv->X[ir->rs1] : rv->X[ir->rs2];
})

/* MIN */
RVOP(min, {
    const int32_t x = rv->X[ir->rs1];
    const int32_t y = rv->X[ir->rs2];
    rv->X[ir->rd] = x < y ? rv->X[ir->rs1] : rv->X[ir->rs2];
})

/* MAXU */
RVOP(maxu, {
    const uint32_t x = rv->X[ir->rs1];
    const uint32_t y = rv->X[ir->rs2];
    rv->X[ir->rd] = x > y ? rv->X[ir->rs1] : rv->X[ir->rs2];
})

/* MINU */
RVOP(minu, {
    const uint32_t x = rv->X[ir->rs1];
    const uint32_t y = rv->X[ir->rs2];
    rv->X[ir->rd] = x < y ? rv->X[ir->rs1] : rv->X[ir->rs2];
})

/* SEXT.B */
RVOP(sextb, {
    rv->X[ir->rd] = rv->X[ir->rs1] & 0xff;
    if (rv->X[ir->rs1] & (1U << 7))
        rv->X[ir->rd] |= 0xffffff00;
})

/* SEXT.H */
RVOP(sexth, {
    rv->X[ir->rd] = rv->X[ir->rs1] & 0xffff;
    if (rv->X[ir->rs1] & (1U << 15))
        rv->X[ir->rd] |= 0xffff0000;
})

/* ZEXT.H */
RVOP(zexth, { rv->X[ir->rd] = rv->X[ir->rs1] & 0x0000ffff; })

/* ROL */
RVOP(rol, {
    const unsigned int shamt = rv->X[ir->rs2] & 0b11111;
    rv->X[ir->rd] =
        (rv->X[ir->rs1] << shamt) | (rv->X[ir->rs1] >> (32 - shamt));
})

/* ROR */
RVOP(ror, {
    const unsigned int shamt = rv->X[ir->rs2] & 0b11111;
    rv->X[ir->rd] =
        (rv->X[ir->rs1] >> shamt) | (rv->X[ir->rs1] << (32 - shamt));
})

/* RORI */
RVOP(rori, {
    const unsigned int shamt = ir->imm & 0b11111;
    rv->X[ir->rd] =
        (rv->X[ir->rs1] >> shamt) | (rv->X[ir->rs1] << (32 - shamt));
})

/* ORCB */
RVOP(orcb, {
    const uint32_t x = rv->X[ir->rs1];
    rv->X[ir->rd] = 0;
    for (int i = 0; i < 4; i++)
        if (x & (0xffu << (i * 8)))
            rv->X[ir->rd] |= 0xffu << (i * 8);
})

/* REV8 */
RVOP(rev8, {
    rv->X[ir->rd] =
        (((rv->X[ir->rs1] & 0xffU) << 24) | ((rv->X[ir->rs1] & 0xff00U) << 8) |
         ((rv->X[ir->rs1] & 0xff0000U) >> 8) |
         ((rv->X[ir->rs1] & 0xff000000U) >> 24));
})

#endif

/* RV32Zbc Standard Extension */

#if RV32_HAS(Zbc)

/* CLMUL */
RVOP(clmul, {
    uint32_t output = 0;
    for (int i = 0; i < 32; i++)
        if ((rv->X[ir->rs2] >> i) & 1)
            output ^= rv->X[ir->rs1] << i;
    rv->X[ir->rd] = output;
})

/* CLMULH */
RVOP(clmulh, {
    uint32_t output = 0;
    for (int i = 1; i < 32; i++)
        if ((rv->X[ir->rs2] >> i) & 1)
            output ^= rv->X[ir->rs1] >> (32 - i);
    rv->X[ir->rd] = output;
})

/* CLMULR */
RVOP(clmulr, {
    uint32_t output = 0;
    for (int i = 0; i < 32; i++)
        if ((rv->X[ir->rs2] >> i) & 1)
            output ^= rv->X[ir->rs1] >> (32 - i - 1);
    rv->X[ir->rd] = output;
})

#endif

/* RV32Zbs Standard Extension */

#if RV32_HAS(Zbs)

/* BCLR */
RVOP(bclr, {
    const unsigned int index = rv->X[ir->rs2] & (32 - 1);
    rv->X[ir->rd] = rv->X[ir->rs1] & (~(1U << index));
})

/* BCLRI */
RVOP(bclri, {
    const unsigned int index = ir->imm & (32 - 1);
    rv->X[ir->rd] = rv->X[ir->rs1] & (~(1U << index));
})

/* BEXT */
RVOP(bext, {
    const unsigned int index = rv->X[ir->rs2] & (32 - 1);
    rv->X[ir->rd] = (rv->X[ir->rs1] >> index) & 1;
})

/* BEXTI */
RVOP(bexti, {
    const unsigned int index = ir->imm & (32 - 1);
    rv->X[ir->rd] = (rv->X[ir->rs1] >> index) & 1;
})

/* BINV */
RVOP(binv, {
    const unsigned int index = rv->X[ir->rs2] & (32 - 1);
    rv->X[ir->rd] = rv->X[ir->rs1] ^ (1U << index);
})

/* BINVI */
RVOP(binvi, {
    const unsigned int index = ir->imm & (32 - 1);
    rv->X[ir->rd] = rv->X[ir->rs1] ^ (1U << index);
})

/* BSET */
RVOP(bset, {
    const unsigned int index = rv->X[ir->rs2] & (32 - 1);
    rv->X[ir->rd] = rv->X[ir->rs1] | (1U << index);
})

/* BSETI */
RVOP(bseti, {
    const unsigned int index = ir->imm & (32 - 1);
    rv->X[ir->rd] = rv->X[ir->rs1] | (1U << index);
})

#endif

#if RV32_HAS(EXT_V)
#define V_NOP                        \
    for (int i = 0; i < 4; i++) {    \
        (rv)->V[rv_reg_zero][i] = 0; \
    }

#define VREG_U32_COUNT ((VLEN) >> (5))
/*
 * Vector Configuration-Setting Instructions
 *
 * These instructions set the vector CSRs, specifically csr_vl and csr_vtype.
 * The CSRs can only be updated using vset{i}vl{i} instructions. The current
 * implementation does not support vma and vta.
 *
 * The value VLMAX = (LMUL * VLEN) / SEW represents the maximum number of
 * elements that can be processed by a single vector instruction given the
 * current SEW and LMUL.
 *
 * Constraints on Setting vl:
 *  - vl = AVL if AVL ≤ VLMAX
 *  - ceil(AVL / 2) ≤ vl ≤ VLMAX if AVL < 2 * VLMAX
 *  - vl = VLMAX if AVL ≥ 2 * VLMAX
 *
 * +------------+------+--------------+
 * | vlmul[2:0] | LMUL |    VLMAX     |
 * +------------+------+--------------+
 * |    1 0 0   |  -   |       -      |
 * |    1 0 1   | 1/8  |  VLEN/SEW/8  |
 * |    1 1 0   | 1/4  |  VLEN/SEW/4  |
 * |    1 1 1   | 1/2  |  VLEN/SEW/2  |
 * |    0 0 0   |  1   |  VLEN/SEW    |
 * |    0 0 1   |  2   |  2*VLEN/SEW  |
 * |    0 1 0   |  4   |  4*VLEN/SEW  |
 * |    0 1 1   |  8   |  8*VLEN/SEW  |
 * +------------+------+--------------+
 *
 * LMUL determines how vector registers are grouped. Since VL controls the
 * number of processed elements (based on SEW) and is derived from VLMAX,
 * LMUL's primary role is setting VLMAX. This implementation computes VLMAX
 * directly, avoiding fractional LMUL values (e.g., 1/2, 1/4, 1/8).
 *
 * Mapping of rd, rs1, and AVL value effects on vl:
 * +-----+-----+------------------+----------------------------------+
 * | rd  | rs1 |    AVL value     |         Effect on vl             |
 * +-----+-----+------------------+----------------------------------+
 * |  -  | !x0 | Value in x[rs1]  | Normal stripmining               |
 * | !x0 |  x0 | ~0               | Set vl to VLMAX                  |
 * |  x0 |  x0 | Value in vl reg  | Keep existing vl                 |
 * +-----+-----+------------------+----------------------------------+
 *
 * +------------+----------+
 * | vsew[2:0]  |   SEW    |
 * +------------+----------+
 * |    0 0 0   |     8    |
 * |    0 0 1   |    16    |
 * |    0 1 0   |    32    |
 * |    0 1 1   |    64    |
 * |    1 X X   | Reserved |
 * +------------+----------+
 */

#define vl_setting(vlmax_, rs1, vl)    \
    if ((rs1) <= vlmax_) {             \
        (vl) = (rs1);                  \
    } else if ((rs1) < (2 * vlmax_)) { \
        (vl) = vlmax_;                 \
    } else {                           \
        (vl) = vlmax_;                 \
    }

RVOP(
    vsetvli,
    {
        uint8_t v_lmul = ir->zimm & 0b111;
        uint8_t v_sew = (ir->zimm >> 3) & 0b111;

        if (v_lmul == 4 || v_sew >= 4) {
            /* Illegal setting */
            rv->csr_vl = 0;
            rv->csr_vtype = 0x80000000;
            return true;
        }
        uint16_t vlmax = (v_lmul < 4)
                             ? ((1 << v_lmul) * VLEN) >> (3 + v_sew)
                             : (VLEN >> (3 + v_sew) >> (3 - (v_lmul - 5)));
        if (ir->rs1) {
            vl_setting(vlmax, rv->X[ir->rs1], rv->csr_vl);
            rv->csr_vtype = ir->zimm;
        } else {
            if (!ir->rd) {
                rv->csr_vtype = ir->zimm;
            } else {
                rv->csr_vl = vlmax;
                rv->csr_vtype = ir->zimm;
            }
        }
        rv->X[ir->rd] = rv->csr_vl;
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsetivli,
    {
        uint8_t v_lmul = ir->zimm & 0b111;
        uint8_t v_sew = (ir->zimm >> 3) & 0b111;

        if (v_lmul == 4 || v_sew >= 4) {
            /* Illegal setting */
            rv->csr_vl = 0;
            rv->csr_vtype = 0x80000000;
            return true;
        }
        uint16_t vlmax = (v_lmul < 4)
                             ? ((1 << v_lmul) * VLEN) >> (3 + v_sew)
                             : (VLEN >> (3 + v_sew) >> (3 - (v_lmul - 5)));
        if (ir->rs1) {
            vl_setting(vlmax, ir->rs1, rv->csr_vl);
            rv->csr_vtype = ir->zimm;
        } else {
            if (!ir->rd) {
                rv->csr_vtype = ir->zimm;
            } else {
                rv->csr_vl = vlmax;
                rv->csr_vtype = ir->zimm;
            }
        }
        rv->X[ir->rd] = rv->csr_vl;
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsetvl,
    {
        uint8_t v_lmul = rv->X[ir->rs2] & 0b111;
        uint8_t v_sew = (rv->X[ir->rs2] >> 3) & 0b111;

        if (v_lmul == 4 || v_sew >= 4) {
            /* Illegal setting */
            rv->csr_vl = 0;
            rv->csr_vtype = 0x80000000;
            return true;
        }
        uint16_t vlmax = (v_lmul < 4)
                             ? ((1 << v_lmul) * VLEN) >> (3 + v_sew)
                             : (VLEN >> (3 + v_sew) >> (3 - (v_lmul - 5)));
        if (rv->X[ir->rs1]) {
            vl_setting(vlmax, rv->X[ir->rs1], rv->csr_vl);
            rv->csr_vtype = rv->X[ir->rs2];
        } else {
            if (!ir->rd) {
                rv->csr_vtype = rv->X[ir->rs2];
            } else {
                rv->csr_vl = vlmax;
                rv->csr_vtype = rv->X[ir->rs2];
            }
        }
        rv->X[ir->rd] = rv->csr_vl;
    },
    GEN({
        assert; /* FIXME: Implement */
    }))
#undef vl_setting

RVOP(
    vle8_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 4;) {
                i %= VREG_U32_COUNT;
                /* Set illegal when trying to access vector register that is
                 * larger then 31.
                 */
                assert(ir->vd + j < 32);
                /* Process full 32-bit words */
                rv->V[ir->vd + j][i] = 0;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr);
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + 1) << 8;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + 2) << 16;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + 3) << 24;
                cnt += 4;
                i++;

                /* Move to next vector register after filling VLEN */
                if (!(cnt % (VREG_U32_COUNT << 2))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            /* Clear corresponding bits of eews */
            if (rv->csr_vl % 4) {
                rv->V[ir->vd + j][i] %= 0xFFFFFFFF << ((rv->csr_vl % 4) << 3);
            }
            /* Handle eews that is narrower then a word */
            for (uint32_t cnt = 0; cnt < (rv->csr_vl % 4); cnt++) {
                assert(ir->vd + j < 32); /* Illegal */
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + cnt)
                                        << (cnt << 3);
            }
        }
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vle16_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 2;) {
                i %= VREG_U32_COUNT;
                assert(ir->vd + j < 32);
                /* Process full 32-bit words */
                rv->V[ir->vd + j][i] = 0;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_s(rv, addr);
                rv->V[ir->vd + j][i] |= rv->io.mem_read_s(rv, addr + 2) << 16;
                cnt += 2;
                i++;

                /* Move to next vector register after filling VLEN */
                if (!(cnt % (VREG_U32_COUNT << 1))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            if (rv->csr_vl % 2) {
                assert(ir->vd + j < 32); /* Illegal */
                rv->V[ir->vd + j][i] |= rv->io.mem_read_s(rv, addr);
            }
        }
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vle32_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl > cnt;) {
                i %= VREG_U32_COUNT;
                assert(ir->vd + j < 32);
                rv->V[ir->vd + j][i] = rv->io.mem_read_w(rv, addr);
                cnt += 1;
                i++;

                if (!(cnt % VREG_U32_COUNT)) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
        }
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vle64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl1re8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl1re16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl1re32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl1re64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl2re8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl2re16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))
RVOP(
    vl2re32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl2re64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl4re8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl4re16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl4re32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl4re64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl8re8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl8re16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl8re32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vl8re64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlm_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vle8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vle16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vle32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vle64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e8ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e16ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e32ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg2e64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg3e64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg4e64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg5e64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg6e64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg7e64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlseg8e64ff_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg2ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg3ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg4ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg5ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg6ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg7ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg8ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg2ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg3ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg4ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg5ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg6ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg7ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg8ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg2ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg3ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg4ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg5ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg6ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg7ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg8ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg2ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg3ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg4ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg5ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg6ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg7ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vluxseg8ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlse8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlse16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlse32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlse64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg2e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg3e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg4e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg5e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg6e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg7e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg8e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg2e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg3e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg4e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg5e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg6e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg7e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg8e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg2e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg3e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg4e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg5e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg6e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg7e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg8e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg2e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg3e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg4e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg5e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg6e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg7e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vlsseg8e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg2ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg3ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg4ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg5ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg6ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg7ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg8ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg2ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg3ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg4ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg5ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg6ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg7ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg8ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg2ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg3ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg4ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg5ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg6ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg7ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg8ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg2ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg3ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg4ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg5ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg6ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg7ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vloxseg8ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))


RVOP(
    vse8_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 4;) {
                i %= VREG_U32_COUNT;
                /* Set illegal when trying to access vector register that is
                 * larger then 31.
                 */
                assert(ir->vs3 + j < 32);
                uint32_t tmp = rv->V[ir->vs3 + j][i];
                /* Process full 32-bit words */
                rv->io.mem_write_b(rv, addr, (tmp) & 0xff);
                rv->io.mem_write_b(rv, addr + 1, (tmp >> 8) & 0xff);
                rv->io.mem_write_b(rv, addr + 2, (tmp >> 16) & 0xff);
                rv->io.mem_write_b(rv, addr + 3, (tmp >> 24) & 0xff);
                cnt += 4;
                i++;

                /* Move to next vector register after filling VLEN */
                if (!(cnt % (VREG_U32_COUNT << 2))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            /* Handle eews that is narrower then a word */
            for (uint32_t cnt = 0; cnt < (rv->csr_vl % 4); cnt++) {
                assert(ir->vs3 + j < 32); /* Illegal */
                uint8_t tmp = (rv->V[ir->vs3 + j][i] >> (cnt << 3)) & 0xff;
                rv->io.mem_write_b(rv, addr + cnt, tmp);
            }
        }
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vse16_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 2;) {
                i %= VREG_U32_COUNT;
                assert(ir->vs3 + j < 32);
                uint32_t tmp = rv->V[ir->vs3 + j][i];
                /* Process full 32-bit words */
                rv->io.mem_write_s(rv, addr, (tmp) & 0xffff);
                rv->io.mem_write_s(rv, addr + 2, (tmp >> 16) & 0xffff);
                cnt += 2;
                i++;

                if (!(cnt % (VREG_U32_COUNT << 1))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            if (rv->csr_vl % 2) {
                rv->io.mem_write_s(rv, addr, rv->V[ir->vs3 + j][i] & 0xffff);
            }
        }
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vse32_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl > cnt;) {
                i %= VREG_U32_COUNT;
                assert(ir->vs3 + j < 32);
                rv->io.mem_write_w(rv, addr, rv->V[ir->vs3 + j][i]);
                cnt += 1;
                i++;

                if (!(cnt % (VREG_U32_COUNT))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
        }
    },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vse64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg2e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg3e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg4e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg5e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg6e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg7e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg8e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg2e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg3e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg4e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg5e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg6e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg7e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg8e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg2e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg3e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg4e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg5e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg6e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg7e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg8e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg2e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg3e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg4e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg5e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg6e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg7e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsseg8e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vs1r_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vs2r_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vs4r_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vs8r_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsm_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg2ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg3ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg4ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg5ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg6ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg7ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg8ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg2ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg3ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg4ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg5ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg6ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg7ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg8ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg2ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg3ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg4ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg5ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg6ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg7ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg8ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg2ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg3ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg4ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg5ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg6ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg7ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsuxseg8ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsse8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsse16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsse32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsse64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg2e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg3e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg4e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg5e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg6e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg7e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg8e8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg2e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg3e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg4e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg5e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg6e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg7e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg8e16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg2e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg3e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg4e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg5e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg6e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg7e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg8e32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg2e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg3e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg4e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg5e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg6e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg7e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssseg8e64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg2ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg3ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg4ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg5ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg6ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg7ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg8ei8_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg2ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg3ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg4ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg5ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg6ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg7ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg8ei16_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg2ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg3ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg4ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg5ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg6ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg7ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg8ei32_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg2ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg3ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg4ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg5ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg6ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg7ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsoxseg8ei64_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))


RVOP(
    vadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vadd_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vadd_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsub_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrsub_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrsub_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vminu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vminu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmin_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmin_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmaxu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmaxu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmax_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmax_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vand_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vand_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vand_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vor_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vor_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vor_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vxor_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vxor_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vxor_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrgather_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrgather_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrgather_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vslideup_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vslideup_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrgatherei16_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vslidedown_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vslidedown_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vadc_vvm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vadc_vxm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vadc_vim,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmadc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmadc_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmadc_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsbc_vvm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsbc_vxm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsbc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsbc_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmerge_vvm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmerge_vxm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmerge_vim,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmv_v_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmv_v_x,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmv_v_i,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmseq_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmseq_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmseq_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsne_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsne_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsne_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsltu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsltu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmslt_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmslt_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsleu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsleu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsleu_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsle_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsle_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsle_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsgtu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsgtu_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsgt_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsgt_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsaddu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsaddu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsaddu_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsadd_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsadd_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssubu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssubu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssub_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsll_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsll_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsll_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsmul_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsmul_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsrl_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsrl_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsrl_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsra_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsra_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vsra_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssrl_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssrl_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssrl_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssra_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssra_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vssra_vi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnsrl_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnsrl_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnsrl_wi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnsra_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnsra_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnsra_wi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnclipu_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnclipu_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnclipu_wi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnclip_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnclip_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnclip_wi,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwredsumu_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwredsum_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))



RVOP(
    vredsum_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vredand_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vredor_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vredxor_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vredminu_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vredmin_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vredmaxu_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vredmax_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vaaddu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vaaddu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vaadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vaadd_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vasubu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vasubu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vasub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vasub_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vslide1up_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vslide1down_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vcompress_vm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmandn_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmand_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmor_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmxor_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmorn_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmnand_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmnor_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmxnor_mm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vdivu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vdivu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vdiv_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vdiv_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vremu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vremu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrem_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vrem_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmulhu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmulhu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmul_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmul_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmulhsu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmulhsu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmulh_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmulh_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmadd_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnmsub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnmsub_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmacc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmacc_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnmsac_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vnmsac_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwaddu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwaddu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwadd_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsubu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsubu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsub_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwaddu_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwaddu_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwadd_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwadd_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsubu_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsubu_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsub_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwsub_wx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmulu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmulu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmulsu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmulsu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmul_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmul_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmaccu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmaccu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmacc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmacc_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmaccus_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmaccsu_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vwmaccsu_vx,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmv_s_x,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmv_x_s,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vcpop_m,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfirst_m,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsbf_m,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsof_m,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmsif_m,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    viota_m,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vid_v,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))


RVOP(
    vfadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfadd_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfredusum_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsub_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfredosum_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmin_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmin_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfredmin_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmax_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmax_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfredmax_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsgnj_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsgnj_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsgnjn_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsgnjn_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsgnjx_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfsgnjx_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfslide1up_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfslide1down_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmerge_vfm,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmv_v_f,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfeq_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfeq_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfle_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfle_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmflt_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmflt_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfne_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfne_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfgt_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vmfge_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfdiv_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfdiv_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfrdiv_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmul_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmul_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfrsub_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmadd_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmadd_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmsub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmsub_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmsub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmsub_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmacc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmacc_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmacc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmacc_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmsac_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfmsac_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmsac_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfnmsac_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwadd_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwadd_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwredusum_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwsub_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwsub_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwredosum_vs,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwadd_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwadd_wf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwsub_wv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwsub_wf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwmul_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwmul_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwmacc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwmacc_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwnmacc_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwnmacc_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwmsac_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwmsac_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwnmsac_vv,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

RVOP(
    vfwnmsac_vf,
    { V_NOP; },
    GEN({
        assert; /* FIXME: Implement */
    }))

#endif
