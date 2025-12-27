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
    emit_alu32_imm32(state, 0x81, 0, temp_reg, ir->imm);
    emit_alu32_imm32(state, 0x81, 4, temp_reg, ~1U);
    if (ir->rd) {
        vm_reg[1] = map_vm_reg(state, ir->rd);
        emit_load_imm(state, vm_reg[1], ir->pc + 4);
    }
    store_back(state);
    parse_branch_history_table(state, rv, ir);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(beq, {
    ra_load2(state, ir->rs1, ir->rs2);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x84);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 4, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 4);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(bne, {
    ra_load2(state, ir->rs1, ir->rs2);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x85);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 4, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 4);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(blt, {
    ra_load2(state, ir->rs1, ir->rs2);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x8c);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 4, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 4);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(bge, {
    ra_load2(state, ir->rs1, ir->rs2);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x8d);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 4, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 4);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(bltu, {
    ra_load2(state, ir->rs1, ir->rs2);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x82);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 4, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 4);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(bgeu, {
    ra_load2(state, ir->rs1, ir->rs2);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x83);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 4, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 4);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(lb, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_lb);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));

            store_back(state);
            emit_jit_mmu_handler(state, ir->rd);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, assign the read value to host register, otherwise,
             * load from memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 0);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);
            vm_reg[1] = map_vm_reg(state, ir->rd);

            emit_load(state, S32, parameter_reg[0], vm_reg[1],
                      offsetof(riscv_t, X) + 4 * ir->rd);
            /* skip regular loading */
            uint64_t jump_loc_1 = state->offset;
            emit_jcc_offset(state, 0xe9);

            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            emit_load_sext(state, S8, temp_reg, vm_reg[1], 0);
            emit_jump_target_offset(state, JUMP_LOC_1, state->offset);
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = map_vm_reg(state, ir->rd);
            emit_load_sext(state, S8, temp_reg, vm_reg[1], 0);
        })
})
GEN(lh, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_lh);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));

            store_back(state);
            emit_jit_mmu_handler(state, ir->rd);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, assign the read value to host register, otherwise,
             * load from memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 0);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);
            vm_reg[1] = map_vm_reg(state, ir->rd);

            emit_load(state, S32, parameter_reg[0], vm_reg[1],
                      offsetof(riscv_t, X) + 4 * ir->rd);
            /* skip regular loading */
            uint64_t jump_loc_1 = state->offset;
            emit_jcc_offset(state, 0xe9);

            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            emit_load_sext(state, S16, temp_reg, vm_reg[1], 0);
            emit_jump_target_offset(state, JUMP_LOC_1, state->offset);
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = map_vm_reg(state, ir->rd);
            emit_load_sext(state, S16, temp_reg, vm_reg[1], 0);
        })
})
GEN(lw, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_lw);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));

            store_back(state);
            emit_jit_mmu_handler(state, ir->rd);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, assign the read value to host register, otherwise,
             * load from memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 0);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);
            vm_reg[1] = map_vm_reg(state, ir->rd);

            emit_load(state, S32, parameter_reg[0], vm_reg[1],
                      offsetof(riscv_t, X) + 4 * ir->rd);
            /* skip regular loading */
            uint64_t jump_loc_1 = state->offset;
            emit_jcc_offset(state, 0xe9);

            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            emit_load(state, S32, temp_reg, vm_reg[1], 0);
            emit_jump_target_offset(state, JUMP_LOC_1, state->offset);
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = map_vm_reg(state, ir->rd);
            emit_load(state, S32, temp_reg, vm_reg[1], 0);
        })
})
GEN(lbu, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_lbu);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));

            store_back(state);
            emit_jit_mmu_handler(state, ir->rd);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, assign the read value to host register, otherwise,
             * load from memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 0);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);
            vm_reg[1] = map_vm_reg(state, ir->rd);

            emit_load(state, S32, parameter_reg[0], vm_reg[1],
                      offsetof(riscv_t, X) + 4 * ir->rd);
            /* skip regular loading */
            uint64_t jump_loc_1 = state->offset;
            emit_jcc_offset(state, 0xe9);

            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            emit_load(state, S8, temp_reg, vm_reg[1], 0);
            emit_jump_target_offset(state, JUMP_LOC_1, state->offset);
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = map_vm_reg(state, ir->rd);
            emit_load(state, S8, temp_reg, vm_reg[1], 0);
        })
})
GEN(lhu, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_lhu);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));

            store_back(state);
            emit_jit_mmu_handler(state, ir->rd);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, assign the read value to host register, otherwise,
             * load from memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 0);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);
            vm_reg[1] = map_vm_reg(state, ir->rd);

            emit_load(state, S32, parameter_reg[0], vm_reg[1],
                      offsetof(riscv_t, X) + 4 * ir->rd);
            /* skip regular loading */
            uint64_t jump_loc_1 = state->offset;
            emit_jcc_offset(state, 0xe9);

            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            emit_load(state, S16, temp_reg, vm_reg[1], 0);
            emit_jump_target_offset(state, JUMP_LOC_1, state->offset);
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = map_vm_reg(state, ir->rd);
            emit_load(state, S16, temp_reg, vm_reg[1], 0);
        })
})
GEN(sb, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_sb);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));
            store_back(state);
            emit_jit_mmu_handler(state, ir->rs2);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, it does not need to do the storing since it has
             * been done in the mmio handler, otherwise, store the value into
             * memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 1);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);

            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = ra_load(state, ir->rs2);
            emit_store(state, S8, vm_reg[1], temp_reg, 0);
            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            /*
             * Clear register mapping since we do not ensure operand "ir->rs2"
             * is loaded or not.
             */
            reset_reg();
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = ra_load(state, ir->rs2);
            emit_store(state, S8, vm_reg[1], temp_reg, 0);
        })
})
GEN(sh, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_sh);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));
            store_back(state);
            emit_jit_mmu_handler(state, ir->rs2);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, it does not need to do the storing since it has
             * been done in the mmio handler, otherwise, store the value into
             * memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 1);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);

            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = ra_load(state, ir->rs2);
            emit_store(state, S16, vm_reg[1], temp_reg, 0);
            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            /*
             * Clear register mapping since we do not ensure operand "ir->rs2"
             * is loaded or not.
             */
            reset_reg();
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = ra_load(state, ir->rs2);
            emit_store(state, S16, vm_reg[1], temp_reg, 0);
        })
})
GEN(sw, {
    memory_t *m = PRIV(rv)->mem;
    vm_reg[0] = ra_load(state, ir->rs1);
    IIF(RV32_HAS(SYSTEM_MMIO))(
        {
            emit_load_imm_sext(state, temp_reg, ir->imm);
            emit_alu32(state, 0x01, vm_reg[0], temp_reg);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.vaddr));
            emit_load_imm(state, temp_reg, rv_insn_sw);
            emit_store(state, S32, temp_reg, parameter_reg[0],
                       offsetof(riscv_t, jit_mmu.type));
            store_back(state);
            emit_jit_mmu_handler(state, ir->rs2);
            /* clear register mapping */
            reset_reg();

            /*
             * If it's MMIO, it does not need to do the storing since it has
             * been done in the mmio handler, otherwise, store the value into
             * memory.
             */
            emit_load(state, S32, parameter_reg[0], temp_reg,
                      offsetof(riscv_t, jit_mmu.is_mmio));
            emit_cmp_imm32(state, temp_reg, 1);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x84);

            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, jit_mmu.paddr));
            emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base);
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = ra_load(state, ir->rs2);
            emit_store(state, S32, vm_reg[1], temp_reg, 0);
            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            /*
             * Clear register mapping since we do not ensure operand "ir->rs2"
             * is loaded into host register "vm_reg[1]" or not.
             */
            reset_reg();
        },
        {
            emit_load_imm_sext(state, temp_reg,
                               (intptr_t) (m->mem_base + ir->imm));
            emit_alu64(state, 0x01, vm_reg[0], temp_reg);
            vm_reg[1] = ra_load(state, ir->rs2);
            emit_store(state, S32, vm_reg[1], temp_reg, 0);
        })
})
GEN(addi, {
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm32(state, 0x81, 0, vm_reg[1], ir->imm);
})
GEN(slti, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_cmp_imm32(state, vm_reg[0], ir->imm);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    emit_load_imm(state, vm_reg[1], 1);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x8c);
    emit_load_imm(state, vm_reg[1], 0);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
})
GEN(sltiu, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_cmp_imm32(state, vm_reg[0], ir->imm);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    emit_load_imm(state, vm_reg[1], 1);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x82);
    emit_load_imm(state, vm_reg[1], 0);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
})
GEN(xori, {
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm32(state, 0x81, 6, vm_reg[1], ir->imm);
})
GEN(ori, {
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm32(state, 0x81, 1, vm_reg[1], ir->imm);
})
GEN(andi, {
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm32(state, 0x81, 4, vm_reg[1], ir->imm);
})
GEN(slli, {
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm8(state, 0xc1, 4, vm_reg[1], ir->imm & 0x1f);
})
GEN(srli, {
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm8(state, 0xc1, 5, vm_reg[1], ir->imm & 0x1f);
})
GEN(srai, {
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1]) {
        emit_mov(state, vm_reg[0], vm_reg[1]);
    }
    emit_alu32_imm8(state, 0xc1, 7, vm_reg[1], ir->imm & 0x1f);
})
GEN(add, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x01, temp_reg, vm_reg[2]);
})
GEN(sub, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x29, temp_reg, vm_reg[2]);
})
GEN(sll, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32_imm32(state, 0x81, 4, temp_reg, 0x1f);
    emit_alu32(state, 0xd3, 4, vm_reg[2]);
})
GEN(slt, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    emit_load_imm(state, vm_reg[2], 1);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x8c);
    emit_load_imm(state, vm_reg[2], 0);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
})
GEN(sltu, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_cmp32(state, vm_reg[1], vm_reg[0]);
    emit_load_imm(state, vm_reg[2], 1);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x82);
    emit_load_imm(state, vm_reg[2], 0);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
})
GEN(xor, {
  ra_load2(state, ir->rs1, ir->rs2);
  vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
  emit_mov(state, vm_reg[1], temp_reg);
  emit_mov(state, vm_reg[0], vm_reg[2]);
  emit_alu32(state, 0x31, temp_reg, vm_reg[2]);
})
GEN(srl, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32_imm32(state, 0x81, 4, temp_reg, 0x1f);
    emit_alu32(state, 0xd3, 5, vm_reg[2]);
})
GEN(sra, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32_imm32(state, 0x81, 4, temp_reg, 0x1f);
    emit_alu32(state, 0xd3, 7, vm_reg[2]);
})
GEN(or, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x09, temp_reg, vm_reg[2]);
})
GEN(and, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    emit_alu32(state, 0x21, temp_reg, vm_reg[2]);
})
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
    emit_alu64_imm8(state, 0xc1, 5, vm_reg[2], 32);
})
GEN(mulhsu, {
    ra_load2_sext(state, ir->rs1, ir->rs2, true, false);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x2f, temp_reg, vm_reg[2], 0);
    emit_alu64_imm8(state, 0xc1, 5, vm_reg[2], 32);
})
GEN(mulhu, {
    ra_load2(state, ir->rs1, ir->rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, ir->rd, vm_reg[0], vm_reg[1]);
    emit_mov(state, vm_reg[1], temp_reg);
    emit_mov(state, vm_reg[0], vm_reg[2]);
    muldivmod(state, 0x2f, temp_reg, vm_reg[2], 0);
    emit_alu64_imm8(state, 0xc1, 5, vm_reg[2], 32);
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
    emit_alu32_imm32(state, 0x81, 0, vm_reg[1], (uint16_t) ir->imm);
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
    emit_alu32_imm32(state, 0x81, 0, vm_reg[0], (int16_t) ir->imm);
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
    emit_alu32_imm32(state, 0x81, 0, vm_reg[0], ir->imm);
})
GEN(clui, {
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
})
GEN(csrli, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm8(state, 0xc1, 5, vm_reg[0], ir->shamt);
})
GEN(csrai, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm8(state, 0xc1, 7, vm_reg[0], ir->shamt);
})
GEN(candi, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm32(state, 0x81, 4, vm_reg[0], ir->imm);
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
GEN(cbeqz, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_cmp_imm32(state, vm_reg[0], 0);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x84);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 2, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 2);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(cbnez, {
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_cmp_imm32(state, vm_reg[0], 0);
    store_back(state);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x85);
    if (ir->branch_untaken) {
        emit_jmp(state, ir->pc + 2, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + 2);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (ir->branch_taken) {
        emit_jmp(state, ir->pc + ir->imm, rv->csr_satp);
    }
    emit_load_imm(state, temp_reg, ir->pc + ir->imm);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
})
GEN(cslli, {
    vm_reg[0] = ra_load(state, ir->rd);
    emit_alu32_imm8(state, 0xc1, 4, vm_reg[0], (uint8_t) ir->imm);
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
