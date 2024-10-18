/* This file maps each custom IR to the corresponding LLVM IRs and builds LLVM
 * IR through LLVM-C API. The built LLVM IR is offloaded to the LLVM backend,
 * where it undergoes optimization through several selected LLVM passes.
 * Subsequently, the optimized LLVM IR is passed to the LLVM execution engine,
 * which compiles the optimized LLVM IR and returns a function pointer to the
 * generated machine code.
 */

T2C_OP(nop, { return; })

T2C_OP(lui, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(auipc, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(jal, {
    if (ir->rd)
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 4,
                                 t2c_gen_rd_addr(start, builder, ir));

    if (ir->branch_taken)
        *taken_builder = *builder;
    else {
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + ir->imm,
                                 t2c_gen_PC_addr(start, builder, ir));
        LLVMBuildRetVoid(*builder);
    }
})

FORCE_INLINE void t2c_jit_cache_helper(LLVMBuilderRef *builder,
                                       LLVMValueRef start,
                                       LLVMValueRef addr,
                                       riscv_t *rv,
                                       rv_insn_t *ir)
{
    LLVMBasicBlockRef true_path = LLVMAppendBasicBlock(start, "");
    LLVMBuilderRef true_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(true_builder, true_path);

    LLVMBasicBlockRef false_path = LLVMAppendBasicBlock(start, "");
    LLVMBuilderRef false_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(false_builder, false_path);

    /* get jit-cache base address */
    LLVMValueRef base = LLVMConstIntToPtr(
        LLVMConstInt(LLVMInt64Type(), (long) rv->jit_cache, false),
        LLVMPointerType(t2c_jit_cache_struct_type, 0));

    /* get index */
    LLVMValueRef hash = LLVMBuildAnd(
        *builder, addr,
        LLVMConstInt(LLVMInt32Type(), N_JIT_CACHE_ENTRIES - 1, false), "");

    /* get jit_cache_t::pc */
    LLVMValueRef cast =
        LLVMBuildIntCast2(*builder, hash, LLVMInt64Type(), false, "");
    LLVMValueRef element_ptr = LLVMBuildInBoundsGEP2(
        *builder, t2c_jit_cache_struct_type, base, &cast, 1, "");
    LLVMValueRef pc_ptr = LLVMBuildStructGEP2(
        *builder, t2c_jit_cache_struct_type, element_ptr, 0, "");
    LLVMValueRef pc = LLVMBuildLoad2(*builder, LLVMInt32Type(), pc_ptr, "");

    /* compare with calculated destination */
    LLVMValueRef cmp = LLVMBuildICmp(*builder, LLVMIntEQ, pc, addr, "");

    LLVMBuildCondBr(*builder, cmp, true_path, false_path);

    /* get jit_cache_t::entry */
    LLVMValueRef entry_ptr = LLVMBuildStructGEP2(
        true_builder, t2c_jit_cache_struct_type, element_ptr, 1, "");

    /* invoke T2C JIT-ed code */
    LLVMValueRef t2c_args[1] = {
        LLVMConstInt(LLVMInt64Type(), (long) rv, false)};

    LLVMBuildCall2(true_builder, t2c_jit_cache_func_type,
                   LLVMBuildLoad2(true_builder, LLVMInt64Type(), entry_ptr, ""),
                   t2c_args, 1, "");
    LLVMBuildRetVoid(true_builder);

    /* return to interpreter if cache-miss */
    LLVMBuildStore(false_builder, addr,
                   t2c_gen_PC_addr(start, &false_builder, ir));
    LLVMBuildRetVoid(false_builder);
}

T2C_OP(jalr, {
    /* The register which stores the indirect address needs to be loaded first
     * to avoid being overriden by other operation.
     */
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    val_rs1 = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);
    val_rs1 = T2C_LLVM_GEN_ALU32_IMM(And, val_rs1, ~1U);

    if (ir->rd)
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 4,
                                 t2c_gen_rd_addr(start, builder, ir));

    t2c_jit_cache_helper(builder, start, val_rs1, rv, ir);
})

#define BRANCH_FUNC(type, cond)                                             \
    T2C_OP(type, {                                                          \
        LLVMValueRef addr_PC = t2c_gen_PC_addr(start, builder, ir);         \
        T2C_LLVM_GEN_LOAD_VMREG(rs1, 32,                                    \
                                t2c_gen_rs1_addr(start, builder, ir));      \
        T2C_LLVM_GEN_LOAD_VMREG(rs2, 32,                                    \
                                t2c_gen_rs2_addr(start, builder, ir));      \
        T2C_LLVM_GEN_CMP(cond, val_rs1, val_rs2);                           \
        LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");     \
        LLVMBuilderRef builder2 = LLVMCreateBuilder();                      \
        LLVMPositionBuilderAtEnd(builder2, taken);                          \
        if (ir->branch_taken)                                               \
            *taken_builder = builder2;                                      \
        else {                                                              \
            T2C_LLVM_GEN_STORE_IMM32(builder2, ir->pc + ir->imm, addr_PC);  \
            LLVMBuildRetVoid(builder2);                                     \
        }                                                                   \
        LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken"); \
        LLVMBuilderRef builder3 = LLVMCreateBuilder();                      \
        LLVMPositionBuilderAtEnd(builder3, untaken);                        \
        if (ir->branch_untaken)                                             \
            *untaken_builder = builder3;                                    \
        else {                                                              \
            T2C_LLVM_GEN_STORE_IMM32(builder3, ir->pc + 4, addr_PC);        \
            LLVMBuildRetVoid(builder3);                                     \
        }                                                                   \
        LLVMBuildCondBr(*builder, cmp, taken, untaken);                     \
    })

BRANCH_FUNC(beq, EQ)
BRANCH_FUNC(bne, NE)
BRANCH_FUNC(blt, SLT)
BRANCH_FUNC(bge, SGE)
BRANCH_FUNC(bltu, ULT)
BRANCH_FUNC(bgeu, UGE)

T2C_OP(lb, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    LLVMValueRef res = LLVMBuildSExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt8Type(), mem_loc, "res"),
        LLVMInt32Type(), "sext8to32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(lh, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    LLVMValueRef res = LLVMBuildSExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt16Type(), mem_loc, "res"),
        LLVMInt32Type(), "sext16to32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})


T2C_OP(lw, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), mem_loc, "res");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(lbu, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    LLVMValueRef res = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt8Type(), mem_loc, "res"),
        LLVMInt32Type(), "zext8to32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(lhu, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    LLVMValueRef res = LLVMBuildZExt(
        *builder, LLVMBuildLoad2(*builder, LLVMInt16Type(), mem_loc, "res"),
        LLVMInt32Type(), "zext16to32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(sb, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 8, t2c_gen_rs2_addr(start, builder, ir));
    LLVMBuildStore(*builder, val_rs2, mem_loc);
})

T2C_OP(sh, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 16, t2c_gen_rs2_addr(start, builder, ir));
    LLVMBuildStore(*builder, val_rs2, mem_loc);
})

T2C_OP(sw, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMBuildStore(*builder, val_rs2, mem_loc);
})

T2C_OP(addi, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(slti, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP_IMM32(SLT, val_rs1, ir->imm);
    LLVMValueRef res =
        LLVMBuildSelect(*builder, cmp, LLVMConstInt(LLVMInt32Type(), 1, true),
                        LLVMConstInt(LLVMInt32Type(), 0, true), "");
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(sltiu, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP_IMM32(ULT, val_rs1, ir->imm);
    LLVMValueRef res =
        LLVMBuildSelect(*builder, cmp, LLVMConstInt(LLVMInt32Type(), 1, true),
                        LLVMConstInt(LLVMInt32Type(), 0, true), "");
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(xori, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Xor, val_rs1, ir->imm);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(ori, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Or, val_rs1, ir->imm);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(andi, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(And, val_rs1, ir->imm);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(slli, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Shl, val_rs1, ir->imm & 0x1f);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(srli, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(LShr, val_rs1, ir->imm & 0x1f);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(srai, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(AShr, val_rs1, ir->imm & 0x1f);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(add, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(sub, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildSub(*builder, val_rs1, val_rs2, "sub");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(sll, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs2 = T2C_LLVM_GEN_ALU32_IMM(And, val_rs2, 0x1f);
    LLVMValueRef res = LLVMBuildShl(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(slt, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP(SLT, val_rs1, val_rs2);
    LLVMValueRef res =
        LLVMBuildSelect(*builder, cmp, LLVMConstInt(LLVMInt32Type(), 1, true),
                        LLVMConstInt(LLVMInt32Type(), 0, true), "");
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(sltu, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP(ULT, val_rs1, val_rs2);
    LLVMValueRef res =
        LLVMBuildSelect(*builder, cmp, LLVMConstInt(LLVMInt32Type(), 1, true),
                        LLVMConstInt(LLVMInt32Type(), 0, true), "");
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(xor, {
  T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
  T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
  LLVMValueRef res = LLVMBuildXor(*builder, val_rs1, val_rs2, "xor");
  LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(srl, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs2 = T2C_LLVM_GEN_ALU32_IMM(And, val_rs2, 0x1f);
    LLVMValueRef res = LLVMBuildLShr(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(sra, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs2 = T2C_LLVM_GEN_ALU32_IMM(And, val_rs2, 0x1f);
    LLVMValueRef res = LLVMBuildAShr(*builder, val_rs1, val_rs2, "sll");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(or, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildOr(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(and, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildAnd(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(fence, { __UNREACHABLE; })

T2C_OP(ecall, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    t2c_gen_call_io_func(start, builder, param_types, 8);
    LLVMBuildRetVoid(*builder);
})

T2C_OP(ebreak, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    t2c_gen_call_io_func(start, builder, param_types, 9);
    LLVMBuildRetVoid(*builder);
})

T2C_OP(wfi, { __UNREACHABLE; })

T2C_OP(uret, { __UNREACHABLE; })

#if RV32_HAS(SYSTEM)
T2C_OP(sret, { __UNREACHABLE; })
#endif

T2C_OP(hret, { __UNREACHABLE; })

T2C_OP(mret, { __UNREACHABLE; })

T2C_OP(sfencevma, { __UNREACHABLE; })

#if RV32_HAS(Zifencei)
T2C_OP(fencei, { __UNREACHABLE; })
#endif

#if RV32_HAS(Zicsr)
T2C_OP(csrrw, { __UNREACHABLE; })

T2C_OP(csrrs, { __UNREACHABLE; })

T2C_OP(csrrc, { __UNREACHABLE; })

T2C_OP(csrrwi, { __UNREACHABLE; })

T2C_OP(csrrsi, { __UNREACHABLE; })

T2C_OP(csrrci, { __UNREACHABLE; })
#endif

#if RV32_HAS(EXT_M)
T2C_OP(mul, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildSExt(*builder, val_rs1, LLVMInt64Type(), "sextrs1to64");
    val_rs2 = LLVMBuildSExt(*builder, val_rs2, LLVMInt64Type(), "sextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(And, res, 0xFFFFFFFF);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(mulh, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildSExt(*builder, val_rs1, LLVMInt64Type(), "sextrs1to64");
    val_rs2 = LLVMBuildSExt(*builder, val_rs2, LLVMInt64Type(), "sextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(LShr, res, 32);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(mulhsu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildSExt(*builder, val_rs1, LLVMInt64Type(), "sextrs1to64");
    val_rs2 = LLVMBuildZExt(*builder, val_rs2, LLVMInt64Type(), "zextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(LShr, res, 32);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(mulhu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildZExt(*builder, val_rs1, LLVMInt64Type(), "sextrs1to64");
    val_rs2 = LLVMBuildZExt(*builder, val_rs2, LLVMInt64Type(), "zextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(LShr, res, 32);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32Type(), "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(div, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildSDiv(*builder, val_rs1, val_rs2, "sdiv");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(divu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildUDiv(*builder, val_rs1, val_rs2, "udiv");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(rem, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildSRem(*builder, val_rs1, val_rs2, "srem");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(remu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildURem(*builder, val_rs1, val_rs2, "urem");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})
#endif

#if RV32_HAS(EXT_A)
T2C_OP(lrw, { __UNREACHABLE; })

T2C_OP(scw, { __UNREACHABLE; })

T2C_OP(amoswapw, { __UNREACHABLE; })

T2C_OP(amoaddw, { __UNREACHABLE; })

T2C_OP(amoxorw, { __UNREACHABLE; })

T2C_OP(amoandw, { __UNREACHABLE; })

T2C_OP(amoorw, { __UNREACHABLE; })

T2C_OP(amominw, { __UNREACHABLE; })

T2C_OP(amomaxw, { __UNREACHABLE; })

T2C_OP(amominuw, { __UNREACHABLE; })

T2C_OP(amomaxuw, { __UNREACHABLE; })
#endif

#if RV32_HAS(EXT_F)
T2C_OP(flw, { __UNREACHABLE; })

T2C_OP(fsw, { __UNREACHABLE; })

T2C_OP(fmadds, { __UNREACHABLE; })

T2C_OP(fmsubs, { __UNREACHABLE; })

T2C_OP(fnmsubs, { __UNREACHABLE; })

T2C_OP(fnmadds, { __UNREACHABLE; })

T2C_OP(fadds, { __UNREACHABLE; })

T2C_OP(fsubs, { __UNREACHABLE; })

T2C_OP(fmuls, { __UNREACHABLE; })

T2C_OP(fdivs, { __UNREACHABLE; })

T2C_OP(fsqrts, { __UNREACHABLE; })

T2C_OP(fsgnjs, { __UNREACHABLE; })

T2C_OP(fsgnjns, { __UNREACHABLE; })

T2C_OP(fsgnjxs, { __UNREACHABLE; })

T2C_OP(fmins, { __UNREACHABLE; })

T2C_OP(fmaxs, { __UNREACHABLE; })

T2C_OP(fcvtws, { __UNREACHABLE; })

T2C_OP(fcvtwus, { __UNREACHABLE; })

T2C_OP(fmvxw, { __UNREACHABLE; })

T2C_OP(feqs, { __UNREACHABLE; })

T2C_OP(flts, { __UNREACHABLE; })

T2C_OP(fles, { __UNREACHABLE; })

T2C_OP(fclasss, { __UNREACHABLE; })

T2C_OP(fcvtsw, { __UNREACHABLE; })

T2C_OP(fcvtswu, { __UNREACHABLE; })

T2C_OP(fmvwx, { __UNREACHABLE; })
#endif

#if RV32_HAS(EXT_C)
T2C_OP(caddi4spn, {
    T2C_LLVM_GEN_LOAD_VMREG(sp, 32, t2c_gen_sp_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Add, val_sp, (int16_t) ir->imm);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})
T2C_OP(clw, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), mem_loc, "res");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(csw, {
    LLVMValueRef mem_loc = t2c_gen_mem_loc(start, builder, ir, mem_base);
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMBuildStore(*builder, val_rs2, mem_loc);
})

T2C_OP(cnop, { return; })

T2C_OP(caddi, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rd, 32, addr_rd);
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Add, val_rd, (int16_t) ir->imm);
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(cjal, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 2,
                             t2c_gen_ra_addr(start, builder, ir));
    if (ir->branch_taken)
        *taken_builder = *builder;
    else {
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + ir->imm,
                                 t2c_gen_PC_addr(start, builder, ir));
        LLVMBuildRetVoid(*builder);
    }
})

T2C_OP(cli, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(caddi16sp, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rd, 32, addr_rd);
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Add, val_rd, ir->imm);
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(clui, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(csrli, {
    LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(LShr, val_rs1, ir->shamt);
    LLVMBuildStore(*builder, res, addr_rs1);
})

T2C_OP(csrai, {
    LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(AShr, val_rs1, ir->shamt);
    LLVMBuildStore(*builder, res, addr_rs1);
})

T2C_OP(candi, {
    LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(And, val_rs1, ir->imm);
    LLVMBuildStore(*builder, res, addr_rs1);
})

T2C_OP(csub, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildSub(*builder, val_rs1, val_rs2, "sub");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cxor, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildXor(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cor, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildOr(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cand, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildAnd(*builder, val_rs1, val_rs2, "xor");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cj, {
    if (ir->branch_taken)
        *taken_builder = *builder;
    else {
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + ir->imm,
                                 t2c_gen_PC_addr(start, builder, ir));
        LLVMBuildRetVoid(*builder);
    }
})

T2C_OP(cbeqz, {
    LLVMValueRef addr_PC = t2c_gen_PC_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP_IMM32(EQ, val_rs1, 0);
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken)
        *taken_builder = builder2;
    else {
        T2C_LLVM_GEN_STORE_IMM32(builder2, ir->pc + ir->imm, addr_PC);
        LLVMBuildRetVoid(builder2);
    }

    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken)
        *untaken_builder = builder3;
    else {
        T2C_LLVM_GEN_STORE_IMM32(builder3, ir->pc + 2, addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cmp, taken, untaken);
})

T2C_OP(cbnez, {
    LLVMValueRef addr_PC = t2c_gen_PC_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP_IMM32(NE, val_rs1, 0);
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken)
        *taken_builder = builder2;
    else {
        T2C_LLVM_GEN_STORE_IMM32(builder2, ir->pc + ir->imm, addr_PC);
        LLVMBuildRetVoid(builder2);
    }

    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken)
        *untaken_builder = builder3;
    else {
        T2C_LLVM_GEN_STORE_IMM32(builder3, ir->pc + 2, addr_PC);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cmp, taken, untaken);
})

T2C_OP(cslli, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rd, 32, addr_rd);
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Shl, val_rd, (uint8_t) ir->imm);
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(clwsp, {
    LLVMValueRef val_sp = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       t2c_gen_sp_addr(start, builder, ir), "val_sp"),
        LLVMInt64Type(), "zext32to64");
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMValueRef res =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cjr, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    t2c_jit_cache_helper(builder, start, val_rs1, rv, ir);
})

T2C_OP(cmv, {
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMBuildStore(*builder, val_rs2, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cebreak, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    t2c_gen_call_io_func(start, builder, param_types, 9);
    LLVMBuildRetVoid(*builder);
})

T2C_OP(cjalr, {
    /* The register which stores the indirect address needs to be loaded first
     * to avoid being overriden by other operation.
     */
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 2,
                             t2c_gen_ra_addr(start, builder, ir));
    t2c_jit_cache_helper(builder, start, val_rs1, rv, ir);
})

T2C_OP(cadd, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cswsp, {
    LLVMValueRef addr_rs2 = t2c_gen_rs2_addr(start, builder, ir);
    LLVMValueRef val_sp = LLVMBuildZExt(
        *builder,
        LLVMBuildLoad2(*builder, LLVMInt32Type(),
                       t2c_gen_sp_addr(start, builder, ir), "val_sp"),
        LLVMInt64Type(), "zext32to64");
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, addr_rs2);
    LLVMValueRef addr = LLVMBuildAdd(
        *builder, val_sp,
        LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true), "addr");
    LLVMValueRef cast_addr = LLVMBuildIntToPtr(
        *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
    LLVMBuildStore(*builder, val_rs2, cast_addr);
})
#endif

#if RV32_HAS(EXT_C) && RV32_HAS(EXT_F)
T2C_OP(cflwsp, { __UNREACHABLE; })

T2C_OP(cfswsp, { __UNREACHABLE; })

T2C_OP(cflw, { __UNREACHABLE; })

T2C_OP(cfsw, { __UNREACHABLE; })
#endif

T2C_OP(fuse1, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        LLVMValueRef rd_offset =
            LLVMConstInt(LLVMInt32Type(),
                         offsetof(riscv_t, X) / sizeof(int) + fuse[i].rd, true);
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),
                                                     LLVMGetParam(start, 0),
                                                     &rd_offset, 1, "addr_rd");
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32Type(), fuse[i].imm, true),
                       addr_rd);
    }
})

T2C_OP(fuse2, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->imm, addr_rd);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rd, 32, addr_rd);
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rd, "add");
    LLVMBuildStore(*builder, res, t2c_gen_rs2_addr(start, builder, ir));
})

T2C_OP(fuse3, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        LLVMValueRef mem_loc =
            t2c_gen_mem_loc(start, builder, (rv_insn_t *) (&fuse[i]), mem_base);
        T2C_LLVM_GEN_LOAD_VMREG(
            rs2, 32,
            t2c_gen_rs2_addr(start, builder, (rv_insn_t *) (&fuse[i])));
        LLVMBuildStore(*builder, val_rs2, mem_loc);
    }
})

T2C_OP(fuse4, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        LLVMValueRef mem_loc =
            t2c_gen_mem_loc(start, builder, (rv_insn_t *) (&fuse[i]), mem_base);
        LLVMValueRef res =
            LLVMBuildLoad2(*builder, LLVMInt32Type(), mem_loc, "res");
        LLVMBuildStore(
            *builder, res,
            t2c_gen_rd_addr(start, builder, (rv_insn_t *) (&fuse[i])));
    }
})

T2C_OP(fuse5, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            t2c_slli(builder, param_types, start, entry, taken_builder,
                     untaken_builder, rv, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        case rv_insn_srli:
            t2c_srli(builder, param_types, start, entry, taken_builder,
                     untaken_builder, rv, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        case rv_insn_srai:
            t2c_srai(builder, param_types, start, entry, taken_builder,
                     untaken_builder, rv, mem_base, (rv_insn_t *) (&fuse[i]));
            break;
        default:
            __UNREACHABLE;
            break;
        }
    }
})
