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
#define EMIT_BRANCH_EPILOGUE(inst_size)                            \
    do {                                                           \
        if (ir->branch_untaken) {                                  \
            emit_jmp(state, ir->pc + (inst_size), rv->csr_satp);   \
        }                                                          \
        emit_load_imm(state, temp_reg, ir->pc + (inst_size));      \
        emit_store(state, S32, temp_reg, parameter_reg[0],         \
                   offsetof(riscv_t, PC));                         \
        emit_exit(state);                                          \
        emit_jump_target_offset(state, JUMP_LOC_0, state->offset); \
        if (ir->branch_taken) {                                    \
            emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);       \
        }                                                          \
        emit_load_imm(state, temp_reg, ir->pc + ir->imm);          \
        emit_store(state, S32, temp_reg, parameter_reg[0],         \
                   offsetof(riscv_t, PC));                         \
        emit_exit(state);                                          \
    } while (0)

/* Branch instruction handler macro - all branch instructions follow
 * the same pattern, differing only in the condition code.
 */
#define GEN_BRANCH(inst, cond)                            \
    GEN(inst, {                                           \
        ra_load2(state, ir->rs1, ir->rs2);                \
        emit_cmp32(state, vm_reg[1], vm_reg[0]);          \
        store_back(state);                                \
        uint32_t jump_loc_0 = state->offset;              \
        emit_jcc_offset(state, cond);                     \
        EMIT_BRANCH_EPILOGUE(4); /* 4-byte instruction */ \
    })

/* Compressed branch instruction handler macro - compares rs1 with zero
 * and uses pc+2 instead of pc+4 for compressed instruction size.
 */
#define GEN_CBRANCH(inst, cond)                           \
    GEN(inst, {                                           \
        vm_reg[0] = ra_load(state, ir->rs1);              \
        emit_cmp_imm32(state, vm_reg[0], 0);              \
        store_back(state);                                \
        uint32_t jump_loc_0 = state->offset;              \
        emit_jcc_offset(state, cond);                     \
        EMIT_BRANCH_EPILOGUE(2); /* 2-byte instruction */ \
    })

/* Group 1 ALU opcode for immediate operand (x86-64 encoding).
 * On Arm64, this opcode is ignored; ALU_* selectors determine the operation.
 */
#define ALU_GRP1_OPCODE 0x81

/* ALU operation selectors for group 1 operations.
 * On x86-64: ModR/M reg field values. On Arm64: switch case selectors
 * in emit_alu32_imm32() that map to native instructions.
 */
#define ALU_ADD 0
#define ALU_OR 1
#define ALU_AND 4
#define ALU_XOR 6

/* ALU immediate instruction handler macro */
#define GEN_ALU_IMM(inst, op)                                             \
    GEN(inst, {                                                           \
        vm_reg[0] = ra_load(state, ir->rs1);                              \
        vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);        \
        if (vm_reg[0] != vm_reg[1]) {                                     \
            emit_mov(state, vm_reg[0], vm_reg[1]);                        \
        }                                                                 \
        emit_alu32_imm32(state, ALU_GRP1_OPCODE, op, vm_reg[1], ir->imm); \
    })

/* Shift operation identifiers.
 * Values match x86-64 ModR/M reg field; used on both architectures.
 */
#define SHIFT_SHL 4
#define SHIFT_SHR 5
#define SHIFT_SAR 7

/* Shift opcodes (x86-64 encoding).
 * On Arm64, emit_alu32_imm8() and emit_alu32() map SHIFT_* values to native
 * instructions.
 */
#define SHIFT_IMM_OPCODE 0xc1 /* Shift by immediate */
#define SHIFT_REG_OPCODE 0xd3 /* Shift by register */

/* RV32 shift amount mask - only lower 5 bits used */
#define RV32_SHIFT_MASK 0x1f

/* Shift immediate instruction handler macro */
#define GEN_SHIFT_IMM(inst, op)                                    \
    GEN(inst, {                                                    \
        vm_reg[0] = ra_load(state, ir->rs1);                       \
        vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]); \
        if (vm_reg[0] != vm_reg[1]) {                              \
            emit_mov(state, vm_reg[0], vm_reg[1]);                 \
        }                                                          \
        emit_alu32_imm8(state, SHIFT_IMM_OPCODE, op, vm_reg[1],    \
                        ir->imm & RV32_SHIFT_MASK);                \
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
        emit_mov(state, vm_reg[1], temp_reg);                                  \
        emit_mov(state, vm_reg[0], vm_reg[2]);                                 \
        emit_alu32(state, op, temp_reg, vm_reg[2]);                            \
    })

/* Shift register instruction handler macro */
#define GEN_SHIFT_REG(inst, op)                                                \
    GEN(inst, {                                                                \
        ra_load2(state, ir->rs1, ir->rs2);                                     \
        vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]); \
        emit_mov(state, vm_reg[1], temp_reg);                                  \
        emit_mov(state, vm_reg[0], vm_reg[2]);                                 \
        emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_AND, temp_reg,            \
                         RV32_SHIFT_MASK);                                     \
        emit_alu32(state, SHIFT_REG_OPCODE, op, vm_reg[2]);                    \
    })

/* Set-less-than immediate instruction handler macro (slti/sltiu) */
#define GEN_SLT_IMM(inst, cond)                                    \
    GEN(inst, {                                                    \
        vm_reg[0] = ra_load(state, ir->rs1);                       \
        emit_cmp_imm32(state, vm_reg[0], ir->imm);                 \
        vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]); \
        emit_load_imm(state, vm_reg[1], 1);                        \
        uint32_t jump_loc_0 = state->offset;                       \
        emit_jcc_offset(state, cond);                              \
        emit_load_imm(state, vm_reg[1], 0);                        \
        emit_jump_target_offset(state, JUMP_LOC_0, state->offset); \
    })

/* Set-less-than register instruction handler macro (slt/sltu) */
#define GEN_SLT_REG(inst, cond)                                                \
    GEN(inst, {                                                                \
        ra_load2(state, ir->rs1, ir->rs2);                                     \
        vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]); \
        emit_cmp32(state, vm_reg[1], vm_reg[0]);                               \
        emit_load_imm(state, vm_reg[2], 1);                                    \
        uint32_t jump_loc_0 = state->offset;                                   \
        emit_jcc_offset(state, cond);                                          \
        emit_load_imm(state, vm_reg[2], 0);                                    \
        emit_jump_target_offset(state, JUMP_LOC_0, state->offset);             \
    })

/* Load instruction handler macro - handles MMIO path when SYSTEM_MMIO enabled.
 * Parameters:
 *   inst: instruction name (lb, lh, lw, lbu, lhu)
 *   insn_type: rv_insn_* constant for MMIO handler
 *   size: memory access size (S8, S16, S32)
 *   load_fn: emit_load or emit_load_sext
 */
#define GEN_LOAD(inst, insn_type, size, load_fn)                              \
    GEN(inst, {                                                               \
        memory_t *m = PRIV(rv)->mem;                                          \
        vm_reg[0] = ra_load(state, ir->rs1);                                  \
        IIF(RV32_HAS(SYSTEM_MMIO))(                                           \
            {                                                                 \
                emit_load_imm_sext(state, temp_reg, ir->imm);                 \
                emit_alu32(state, ALU_OP_ADD, vm_reg[0], temp_reg);           \
                emit_store(state, S32, temp_reg, parameter_reg[0],            \
                           offsetof(riscv_t, jit_mmu.vaddr));                 \
                emit_load_imm(state, temp_reg, insn_type);                    \
                emit_store(state, S32, temp_reg, parameter_reg[0],            \
                           offsetof(riscv_t, jit_mmu.type));                  \
                /* Store instruction PC for trap return address */            \
                emit_load_imm(state, temp_reg, ir->pc);                       \
                emit_store(state, S32, temp_reg, parameter_reg[0],            \
                           offsetof(riscv_t, jit_mmu.pc));                    \
                                                                              \
                store_back(state);                                            \
                emit_jit_mmu_handler(state, ir->rd);                          \
                reset_reg();                                                  \
                                                                              \
                /* Check if trap occurred - skip load if trapped */           \
                emit_load(state, S8, parameter_reg[0], temp_reg,              \
                          offsetof(riscv_t, is_trapped));                     \
                emit_cmp_imm32(state, temp_reg, 0);                           \
                uint32_t jump_trap = state->offset;                           \
                emit_jcc_offset(state, JCC_JNE);                              \
                                                                              \
                /* If MMIO, load from X[rd]; otherwise load from memory */    \
                emit_load(state, S8, parameter_reg[0], temp_reg,              \
                          offsetof(riscv_t, jit_mmu.is_mmio));                \
                emit_cmp_imm32(state, temp_reg, 0);                           \
                vm_reg[1] = map_vm_reg(state, ir->rd);                        \
                uint32_t jump_loc_0 = state->offset;                          \
                emit_jcc_offset(state, JCC_JE);                               \
                                                                              \
                emit_load(state, S32, parameter_reg[0], vm_reg[1],            \
                          offsetof(riscv_t, X) + 4 * ir->rd);                 \
                uint32_t jump_loc_1 = state->offset;                          \
                emit_jcc_offset(state, JCC_JMP);                              \
                                                                              \
                emit_jump_target_offset(state, JUMP_LOC_0, state->offset);    \
                emit_load(state, S32, parameter_reg[0], temp_reg,             \
                          offsetof(riscv_t, jit_mmu.paddr));                  \
                emit_load_imm_sext(state, vm_reg[1], (intptr_t) m->mem_base); \
                emit_alu64(state, ALU_OP_ADD, temp_reg, vm_reg[1]);           \
                load_fn(state, size, vm_reg[1], vm_reg[1], 0);                \
                emit_jump_target_offset(state, JUMP_LOC_1, state->offset);    \
                /* Jump over trap exit to continue normally */                \
                uint32_t jump_normal = state->offset;                         \
                emit_jcc_offset(state, JCC_JMP);                              \
                /* Trap exit point - exit JIT block for trap handling */      \
                emit_jump_target_offset(state, jump_trap, state->offset);     \
                emit_exit(state);                                             \
                /* Normal continuation point */                               \
                emit_jump_target_offset(state, jump_normal, state->offset);   \
            },                                                                \
            {                                                                 \
                emit_load_imm_sext(state, temp_reg,                           \
                                   (intptr_t) (m->mem_base + ir->imm));       \
                emit_alu64(state, ALU_OP_ADD, vm_reg[0], temp_reg);           \
                vm_reg[1] = map_vm_reg(state, ir->rd);                        \
                load_fn(state, size, temp_reg, vm_reg[1], 0);                 \
            })                                                                \
    })

/* Store instruction handler macro - handles MMIO path when SYSTEM_MMIO enabled.
 * Parameters:
 *   inst: instruction name (sb, sh, sw)
 *   insn_type: rv_insn_* constant for MMIO handler
 *   size: memory access size (S8, S16, S32)
 */
#define GEN_STORE(inst, insn_type, size)                                      \
    GEN(inst, {                                                               \
        memory_t *m = PRIV(rv)->mem;                                          \
        vm_reg[0] = ra_load(state, ir->rs1);                                  \
        IIF(RV32_HAS(SYSTEM_MMIO))(                                           \
            {                                                                 \
                emit_load_imm_sext(state, temp_reg, ir->imm);                 \
                emit_alu32(state, ALU_OP_ADD, vm_reg[0], temp_reg);           \
                emit_store(state, S32, temp_reg, parameter_reg[0],            \
                           offsetof(riscv_t, jit_mmu.vaddr));                 \
                emit_load_imm(state, temp_reg, insn_type);                    \
                emit_store(state, S32, temp_reg, parameter_reg[0],            \
                           offsetof(riscv_t, jit_mmu.type));                  \
                /* Store instruction PC for trap return address */            \
                emit_load_imm(state, temp_reg, ir->pc);                       \
                emit_store(state, S32, temp_reg, parameter_reg[0],            \
                           offsetof(riscv_t, jit_mmu.pc));                    \
                store_back(state);                                            \
                emit_jit_mmu_handler(state, ir->rs2);                         \
                reset_reg();                                                  \
                                                                              \
                /* Check if trap occurred - skip store if trapped */          \
                emit_load(state, S8, parameter_reg[0], temp_reg,              \
                          offsetof(riscv_t, is_trapped));                     \
                emit_cmp_imm32(state, temp_reg, 0);                           \
                uint32_t jump_trap = state->offset;                           \
                emit_jcc_offset(state, JCC_JNE);                              \
                                                                              \
                /* If MMIO, skip store (handled by MMIO handler) */           \
                emit_load(state, S8, parameter_reg[0], temp_reg,              \
                          offsetof(riscv_t, jit_mmu.is_mmio));                \
                emit_cmp_imm32(state, temp_reg, 1);                           \
                uint32_t jump_loc_0 = state->offset;                          \
                emit_jcc_offset(state, JCC_JE);                               \
                                                                              \
                /* Load rs2 value BEFORE computing address to avoid register  \
                 * allocation conflicts. ra_load could evict any allocated    \
                 * register, so we load rs2 first, then use temp_reg for the  \
                 * address (temp_reg is reserved and won't be evicted).       \
                 */                                                           \
                vm_reg[1] = ra_load(state, ir->rs2);                          \
                emit_load(state, S32, parameter_reg[0], temp_reg,             \
                          offsetof(riscv_t, jit_mmu.paddr));                  \
                vm_reg[0] = map_vm_reg(state, rv_reg_zero);                   \
                emit_load_imm_sext(state, vm_reg[0], (intptr_t) m->mem_base); \
                emit_alu64(state, ALU_OP_ADD, vm_reg[0], temp_reg);           \
                emit_store(state, size, vm_reg[1], temp_reg, 0);              \
                emit_jump_target_offset(state, JUMP_LOC_0, state->offset);    \
                /* Jump over trap exit to continue normally */                \
                uint32_t jump_normal = state->offset;                         \
                emit_jcc_offset(state, JCC_JMP);                              \
                /* Trap exit point - exit JIT block for trap handling */      \
                emit_jump_target_offset(state, jump_trap, state->offset);     \
                emit_exit(state);                                             \
                /* Normal continuation point */                               \
                emit_jump_target_offset(state, jump_normal, state->offset);   \
                reset_reg();                                                  \
            },                                                                \
            {                                                                 \
                emit_load_imm_sext(state, temp_reg,                           \
                                   (intptr_t) (m->mem_base + ir->imm));       \
                emit_alu64(state, ALU_OP_ADD, vm_reg[0], temp_reg);           \
                vm_reg[1] = ra_load(state, ir->rs2);                          \
                emit_store(state, size, vm_reg[1], temp_reg, 0);              \
            })                                                                \
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
    emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
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
    parse_branch_history_table(state, rv, ir);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
/* RV32I Branch Instructions */
GEN_BRANCH(beq, JCC_JE)
GEN_BRANCH(bne, JCC_JNE)
GEN_BRANCH(blt, JCC_JL)
GEN_BRANCH(bge, JCC_JGE)
GEN_BRANCH(bltu, JCC_JB)
GEN_BRANCH(bgeu, JCC_JAE)
/* RV32I Load Instructions */
GEN_LOAD(lb, rv_insn_lb, S8, emit_load_sext)
GEN_LOAD(lh, rv_insn_lh, S16, emit_load_sext)
GEN_LOAD(lw, rv_insn_lw, S32, emit_load)
GEN_LOAD(lbu, rv_insn_lbu, S8, emit_load)
GEN_LOAD(lhu, rv_insn_lhu, S16, emit_load)
/* RV32I Store Instructions */
GEN_STORE(sb, rv_insn_sb, S8)
GEN_STORE(sh, rv_insn_sh, S16)
GEN_STORE(sw, rv_insn_sw, S32)
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
GEN(fence, { assert(NULL); })
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
GEN(wfi, { assert(NULL); })
GEN(uret, { assert(NULL); })
#if RV32_HAS(SYSTEM)
GEN(sret, { assert(NULL); })
#endif
GEN(hret, { assert(NULL); })
GEN(mret, { assert(NULL); })
GEN(sfencevma, { assert(NULL); })
#if RV32_HAS(Zifencei) /* RV32 Zifencei Standard Extension */
GEN(fencei, { assert(NULL); })
#endif
#if RV32_HAS(Zicsr) /* RV32 Zicsr Standard Extension */
GEN(csrrw, { assert(NULL); })
GEN(csrrs, { assert(NULL); })
GEN(csrrc, { assert(NULL); })
GEN(csrrwi, { assert(NULL); })
GEN(csrrsi, { assert(NULL); })
GEN(csrrci, { assert(NULL); })
#endif
#if RV32_HAS(EXT_M)
GEN(mul, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x28, temp_reg, vm_reg[2], 0);
})
GEN(mulh, {
    ra_load2_sext(state, ir->rs1, ir->rs2, true, true);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x2f, temp_reg, vm_reg[2], 0);
    emit_alu64_imm8(state, SHIFT_IMM_OPCODE, SHIFT_SHR, vm_reg[2], 32);
})
GEN(mulhsu, {
    ra_load2_sext(state, ir->rs1, ir->rs2, true, false);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x2f, temp_reg, vm_reg[2], 0);
    emit_alu64_imm8(state, SHIFT_IMM_OPCODE, SHIFT_SHR, vm_reg[2], 32);
})
GEN(mulhu, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x2f, temp_reg, vm_reg[2], 0);
    emit_alu64_imm8(state, SHIFT_IMM_OPCODE, SHIFT_SHR, vm_reg[2], 32);
})
GEN(div, {
    ra_load2_sext(state, ir->rs1, ir->rs2, true, true);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x38, temp_reg, vm_reg[2], 1);
})
GEN(divu, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x38, temp_reg, vm_reg[2], 0);
})
GEN(rem, {
    ra_load2_sext(state, ir->rs1, ir->rs2, true, true);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x98, temp_reg, vm_reg[2], 1);
})
GEN(remu, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x98, temp_reg, vm_reg[2], 0);
})
#endif
#if RV32_HAS(EXT_A)
GEN(lrw, { assert(NULL); })
GEN(scw, { assert(NULL); })
GEN(amoswapw, { assert(NULL); })
GEN(amoaddw, { assert(NULL); })
GEN(amoxorw, { assert(NULL); })
GEN(amoandw, { assert(NULL); })
GEN(amoorw, { assert(NULL); })
GEN(amominw, { assert(NULL); })
GEN(amomaxw, { assert(NULL); })
GEN(amominuw, { assert(NULL); })
GEN(amomaxuw, { assert(NULL); })
#endif
#if RV32_HAS(EXT_F)
GEN(flw, { assert(NULL); })
GEN(fsw, { assert(NULL); })
GEN(fmadds, { assert(NULL); })
GEN(fmsubs, { assert(NULL); })
GEN(fnmsubs, { assert(NULL); })
GEN(fnmadds, { assert(NULL); })
GEN(fadds, { assert(NULL); })
GEN(fsubs, { assert(NULL); })
GEN(fmuls, { assert(NULL); })
GEN(fdivs, { assert(NULL); })
GEN(fsqrts, { assert(NULL); })
GEN(fsgnjs, { assert(NULL); })
GEN(fsgnjns, { assert(NULL); })
GEN(fsgnjxs, { assert(NULL); })
GEN(fmins, { assert(NULL); })
GEN(fmaxs, { assert(NULL); })
GEN(fcvtws, { assert(NULL); })
GEN(fcvtwus, { assert(NULL); })
GEN(fmvxw, { assert(NULL); })
GEN(feqs, { assert(NULL); })
GEN(flts, { assert(NULL); })
GEN(fles, { assert(NULL); })
GEN(fclasss, { assert(NULL); })
GEN(fcvtsw, { assert(NULL); })
GEN(fcvtswu, { assert(NULL); })
GEN(fmvwx, { assert(NULL); })
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
GEN(clw, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_load_imm_sext(state, temp_reg, (intptr_t) (m->mem_base + ir->imm));
    emit_alu64(state, 0x01, vm_reg[0], temp_reg);
    vm_reg[1] = map_vm_reg(state, ir->rd);
    emit_load(state, S32, temp_reg, vm_reg[1], 0);
})
GEN(csw, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_load_imm_sext(state, temp_reg, (intptr_t) (m->mem_base + ir->imm));
    emit_alu64(state, 0x01, vm_reg[0], temp_reg);
    vm_reg[1] = ra_load(state, ir->rs2);
    emit_store(state, S32, vm_reg[1], temp_reg, 0);
})
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
    emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
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
    emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
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
GEN(clwsp, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, rv_reg_sp);
    emit_load_imm_sext(state, temp_reg, (intptr_t) (m->mem_base + ir->imm));
    emit_alu64(state, 0x01, vm_reg[0], temp_reg);
    vm_reg[1] = map_vm_reg(state, ir->rd);
    emit_load(state, S32, temp_reg, vm_reg[1], 0);
})
GEN(cjr, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_mov(state, vm_reg[0], temp_reg);
    store_back(state);
    parse_branch_history_table(state, rv, ir);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
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
    vm_reg[1] = map_vm_reg(state, rv_reg_ra);
    emit_load_imm(state, vm_reg[1], ir->pc + 2);
    store_back(state);
    parse_branch_history_table(state, rv, ir);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(cadd, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x01, temp_reg, vm_reg[2]);
})
GEN(cswsp, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, rv_reg_sp);
    emit_load_imm_sext(state, temp_reg, (intptr_t) (m->mem_base + ir->imm));
    emit_alu64(state, 0x01, vm_reg[0], temp_reg);
    vm_reg[1] = ra_load(state, ir->rs2);
    emit_store(state, S32, vm_reg[1], temp_reg, 0);
})
#endif
#if RV32_HAS(EXT_C) && RV32_HAS(EXT_F)
GEN(cflwsp, { assert(NULL); })
GEN(cfswsp, { assert(NULL); })
GEN(cflw, { assert(NULL); })
GEN(cfsw, { assert(NULL); })
#endif
#if RV32_HAS(Zba)
GEN(sh1add, { assert(NULL); })
GEN(sh2add, { assert(NULL); })
GEN(sh3add, { assert(NULL); })
#endif
#if RV32_HAS(Zbb)
GEN(andn, { assert(NULL); })
GEN(orn, { assert(NULL); })
GEN(xnor, { assert(NULL); })
GEN(clz, { assert(NULL); })
GEN(ctz, { assert(NULL); })
GEN(cpop, { assert(NULL); })
GEN(max, { assert(NULL); })
GEN(min, { assert(NULL); })
GEN(maxu, { assert(NULL); })
GEN(minu, { assert(NULL); })
GEN(sextb, { assert(NULL); })
GEN(sexth, { assert(NULL); })
GEN(zexth, { assert(NULL); })
GEN(rol, { assert(NULL); })
GEN(ror, { assert(NULL); })
GEN(rori, { assert(NULL); })
GEN(orcb, { assert(NULL); })
GEN(rev8, { assert(NULL); })
#endif
#if RV32_HAS(Zbc)
GEN(clmul, { assert(NULL); })
GEN(clmulh, { assert(NULL); })
GEN(clmulr, { assert(NULL); })
#endif
#if RV32_HAS(Zbs)
GEN(bclr, { assert(NULL); })
GEN(bclri, { assert(NULL); })
GEN(bext, { assert(NULL); })
GEN(bexti, { assert(NULL); })
GEN(binv, { assert(NULL); })
GEN(binvi, { assert(NULL); })
GEN(bset, { assert(NULL); })
GEN(bseti, { assert(NULL); })
#endif
