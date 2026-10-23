/*
 * JIT Code Generator for RISC-V Instructions (Tier-1)
 *
 * This file contains native code generation handlers for RISC-V instructions,
 * supporting x86-64 and Arm64 host architectures.
 *
 * Architecture Overview:
 * - GEN(name, { body }): Defines a generator that emits host machine code
 *   equivalent to the RISC-V instruction 'name'.
 * - Register Allocation: Maps RISC-V registers (X[rd]) to host registers
 *   (vm_reg[0..2]) using a farthest-liveness eviction policy.
 * - Manual Maintenance: Handlers are manually optimized for host performance
 *   and are independent of the interpreter implementations in rv32_template.c.
 *
 * Key Registers:
 * - vm_reg[0..2]: Host registers allocated for VM register operations
 * - temp_reg: Scratch register for intermediate calculations
 * - parameter_reg[0]: Points to riscv_t structure
 *
 * Code Generation (emit_*) API:
 * ---------------------------------------------------------------------------
 * Function                | Description
 * ---------------------------------------------------------------------------
 * emit_alu32/64           | Emits arithmetic/logic (ADD, SUB, XOR, OR, AND).
 * emit_alu32_imm32/8      | Emits ALU operations with immediate operands.
 * emit_load/store         | Emits memory access with MMIO/System support.
 * emit_load_sext          | Emits sign-extending memory loads (LB, LH).
 * emit_cmp32/imm32        | Emits comparison logic for branches/SLT.
 * emit_jcc_offset         | Emits conditional jumps (using JCC_* identifiers).
 * emit_jmp                | Emits unconditional jumps to a target PC.
 * emit_exit               | Emits the epilogue to return from JIT execution.
 * ---------------------------------------------------------------------------
 *
 * Code Generation Macros:
 * Helper macros reduce duplication for common instruction patterns:
 * - GEN_BRANCH: Conditional branch instructions (beq, bne, blt, etc.)
 * - GEN_CBRANCH: Compressed branch instructions (cbeqz, cbnez)
 * - GEN_ALU_IMM: ALU with immediate operand (addi, xori, ori, andi)
 * - GEN_ALU_REG: ALU with register operands (add, sub, xor, or, and)
 * - GEN_SHIFT_IMM: Shift by immediate (slli, srli, srai)
 * - GEN_SHIFT_REG: Shift by register (sll, srl, sra)
 * - GEN_SLT_IMM: Set-less-than immediate (slti, sltiu)
 * - GEN_SLT_REG: Set-less-than register (slt, sltu)
 * - GEN_LOAD: Memory load with MMIO support (lb, lh, lw, lbu, lhu)
 * - GEN_STORE: Memory store with MMIO support (sb, sh, sw)
 *
 * Host Abstraction Layer:
 * The emit_* API abstracts architecture differences by using x86-64 bit
 * patterns (e.g., JCC_JE=0x84, ALU_OP_ADD=0x01) as symbolic identifiers across
 * all hosts. The backend in src/jit.c maps these to native Arm64 or x86
 * instructions.
 *
 * Memory Access Patterns:
 * Handlers use IIF(RV32_HAS(SYSTEM_MMIO)) to switch between direct RAM access
 * (User mode) and the JIT MMU handler path (System mode).
 *
 * See rv32_template.c for the corresponding interpreter implementations.
 */

/* Branch epilogue helper - emits fall-through and taken paths.
 * Used by both regular (4-byte) and compressed (2-byte) branch instructions.
 */
#define EMIT_BRANCH_EPILOGUE(inst_size)                                   \
    do {                                                                  \
        if (JIT_LINK_ALL_EDGES || ir->branch_untaken)                     \
            emit_jmp(state, ir->pc + (inst_size), BLOCK_SATP(cur_block)); \
        emit_load_imm(state, temp_reg, ir->pc + (inst_size));             \
        emit_store(state, S32, temp_reg, parameter_reg[0],                \
                   offsetof(riscv_t, PC));                                \
        emit_exit(state);                                                 \
        emit_jump_target_offset(state, JUMP_LOC_0, state->offset);        \
        if (JIT_LINK_ALL_EDGES || ir->branch_taken)                       \
            emit_jmp(state, ir->pc + ir->imm, BLOCK_SATP(cur_block));     \
        emit_load_imm(state, temp_reg, ir->pc + ir->imm);                 \
        emit_store(state, S32, temp_reg, parameter_reg[0],                \
                   offsetof(riscv_t, PC));                                \
        emit_exit(state);                                                 \
    } while (0)

/* Branch instruction handler macro - all branch instructions follow
 * the same pattern, differing only in the condition code. A comparison
 * with x0 on either side tests the other operand against zero.
 */
#define GEN_BRANCH(inst, cond)                            \
    GEN(inst, {                                           \
        uint8_t lhs = ir->rs1;                            \
        uint8_t rhs = ir->rs2;                            \
        int jcc = cond;                                   \
        branch_zero_rhs(&lhs, &rhs, &jcc);                \
        if (rhs == rv_reg_zero) {                         \
            vm_reg[0] = ra_load(state, lhs);              \
            store_back(state);                            \
            emit_cmp_imm32(state, vm_reg[0], 0);          \
        } else {                                          \
            ra_load2(state, lhs, rhs);                    \
            store_back(state);                            \
            emit_cmp32(state, vm_reg[1], vm_reg[0]);      \
        }                                                 \
        uint32_t jump_loc_0 = state->offset;              \
        emit_jcc_offset(state, jcc);                      \
        EMIT_BRANCH_EPILOGUE(4); /* 4-byte instruction */ \
    })

/* Compressed branch instruction handler macro - compares rs1 with zero
 * and uses pc+2 instead of pc+4 for compressed instruction size.
 */
#define GEN_CBRANCH(inst, cond)                           \
    GEN(inst, {                                           \
        vm_reg[0] = ra_load(state, ir->rs1);              \
        store_back(state);                                \
        emit_cmp_imm32(state, vm_reg[0], 0);              \
        uint32_t jump_loc_0 = state->offset;              \
        emit_jcc_offset(state, cond);                     \
        EMIT_BRANCH_EPILOGUE(2); /* 2-byte instruction */ \
    })

/* ALU immediate instruction handler macro. Identity operations are resolved
 * while translating, retaining a 32-bit move to normalize the result.
 */
#define GEN_ALU_IMM(inst, op)                                                 \
    GEN(inst, {                                                               \
        if (ir->rd == rv_reg_zero)                                            \
            return;                                                           \
        vm_reg[0] = ra_load(state, ir->rs1);                                  \
        vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);            \
        if ((ir->imm == 0 &&                                                  \
             (op == ALU_ADD || op == ALU_OR || op == ALU_XOR)) ||             \
            (ir->imm == -1 && op == ALU_AND)) {                               \
            emit_mov32(state, vm_reg[0], vm_reg[1]);                          \
        } else {                                                              \
            if (vm_reg[0] != vm_reg[1])                                       \
                emit_mov(state, vm_reg[0], vm_reg[1]);                        \
            emit_alu32_imm32(state, ALU_GRP1_OPCODE, op, vm_reg[1], ir->imm); \
        }                                                                     \
    })

/* RV32 shift amount mask - only lower 5 bits used */
#define RV32_SHIFT_MASK 0x1f

/* Shift immediate instruction handler macro. A zero shift is a 32-bit move
 * and a discarded result has no architectural side effect.
 */
#define GEN_SHIFT_IMM(inst, op)                                     \
    GEN(inst, {                                                     \
        if (ir->rd == rv_reg_zero)                                  \
            return;                                                 \
        vm_reg[0] = ra_load(state, ir->rs1);                        \
        vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);  \
        if (ir->imm & RV32_SHIFT_MASK) {                            \
            if (vm_reg[0] != vm_reg[1])                             \
                emit_mov(state, vm_reg[0], vm_reg[1]);              \
            emit_alu32_imm8(state, SHIFT_IMM_OPCODE, op, vm_reg[1], \
                            ir->imm & RV32_SHIFT_MASK);             \
        } else {                                                    \
            emit_mov32(state, vm_reg[0], vm_reg[1]);                \
        }                                                           \
    })

/* ALU opcodes for register-to-register operations (x86-64 encoding).
 * On Arm64, emit_alu32() maps these to equivalent instructions.
 */
#define ALU_OP_ADD 0x01
#define ALU_OP_SUB 0x29
#define ALU_OP_XOR 0x31
#define ALU_OP_OR 0x09
#define ALU_OP_AND 0x21

/* ALU register instruction handler macro */
#define GEN_ALU_REG(inst, op)                                                  \
    GEN(inst, {                                                                \
        ra_load2(state, ir->rs1, ir->rs2);                                     \
        vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]); \
        if (vm_reg[2] != vm_reg[1] || vm_reg[0] == vm_reg[1]) {                \
            if (vm_reg[0] != vm_reg[2])                                        \
                emit_mov(state, vm_reg[0], vm_reg[2]);                         \
            emit_alu32(state, op, vm_reg[1], vm_reg[2]);                       \
        } else if (op != ALU_OP_SUB) {                                         \
            /* Commutative operations can update the second operand. */        \
            emit_alu32(state, op, vm_reg[0], vm_reg[2]);                       \
        } else {                                                               \
            /* Preserve rhs before overwriting it with lhs for subtraction. */ \
            emit_mov(state, vm_reg[1], temp_reg);                              \
            emit_mov(state, vm_reg[0], vm_reg[2]);                             \
            emit_alu32(state, op, temp_reg, vm_reg[2]);                        \
        }                                                                      \
    })

/* Shift register instruction handler macro */
#define GEN_SHIFT_REG(inst, op)                                                \
    GEN(inst, {                                                                \
        ra_load2(state, ir->rs1, ir->rs2);                                     \
        vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]); \
        emit_shift32(state, op, vm_reg[0], vm_reg[1], vm_reg[2]);              \
    })

/* Set-less-than immediate instruction handler macro (slti/sltiu) */
#define GEN_SLT_IMM(inst, cond)                                    \
    GEN(inst, {                                                    \
        vm_reg[0] = ra_load(state, ir->rs1);                       \
        vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]); \
        emit_cmp_imm32(state, vm_reg[0], ir->imm);                 \
        emit_set_less_than(state, cond, vm_reg[1]);                \
    })

/* Set-less-than register instruction handler macro (slt/sltu) */
#define GEN_SLT_REG(inst, cond)                                                \
    GEN(inst, {                                                                \
        ra_load2(state, ir->rs1, ir->rs2);                                     \
        vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]); \
        emit_cmp32(state, vm_reg[1], vm_reg[0]);                               \
        emit_set_less_than(state, cond, vm_reg[2]);                            \
    })

/* Load instruction handler macro - handles MMIO path when SYSTEM_MMIO enabled.
 * Parameters:
 *   inst: instruction name (lb, lh, lw, lbu, lhu)
 *   insn_type: rv_insn_* constant for MMIO handler
 *   size: memory access size (S8, S16, S32)
 *   load_fn: emit_load or emit_load_sext
 *   flags: JIT_MISALIGN_COMPRESSED for compressed forms, otherwise 0
 * Compressed forms reuse the word handler; the decoder sets rs1 to sp for
 * C.LWSP, and insn_type selects the access size for the MMU handler.
 */
#define GEN_LOAD(inst, insn_type, size, load_fn, flags)                        \
    GEN(inst, {                                                                \
        memory_t *m = PRIV(rv)->mem;                                           \
        vm_reg[0] = ra_load(state, ir->rs1);                                   \
        emit_misalign_guard(state, rv, ir->rs1, ir->imm, size, flags, ir->pc); \
        IIF(RV32_HAS(SYSTEM_MMIO))(                                            \
            {                                                                  \
                emit_load_imm_sext(state, temp_reg, ir->imm);                  \
                emit_alu32(state, ALU_OP_ADD, vm_reg[0], temp_reg);            \
                /* Spill any dirty allocator-managed registers and reset the   \
                 * allocator so the fast and slow paths share the same         \
                 * post-state.  The slow path's later store_back becomes a     \
                 * no-op and its map_vm_reg returns the same host register as  \
                 * the one we bind to rd here.                                 \
                 */                                                            \
                store_back(state);                                             \
                reset_reg();                                                   \
                vm_reg[1] = map_vm_reg(state, ir->rd);                         \
                                                                               \
                /* Inline dTLB fast path; writes the loaded value into         \
                 * vm_reg[1] on hit and returns the offset of an               \
                 * unconditional branch to patch to the merge label.  Gating   \
                 * branches funnel to state->offset right after this call,     \
                 * which is exactly where the slow path begins.                \
                 */                                                            \
                uint32_t fp_end_loc =                                          \
                    emit_jit_mmu_fastpath(state, ir, insn_type, vm_reg[1], m); \
                                                                               \
                /* vaddr already in temp_reg from the address compute; the     \
                 * fast-path probe preserves it.  Pass it (and type/pc) as     \
                 * call args instead of writing to rv->jit_mmu.{vaddr,type,    \
                 * pc} first; the C handler reads them directly.               \
                 */                                                            \
                emit_jit_mmu_handler(state, ir->rd, temp_reg, insn_type,       \
                                     ir->pc, jit_instruction_size(ir->opcode), \
                                     unretired_after(rv, ir->pc));             \
                reset_reg();                                                   \
                                                                               \
                /* Check if trap occurred - skip load if trapped */            \
                emit_load(state, S8, parameter_reg[0], temp_reg,               \
                          offsetof(riscv_t, jit_mmu.abort));                   \
                emit_cmp_imm32(state, temp_reg, 0);                            \
                uint32_t jump_trap = state->offset;                            \
                emit_jcc_offset(state, JCC_JNE);                               \
                                                                               \
                /* If MMIO, load from X[rd]; otherwise load from memory */     \
                emit_load(state, S8, parameter_reg[0], temp_reg,               \
                          offsetof(riscv_t, jit_mmu.is_mmio));                 \
                emit_cmp_imm32(state, temp_reg, 0);                            \
                vm_reg[1] = map_vm_reg(state, ir->rd);                         \
                uint32_t jump_loc_0 = state->offset;                           \
                emit_jcc_offset(state, JCC_JE);                                \
                                                                               \
                emit_load(state, S32, parameter_reg[0], vm_reg[1],             \
                          offsetof(riscv_t, X) + 4 * ir->rd);                  \
                uint32_t jump_loc_1 = state->offset;                           \
                emit_jcc_offset(state, JCC_JMP);                               \
                                                                               \
                emit_jump_target_offset(state, JUMP_LOC_0, state->offset);     \
                emit_load(state, S32, parameter_reg[0], temp_reg,              \
                          offsetof(riscv_t, jit_mmu.paddr));                   \
                emit_load_imm_sext(state, vm_reg[1], (intptr_t) m->mem_base);  \
                emit_alu64(state, ALU_OP_ADD, temp_reg, vm_reg[1]);            \
                load_fn(state, size, vm_reg[1], vm_reg[1], 0);                 \
                emit_jump_target_offset(state, JUMP_LOC_1, state->offset);     \
                /* Jump over trap exit to continue normally */                 \
                uint32_t jump_normal = state->offset;                          \
                emit_jcc_offset(state, JCC_JMP);                               \
                /* Trap exit point - exit JIT block for trap handling */       \
                emit_jump_target_offset(state, JUMP_TRAP, state->offset);      \
                emit_exit(state);                                              \
                /* Normal continuation point: fast path merges here too */     \
                emit_jump_target_offset(state, JUMP_NORMAL, state->offset);    \
                if (fp_end_loc)                                                \
                    emit_jump_target_offset(state, fp_end_loc, state->offset); \
                /* The fast path loads into x0's host register as well. */     \
                if (ir->rd == rv_reg_zero)                                     \
                    emit_load_imm(state, vm_reg[1], 0);                        \
            },                                                                 \
            {                                                                  \
                vm_reg[1] = map_vm_reg(state, ir->rd);                         \
                emit_guest_load(state, m, size, vm_reg[0], vm_reg[1], ir->imm, \
                                load_fn == emit_load_sext);                    \
            })                                                                 \
    })

/* Store instruction handler macro - handles MMIO path when SYSTEM_MMIO enabled.
 * Parameters:
 *   inst: instruction name (sb, sh, sw)
 *   insn_type: rv_insn_* constant for MMIO handler
 *   size: memory access size (S8, S16, S32)
 *   flags: JIT_MISALIGN_COMPRESSED for compressed forms, otherwise 0
 */
#define GEN_STORE(inst, insn_type, size, flags)                                \
    GEN(inst, {                                                                \
        memory_t *m = PRIV(rv)->mem;                                           \
        vm_reg[0] = ra_load(state, ir->rs1);                                   \
        emit_misalign_guard(state, rv, ir->rs1, ir->imm, size,                 \
                            JIT_MISALIGN_STORE | (flags), ir->pc);             \
        IIF(RV32_HAS(SYSTEM_MMIO))(                                            \
            {                                                                  \
                emit_load_imm_sext(state, temp_reg, ir->imm);                  \
                emit_alu32(state, ALU_OP_ADD, vm_reg[0], temp_reg);            \
                /* Spill dirty allocator-managed registers and reset.  This    \
                 * guarantees rs2 is in rv->X[rs2] in memory for the fast      \
                 * path scratch read, and lets the slow path's later           \
                 * store_back be a no-op.                                      \
                 */                                                            \
                store_back(state);                                             \
                reset_reg();                                                   \
                                                                               \
                /* Inline dTLB fast path; on hit it loads rs2 from memory      \
                 * and writes to RAM, then branches past the slow path.        \
                 */                                                            \
                uint32_t fp_end_loc =                                          \
                    emit_jit_mmu_fastpath(state, ir, insn_type, -1, m);        \
                                                                               \
                /* Same calling convention as GEN_LOAD: vaddr lives in         \
                 * temp_reg after the fast-path probe.  Pass it (and type/     \
                 * pc) as call args; the C handler reads them directly         \
                 * instead of from rv->jit_mmu staging fields.                 \
                 */                                                            \
                emit_jit_mmu_handler(state, ir->rs2, temp_reg, insn_type,      \
                                     ir->pc, jit_instruction_size(ir->opcode), \
                                     unretired_after(rv, ir->pc));             \
                reset_reg();                                                   \
                                                                               \
                /* Check if trap occurred - skip store if trapped */           \
                emit_load(state, S8, parameter_reg[0], temp_reg,               \
                          offsetof(riscv_t, jit_mmu.abort));                   \
                emit_cmp_imm32(state, temp_reg, 0);                            \
                uint32_t jump_trap = state->offset;                            \
                emit_jcc_offset(state, JCC_JNE);                               \
                                                                               \
                /* If MMIO, skip store (handled by MMIO handler) */            \
                emit_load(state, S8, parameter_reg[0], temp_reg,               \
                          offsetof(riscv_t, jit_mmu.is_mmio));                 \
                emit_cmp_imm32(state, temp_reg, 1);                            \
                uint32_t jump_loc_0 = state->offset;                           \
                emit_jcc_offset(state, JCC_JE);                                \
                                                                               \
                /* Load rs2 value BEFORE computing address to avoid register   \
                 * allocation conflicts. ra_load could evict any allocated     \
                 * register, so we load rs2 first, then use temp_reg for the   \
                 * address (temp_reg is reserved and won't be evicted).        \
                 */                                                            \
                vm_reg[1] = ra_load(state, ir->rs2);                           \
                emit_load(state, S32, parameter_reg[0], temp_reg,              \
                          offsetof(riscv_t, jit_mmu.paddr));                   \
                vm_reg[0] = map_vm_reg(state, rv_reg_zero);                    \
                emit_load_imm_sext(state, vm_reg[0], (intptr_t) m->mem_base);  \
                emit_alu64(state, ALU_OP_ADD, vm_reg[0], temp_reg);            \
                emit_store(state, size, vm_reg[1], temp_reg, 0);               \
                emit_jump_target_offset(state, JUMP_LOC_0, state->offset);     \
                /* Jump over trap exit to continue normally */                 \
                uint32_t jump_normal = state->offset;                          \
                emit_jcc_offset(state, JCC_JMP);                               \
                /* Trap exit point - exit JIT block for trap handling */       \
                emit_jump_target_offset(state, JUMP_TRAP, state->offset);      \
                emit_exit(state);                                              \
                /* Normal continuation point: fast path merges here too */     \
                emit_jump_target_offset(state, JUMP_NORMAL, state->offset);    \
                if (fp_end_loc)                                                \
                    emit_jump_target_offset(state, fp_end_loc, state->offset); \
                reset_reg();                                                   \
            },                                                                 \
            { ra_store_guest(state, m, size, ir->rs1, ir->rs2, ir->imm); })    \
    })

GEN(nop, {})
GEN(lui, {
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
})
GEN(auipc, {
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->pc + ir->imm);
})
GEN(jal, {
    if (ir->rd) {
        vm_reg[0] = map_vm_reg(state, ir->rd);
        emit_load_imm(state, vm_reg[0], ir->pc + 4);
    }
    store_back(state);
    emit_jmp(state, ir->pc + ir->imm, BLOCK_SATP(cur_block));
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(jalr, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_mov(state, vm_reg[0], temp_reg);
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_ADD, temp_reg, ir->imm);
    /* RISC-V spec: target address LSB is always cleared */
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_AND, temp_reg, ~1U);
    if (ir->rd) {
        vm_reg[1] = map_vm_reg(state, ir->rd);
        emit_load_imm(state, vm_reg[1], ir->pc + 4);
    }
    store_back(state);
    emit_indirect_jump(state, rv, ir);
})
/* RV32I Branch Instructions */
GEN_BRANCH(beq, JCC_JE)
GEN_BRANCH(bne, JCC_JNE)
GEN_BRANCH(blt, JCC_JL)
GEN_BRANCH(bge, JCC_JGE)
GEN_BRANCH(bltu, JCC_JB)
GEN_BRANCH(bgeu, JCC_JAE)
/* RV32I Load Instructions */
GEN_LOAD(lb, rv_insn_lb, S8, emit_load_sext, 0)
GEN_LOAD(lh, rv_insn_lh, S16, emit_load_sext, 0)
GEN_LOAD(lw, rv_insn_lw, S32, emit_load, 0)
GEN_LOAD(lbu, rv_insn_lbu, S8, emit_load, 0)
GEN_LOAD(lhu, rv_insn_lhu, S16, emit_load, 0)
/* RV32I Store Instructions */
GEN_STORE(sb, rv_insn_sb, S8, 0)
GEN_STORE(sh, rv_insn_sh, S16, 0)
GEN_STORE(sw, rv_insn_sw, S32, 0)
/* RV32I ALU Immediate Instructions */
GEN_ALU_IMM(addi, ALU_ADD)
GEN_SLT_IMM(slti, JCC_JL)
GEN_SLT_IMM(sltiu, JCC_JB)
GEN_ALU_IMM(xori, ALU_XOR)
GEN_ALU_IMM(ori, ALU_OR)
GEN_ALU_IMM(andi, ALU_AND)
/* RV32I Shift Immediate Instructions */
GEN_SHIFT_IMM(slli, SHIFT_SHL)
GEN_SHIFT_IMM(srli, SHIFT_SHR)
GEN_SHIFT_IMM(srai, SHIFT_SAR)
/* RV32I ALU Register Instructions */
GEN_ALU_REG(add, ALU_OP_ADD)
GEN_ALU_REG(sub, ALU_OP_SUB)
/* RV32I Shift Register Instructions */
GEN_SHIFT_REG(sll, SHIFT_SHL)
GEN_SLT_REG(slt, JCC_JL)
GEN_SLT_REG(sltu, JCC_JB)
GEN_ALU_REG(xor, ALU_OP_XOR)
GEN_SHIFT_REG(srl, SHIFT_SHR)
GEN_SHIFT_REG(sra, SHIFT_SAR)
GEN_ALU_REG(or, ALU_OP_OR)
GEN_ALU_REG(and, ALU_OP_AND)
GEN_UNSUPPORTED(fence)
GEN(ecall, {
    store_back(state);
    emit_load_imm(state, temp_reg, ir->pc);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_call(state, (intptr_t) rv->io.on_ecall);
    emit_exit(state);
})
GEN(ebreak, {
    store_back(state);
    emit_load_imm(state, temp_reg, ir->pc);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_call(state, (intptr_t) rv->io.on_ebreak);
    emit_exit(state);
})
GEN_UNSUPPORTED(wfi)
GEN_UNSUPPORTED(uret)
#if RV32_HAS(SYSTEM)
GEN_UNSUPPORTED(sret)
#endif
GEN_UNSUPPORTED(hret)
GEN_UNSUPPORTED(mret)
GEN_UNSUPPORTED(sfencevma)
#if RV32_HAS(Zifencei) /* RV32 Zifencei Standard Extension */
GEN_UNSUPPORTED(fencei)
#endif
#if RV32_HAS(Zicsr) /* RV32 Zicsr Standard Extension */
GEN_UNSUPPORTED(csrrw)
GEN_UNSUPPORTED(csrrs)
GEN_UNSUPPORTED(csrrc)
GEN_UNSUPPORTED(csrrwi)
GEN_UNSUPPORTED(csrrsi)
GEN_UNSUPPORTED(csrrci)
#endif
#if RV32_HAS(EXT_M)
GEN(mul, {
    if (ir->rd == rv_reg_zero)
        return;
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mul32(state, vm_reg[0], vm_reg[1], vm_reg[2]);
})
/* High multiply: the full product of the operands, each extended as signed
 * or unsigned, shifted down by 32.
 */
#define GEN_MULH(inst, sign1, sign2)                                        \
    GEN(inst, {                                                             \
        ra_load2_muldiv(state, ir->rs1, ir->rs2, ir->rd, sign1, sign2);     \
        muldivmod(state, 0x2f, temp_reg, vm_reg[2], 0);                     \
        emit_alu64_imm8(state, SHIFT_IMM_OPCODE, SHIFT_SHR, vm_reg[2], 32); \
    })
GEN_MULH(mulh, true, true)
GEN_MULH(mulhsu, true, false)
GEN_MULH(mulhu, false, false)

/* Division (0x38) and remainder (0x98), signed or unsigned. */
#define GEN_DIVREM(inst, opcode, sign)                                \
    GEN(inst, {                                                       \
        ra_load2_muldiv(state, ir->rs1, ir->rs2, ir->rd, sign, sign); \
        muldivmod(state, opcode, temp_reg, vm_reg[2], sign);          \
    })
GEN_DIVREM(div, 0x38, true)
GEN_DIVREM(divu, 0x38, false)
GEN_DIVREM(rem, 0x98, true)
GEN_DIVREM(remu, 0x98, false)
#endif
#if RV32_HAS(EXT_A)
GEN_UNSUPPORTED(lrw)
GEN_UNSUPPORTED(scw)
GEN_UNSUPPORTED(amoswapw)
GEN_UNSUPPORTED(amoaddw)
GEN_UNSUPPORTED(amoxorw)
GEN_UNSUPPORTED(amoandw)
GEN_UNSUPPORTED(amoorw)
GEN_UNSUPPORTED(amominw)
GEN_UNSUPPORTED(amomaxw)
GEN_UNSUPPORTED(amominuw)
GEN_UNSUPPORTED(amomaxuw)
#endif
#if RV32_HAS(EXT_F)
GEN_UNSUPPORTED(flw)
GEN_UNSUPPORTED(fsw)
GEN_UNSUPPORTED(fmadds)
GEN_UNSUPPORTED(fmsubs)
GEN_UNSUPPORTED(fnmsubs)
GEN_UNSUPPORTED(fnmadds)
GEN_UNSUPPORTED(fadds)
GEN_UNSUPPORTED(fsubs)
GEN_UNSUPPORTED(fmuls)
GEN_UNSUPPORTED(fdivs)
GEN_UNSUPPORTED(fsqrts)
GEN_UNSUPPORTED(fsgnjs)
GEN_UNSUPPORTED(fsgnjns)
GEN_UNSUPPORTED(fsgnjxs)
GEN_UNSUPPORTED(fmins)
GEN_UNSUPPORTED(fmaxs)
GEN_UNSUPPORTED(fcvtws)
GEN_UNSUPPORTED(fcvtwus)
GEN_UNSUPPORTED(fmvxw)
GEN_UNSUPPORTED(feqs)
GEN_UNSUPPORTED(flts)
GEN_UNSUPPORTED(fles)
GEN_UNSUPPORTED(fclasss)
GEN_UNSUPPORTED(fcvtsw)
GEN_UNSUPPORTED(fcvtswu)
GEN_UNSUPPORTED(fmvwx)
#endif
#if RV32_HAS(EXT_C)
GEN(caddi4spn, {
    vm_reg[0] = ra_load(state, rv_reg_sp);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_ADD, vm_reg[1],
                     (uint16_t) ir->imm);
})
GEN_LOAD(clw, rv_insn_lw, S32, emit_load, JIT_MISALIGN_COMPRESSED)
GEN_STORE(csw, rv_insn_sw, S32, JIT_MISALIGN_COMPRESSED)
GEN(cnop, {})
GEN(caddi, {
    vm_reg[0] = ra_load(state, ir->rd);
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_ADD, vm_reg[0],
                     (int16_t) ir->imm);
})
GEN(cjal, {
    vm_reg[0] = map_vm_reg(state, rv_reg_ra);
    emit_load_imm(state, vm_reg[0], ir->pc + 2);
    store_back(state);
    emit_jmp(state, ir->pc + ir->imm, BLOCK_SATP(cur_block));
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(cli, {
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
})
GEN(caddi16sp, {
    vm_reg[0] = ra_load(state, ir->rd);
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_ADD, vm_reg[0], ir->imm);
})
GEN(clui, {
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
})
GEN(csrli, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm8(state, SHIFT_IMM_OPCODE, SHIFT_SHR, vm_reg[0], ir->shamt);
})
GEN(csrai, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm8(state, SHIFT_IMM_OPCODE, SHIFT_SAR, vm_reg[0], ir->shamt);
})
GEN(candi, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_AND, vm_reg[0], ir->imm);
})
GEN(csub, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x29, temp_reg, vm_reg[2]);
})
GEN(cxor, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x31, temp_reg, vm_reg[2]);
})
GEN(cor, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x09, temp_reg, vm_reg[2]);
})
GEN(cand, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x21, temp_reg, vm_reg[2]);
})
GEN(cj, {
    store_back(state);
    emit_jmp(state, ir->pc + ir->imm, BLOCK_SATP(cur_block));
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
/* RV32C Compressed Branch Instructions */
GEN_CBRANCH(cbeqz, JCC_JE)
GEN_CBRANCH(cbnez, JCC_JNE)
GEN(cslli, {
    vm_reg[0] = ra_load(state, ir->rd);
    emit_alu32_imm8(state, SHIFT_IMM_OPCODE, SHIFT_SHL, vm_reg[0],
                    (uint8_t) ir->imm);
})
GEN_LOAD(clwsp, rv_insn_lw, S32, emit_load, JIT_MISALIGN_COMPRESSED)
GEN(cjr, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_mov(state, vm_reg[0], temp_reg);
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_AND, temp_reg, ~1U);
    store_back(state);
    emit_indirect_jump(state, rv, ir);
})
GEN(cmv, {
    vm_reg[0] = ra_load(state, ir->rs2);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    } else {
        set_dirty(vm_reg[1], true);
    }
})
GEN(cebreak, {
    store_back(state);
    emit_load_imm(state, temp_reg, ir->pc);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_call(state, (intptr_t) rv->io.on_ebreak);
    emit_exit(state);
})
GEN(cjalr, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_mov(state, vm_reg[0], temp_reg);
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_AND, temp_reg, ~1U);
    vm_reg[1] = map_vm_reg(state, rv_reg_ra);
    emit_load_imm(state, vm_reg[1], ir->pc + 2);
    store_back(state);
    emit_indirect_jump(state, rv, ir);
})
GEN(cadd, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x01, temp_reg, vm_reg[2]);
})
GEN_STORE(cswsp, rv_insn_sw, S32, JIT_MISALIGN_COMPRESSED)
#endif
#if RV32_HAS(EXT_C) && RV32_HAS(EXT_F)
GEN_UNSUPPORTED(cflwsp)
GEN_UNSUPPORTED(cfswsp)
GEN_UNSUPPORTED(cflw)
GEN_UNSUPPORTED(cfsw)
#endif
#if RV32_HAS(Zba)
#define GEN_SHIFT_ADD(inst, shift) \
    GEN(inst, { emit_guest_shift_add(state, ir->rs1, ir->rs2, ir->rd, shift); })
GEN_SHIFT_ADD(sh1add, 1)
GEN_SHIFT_ADD(sh2add, 2)
GEN_SHIFT_ADD(sh3add, 3)
#undef GEN_SHIFT_ADD
#endif
#if RV32_HAS(Zbb)
GEN_UNSUPPORTED(andn)
GEN_UNSUPPORTED(orn)
GEN_UNSUPPORTED(xnor)
GEN_UNSUPPORTED(clz)
GEN_UNSUPPORTED(ctz)
GEN_UNSUPPORTED(cpop)
GEN_UNSUPPORTED(max)
GEN_UNSUPPORTED(min)
GEN_UNSUPPORTED(maxu)
GEN_UNSUPPORTED(minu)
GEN_UNSUPPORTED(sextb)
GEN_UNSUPPORTED(sexth)
GEN_UNSUPPORTED(zexth)
GEN_UNSUPPORTED(rol)
GEN_UNSUPPORTED(ror)
GEN_UNSUPPORTED(rori)
GEN_UNSUPPORTED(orcb)
GEN_UNSUPPORTED(rev8)
#endif
#if RV32_HAS(Zbc)
GEN_UNSUPPORTED(clmul)
GEN_UNSUPPORTED(clmulh)
GEN_UNSUPPORTED(clmulr)
#endif
#if RV32_HAS(Zbs)
GEN_UNSUPPORTED(bclr)
GEN_UNSUPPORTED(bclri)
GEN_UNSUPPORTED(bext)
GEN_UNSUPPORTED(bexti)
GEN_UNSUPPORTED(binv)
GEN_UNSUPPORTED(binvi)
GEN_UNSUPPORTED(bset)
GEN_UNSUPPORTED(bseti)
#endif

#if RV32_HAS(EXT_V)
/* JIT stubs for every V opcode. We deliberately reuse rv32_v_constopt.c
 * for the opcode list instead of including rv32_v_template.c here: the
 * interpreter template defines static-inline FP helpers (FMASK_SIGN /
 * is_nan / softfloat shims) that depend on rv32_template.c's earlier F
 * context, which the JIT compilation unit does not pull in. Including
 * the template under JIT therefore fails to build whenever EXT_V is
 * enabled. The constopt sibling, in contrast, is a hand-maintained list
 * of CONSTOPT(name, {}) lines with no helper dependencies; redefining
 * the macro converts the list to GEN handlers without touching the rest
 * of the file.
 *
 * Vector instructions are marked translatable=0 in src/decode.h, so the
 * JIT never reaches these handlers in normal flow. The assert(NULL)
 * stub catches accidental flips of that flag without quietly emitting
 * empty (wrong) JIT code for vector ops.
 */
#define CONSTOPT(inst, body) GEN_UNSUPPORTED(inst)
#include "rv32_v_constopt.c"
#undef CONSTOPT
#endif
