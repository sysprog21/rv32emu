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

/* Query the block by pc and return if it is valid. */
static bool t2c_check_valid_blk(riscv_t *rv, block_t *block UNUSED, uint32_t pc)
{
    block_t *blk = cache_get(rv->block_cache, pc, false);
    if (!blk || !blk->translatable)
        return false;

#if RV32_HAS(SYSTEM)
    if (blk->satp != block->satp)
        return false;
#endif

    return true;
}

T2C_OP(jal, {
    if (ir->rd)
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 4,
                                 t2c_gen_rd_addr(start, builder, ir));

    if (ir->branch_taken &&
        t2c_check_valid_blk(rv, block, ir->branch_taken->pc)) {
        *taken_builder = *builder;
    } else {
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + ir->imm,
                                 t2c_gen_PC_addr(start, builder, ir));
        T2C_STORE_TIMER(*builder, start, insn_counter);
        LLVMBuildRetVoid(*builder);
    }
})

FORCE_INLINE void t2c_jit_cache_helper(LLVMBuilderRef *builder,
                                       LLVMValueRef start,
                                       LLVMValueRef addr,
                                       riscv_t *rv UNUSED,
                                       block_t *block UNUSED,
                                       rv_insn_t *ir,
                                       LLVMValueRef insn_counter)
{
    /* Seqlock read pattern for lock-free jit_cache lookup:
     *
     *   1. Load seq1 (acquire), if odd (write in progress) -> fallback
     *   2. Load key (monotonic), compare with expected -> fallback on mismatch
     *   3. Load entry (acquire), ensuring all data loads complete
     *   4. Load seq2 (monotonic), if seq1 != seq2 or entry == NULL -> fallback
     *   5. Entry is consistent, call JIT code
     *
     * Memory ordering: Acquire loads on seq1 and entry provide the necessary
     * barriers. On x86, these are plain loads (TSO provides ordering). On
     * ARM64, they generate LDAR instructions, cheaper than separate load + dmb
     * fence.
     */
    LLVMBasicBlockRef seq_even = LLVMAppendBasicBlock(start, "seq_even");
    LLVMBuilderRef seq_even_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(seq_even_builder, seq_even);

    LLVMBasicBlockRef key_match = LLVMAppendBasicBlock(start, "key_match");
    LLVMBuilderRef key_match_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(key_match_builder, key_match);

    LLVMBasicBlockRef call_jit = LLVMAppendBasicBlock(start, "call_jit");
    LLVMBuilderRef call_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(call_builder, call_jit);

    LLVMBasicBlockRef fallback = LLVMAppendBasicBlock(start, "fallback");
    LLVMBuilderRef fallback_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(fallback_builder, fallback);

    /* Get jit-cache base address from runtime rv parameter.
     * Load rv->jit_cache using offsetof(riscv_t, jit_cache) computed at compile
     * time. The actual offset varies depending on struct layout (e.g., ~768 on
     * x86-64 Linux with system emulation enabled). */
    LLVMValueRef rv_param = LLVMGetParam(start, 0);
    LLVMValueRef jit_cache_offset =
        LLVMConstInt(LLVMInt64Type(), offsetof(riscv_t, jit_cache), false);
    LLVMValueRef jit_cache_ptr = LLVMBuildInBoundsGEP2(
        *builder, LLVMInt8Type(), rv_param, &jit_cache_offset, 1, "");
    LLVMValueRef base =
        LLVMBuildLoad2(*builder, LLVMPointerType(t2c_jit_cache_struct_type, 0),
                       jit_cache_ptr, "");

    /* Compute cache index: (addr ^ satp) & mask in system mode, addr & mask
     * otherwise. Must match jit_cache_update's indexing for correct lookup. */
#if RV32_HAS(SYSTEM)
    LLVMValueRef satp_offset_early =
        LLVMConstInt(LLVMInt64Type(), offsetof(riscv_t, csr_satp), false);
    LLVMValueRef satp_ptr_early = LLVMBuildInBoundsGEP2(
        *builder, LLVMInt8Type(), rv_param, &satp_offset_early, 1, "");
    LLVMValueRef satp_early =
        LLVMBuildLoad2(*builder, LLVMInt32Type(), satp_ptr_early, "");
    LLVMValueRef hash_xor = LLVMBuildXor(*builder, addr, satp_early, "");
    LLVMValueRef hash = LLVMBuildAnd(
        *builder, hash_xor,
        LLVMConstInt(LLVMInt32Type(), N_JIT_CACHE_ENTRIES - 1, false), "");
#else
    LLVMValueRef hash = LLVMBuildAnd(
        *builder, addr,
        LLVMConstInt(LLVMInt32Type(), N_JIT_CACHE_ENTRIES - 1, false), "");
#endif

    /* Get element pointer: &cache[hash] */
    LLVMValueRef cast =
        LLVMBuildIntCast2(*builder, hash, LLVMInt64Type(), false, "");
    LLVMValueRef element_ptr = LLVMBuildInBoundsGEP2(
        *builder, t2c_jit_cache_struct_type, base, &cast, 1, "");

    /* Step 1: Load seq1 (acquire). Odd value means write in progress. */
    LLVMValueRef seq_ptr = LLVMBuildStructGEP2(
        *builder, t2c_jit_cache_struct_type, element_ptr, 0, "");
    LLVMValueRef seq1 = LLVMBuildLoad2(*builder, LLVMInt32Type(), seq_ptr, "");
    LLVMSetOrdering(seq1, LLVMAtomicOrderingAcquire);
    LLVMValueRef seq_odd = LLVMBuildAnd(
        *builder, seq1, LLVMConstInt(LLVMInt32Type(), 1, false), "");
    LLVMValueRef is_even =
        LLVMBuildICmp(*builder, LLVMIntEQ, seq_odd,
                      LLVMConstInt(LLVMInt32Type(), 0, false), "");
    LLVMBuildCondBr(*builder, is_even, seq_even, fallback);

    /* Step 2: Load key (monotonic) and compare with expected.
     * Use 8-byte alignment hint to force inline load instead of libatomic call.
     * Struct layout: [seq:i32, pad:i32, key:i64, entry:ptr] - key is index 2.
     */
    LLVMValueRef key_ptr = LLVMBuildStructGEP2(
        seq_even_builder, t2c_jit_cache_struct_type, element_ptr, 2, "");
#if RV32_HAS(SYSTEM)
    LLVMValueRef key =
        LLVMBuildLoad2(seq_even_builder, LLVMInt64Type(), key_ptr, "");
    LLVMSetOrdering(key, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(key, 8); /* Force 8-byte alignment to avoid libatomic */
    LLVMValueRef addr64 =
        LLVMBuildIntCast2(seq_even_builder, addr, LLVMInt64Type(), false, "");
    /* Reuse satp_early loaded before hash calculation to avoid spurious misses.
     * Loading SATP twice could cause hash/key mismatch if SATP changes between
     * loads, forcing unnecessary fallback even when valid entry exists.
     */
    LLVMValueRef satp64 = LLVMBuildIntCast2(seq_even_builder, satp_early,
                                            LLVMInt64Type(), false, "");
    LLVMValueRef satp_shifted = LLVMBuildShl(
        seq_even_builder, satp64, LLVMConstInt(LLVMInt64Type(), 32, false), "");
    LLVMValueRef expected_key =
        LLVMBuildAdd(seq_even_builder, addr64, satp_shifted, "");
#else
    /* Non-system mode: key is still uint64_t in struct, but only lower 32 bits
     * are meaningful (PC only). Load as i64 for type consistency, then compare
     * with zero-extended addr to avoid type mismatch in LLVM IR.
     */
    LLVMValueRef key =
        LLVMBuildLoad2(seq_even_builder, LLVMInt64Type(), key_ptr, "");
    LLVMSetOrdering(key, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(key, 8); /* Force 8-byte alignment to avoid libatomic */
    LLVMValueRef expected_key =
        LLVMBuildIntCast2(seq_even_builder, addr, LLVMInt64Type(), false, "");
#endif

    LLVMValueRef key_cmp =
        LLVMBuildICmp(seq_even_builder, LLVMIntEQ, key, expected_key, "");
    LLVMBuildCondBr(seq_even_builder, key_cmp, key_match, fallback);

    /* Step 3: Load entry (acquire). Pointer type required for ARM64 codegen.
     * Field index 3 (struct has explicit pad at index 1). Acquire ordering
     * ensures data loads complete before seq2 check. On ARM64, generates LDAR.
     */
    LLVMValueRef entry_ptr = LLVMBuildStructGEP2(
        key_match_builder, t2c_jit_cache_struct_type, element_ptr, 3, "");
    LLVMValueRef entry = LLVMBuildLoad2(
        key_match_builder, LLVMPointerType(LLVMVoidType(), 0), entry_ptr, "");
    LLVMSetOrdering(entry, LLVMAtomicOrderingAcquire);
    LLVMSetAlignment(entry, 8); /* Pointer is 8-byte aligned on 64-bit */

    /* Step 4: Load seq2 (monotonic), check seq1 == seq2 AND entry != NULL.
     * Entry can be NULL if cache was cleared via jit_cache_clear_page.
     * Without this check, cleared entries could cause NULL pointer call.
     */
    LLVMValueRef seq2 =
        LLVMBuildLoad2(key_match_builder, LLVMInt32Type(), seq_ptr, "");
    LLVMSetOrdering(seq2, LLVMAtomicOrderingMonotonic);

    LLVMValueRef seq_cmp =
        LLVMBuildICmp(key_match_builder, LLVMIntEQ, seq1, seq2, "");
    LLVMValueRef entry_not_null =
        LLVMBuildIsNotNull(key_match_builder, entry, "");
    LLVMValueRef valid =
        LLVMBuildAnd(key_match_builder, seq_cmp, entry_not_null, "");
    LLVMBuildCondBr(key_match_builder, valid, call_jit, fallback);

    /* Step 5: Entry is consistent, call JIT code. Pass runtime rv parameter
     * (not compile-time constant) for correct chaining between T2C blocks.
     */
#if defined(__aarch64__)
    /* ARM64 instruction cache synchronization barrier.
     * When T2C-compiled code calls another T2C block via jit_cache lookup,
     * the target block may have been compiled by the T2C thread after this
     * block started executing. ARM64 icache is not coherent with writes from
     * other cores, so we need ISB to ensure the CPU fetches the latest
     * instructions. Without this, the main thread might execute stale/invalid
     * instructions at the target address, causing crashes.
     * This matches the ISB in emulate.c for the direct T2C call path.
     */
    LLVMTypeRef isb_func_type = LLVMFunctionType(LLVMVoidType(), NULL, 0, 0);
    LLVMValueRef isb_asm =
        LLVMGetInlineAsm(isb_func_type, "isb", 3, "", 0, true, false,
                         LLVMInlineAsmDialectATT, 0);
    LLVMBuildCall2(call_builder, isb_func_type, isb_asm, NULL, 0, "");
#endif
    /* Store cycle count before calling next T2C block - the called block has
     * its own counter and will add to csr_cycle. Must flush our count first.
     */
    T2C_STORE_TIMER(call_builder, start, insn_counter);
    LLVMValueRef t2c_args[1] = {rv_param};
    LLVMBuildCall2(call_builder, t2c_jit_cache_func_type, entry, t2c_args, 1,
                   "");
    LLVMBuildRetVoid(call_builder);

    /* Fallback: seq odd, key mismatch, or seq changed - return to interp */
    LLVMBuildStore(fallback_builder, addr,
                   t2c_gen_PC_addr(start, &fallback_builder, ir));
    T2C_STORE_TIMER(fallback_builder, start, insn_counter);
    LLVMBuildRetVoid(fallback_builder);

    /* Dispose temporary builders to prevent memory leak during T2C compilation.
     * Each builder allocates ~KB of memory; repeated compilations without
     * cleanup can exhaust host memory.
     */
    LLVMDisposeBuilder(seq_even_builder);
    LLVMDisposeBuilder(key_match_builder);
    LLVMDisposeBuilder(call_builder);
    LLVMDisposeBuilder(fallback_builder);
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

    t2c_jit_cache_helper(builder, start, val_rs1, rv, block, ir, insn_counter);
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
        if (ir->branch_taken &&                                             \
            t2c_check_valid_blk(rv, block, ir->branch_taken->pc)) {         \
            *taken_builder = builder2;                                      \
        } else {                                                            \
            T2C_LLVM_GEN_STORE_IMM32(builder2, ir->pc + ir->imm, addr_PC);  \
            T2C_STORE_TIMER(builder2, start, insn_counter);                 \
            LLVMBuildRetVoid(builder2);                                     \
            LLVMDisposeBuilder(builder2);                                   \
        }                                                                   \
        LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken"); \
        LLVMBuilderRef builder3 = LLVMCreateBuilder();                      \
        LLVMPositionBuilderAtEnd(builder3, untaken);                        \
        if (ir->branch_untaken &&                                           \
            t2c_check_valid_blk(rv, block, ir->branch_untaken->pc)) {       \
            *untaken_builder = builder3;                                    \
        } else {                                                            \
            T2C_LLVM_GEN_STORE_IMM32(builder3, ir->pc + 4, addr_PC);        \
            T2C_STORE_TIMER(builder3, start, insn_counter);                 \
            LLVMBuildRetVoid(builder3);                                     \
            LLVMDisposeBuilder(builder3);                                   \
        }                                                                   \
        LLVMBuildCondBr(*builder, cmp, taken, untaken);                     \
    })

BRANCH_FUNC(beq, EQ)
BRANCH_FUNC(bne, NE)
BRANCH_FUNC(blt, SLT)
BRANCH_FUNC(bge, SGE)
BRANCH_FUNC(bltu, ULT)
BRANCH_FUNC(bgeu, UGE)

#if RV32_HAS(SYSTEM)

#include "system.h"

#define t2c_mmu_wrapper(opcode) t2c_mmu_wrapper_##opcode

/* T2C_MMU_LOAD: Generate LLVM IR for MMU load operations.
 * Loads function pointer from rv->io at runtime to avoid ASLR issues.
 * Parameters:
 *   opcode: Instruction name (lb, lh, lw, lbu, lhu)
 *   io_field: Field name in riscv_io_t (mmu_read_b, mmu_read_s, mmu_read_w)
 *   bits: Return value bit width (8, 16, 32)
 *   is_signed: Whether to sign-extend the result
 *
 * OPTIMIZATION NOTE: Each call to t2c_mmu_wrapper_* generates a load of the
 * MMU function pointer from rv->io. For blocks with multiple memory ops of
 * the same type, this creates redundant loads. LLVM's O3 optimization with
 * early-cse (Common Subexpression Elimination) should eliminate these since:
 *   1. rv->io is at a constant offset from the rv parameter
 *   2. The pointer values are invariant during block execution
 *   3. Memory SSA analysis tracks the load dependencies
 * If profiling shows this is still a bottleneck, consider hoisting the function
 * pointer loads to block entry and passing them through a context structure.
 */
#define T2C_MMU_LOAD(opcode, io_field, bits, is_signed)                       \
    static void t2c_mmu_wrapper_##opcode(LLVMBuilderRef *builder,             \
                                         LLVMValueRef start, rv_insn_t *ir)   \
    {                                                                         \
        LLVMValueRef val_rs1 =                                                \
            LLVMBuildLoad2(*builder, LLVMInt32Type(),                         \
                           t2c_gen_rs1_addr(start, builder, ir), "");         \
        LLVMValueRef vaddr = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);   \
        /* MMU read functions: uint##bits##_t fn(riscv_t *rv, uint32_t vaddr) \
         * Use proper 32-bit vaddr type to match C function signature. */     \
        LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),      \
                                     LLVMInt32Type()};                        \
        LLVMTypeRef mmu_fn_type =                                             \
            LLVMFunctionType(LLVMInt##bits##Type(), param_types, 2, 0);       \
        /* Load MMU function pointer from rv->io at runtime.                  \
         * This avoids embedding compile-time addresses that break with ASLR. \
         * Offset = offsetof(riscv_t, io) + offsetof(riscv_io_t, io_field) */ \
        LLVMValueRef rv_param = LLVMGetParam(start, 0);                       \
        LLVMValueRef fn_offset = LLVMConstInt(                                \
            LLVMInt64Type(),                                                  \
            offsetof(riscv_t, io) + offsetof(riscv_io_t, io_field), false);   \
        LLVMValueRef fn_ptr_loc = LLVMBuildInBoundsGEP2(                      \
            *builder, LLVMInt8Type(), rv_param, &fn_offset, 1, "");           \
        LLVMValueRef mmu_fn_ptr = LLVMBuildLoad2(                             \
            *builder, LLVMPointerType(mmu_fn_type, 0), fn_ptr_loc, "");       \
        LLVMValueRef params[] = {rv_param, vaddr};                            \
        LLVMValueRef ret =                                                    \
            LLVMBuildCall2(*builder, mmu_fn_type, mmu_fn_ptr, params, 2, ""); \
        ret =                                                                 \
            LLVMBuildIntCast2(*builder, ret, LLVMInt32Type(), is_signed, ""); \
        LLVMBuildStore(*builder, ret, t2c_gen_rd_addr(start, builder, ir));   \
    }

/* T2C_MMU_STORE: Generate LLVM IR for MMU store operations.
 * Loads function pointer from rv->io at runtime to avoid ASLR issues.
 * Parameters:
 *   opcode: Instruction name (sb, sh, sw)
 *   io_field: Field name in riscv_io_t (mmu_write_b, mmu_write_s, mmu_write_w)
 *   val_bits: Value parameter bit width (8, 16, 32)
 */
#define T2C_MMU_STORE(opcode, io_field, val_bits)                             \
    static void t2c_mmu_wrapper_##opcode(LLVMBuilderRef *builder,             \
                                         LLVMValueRef start, rv_insn_t *ir)   \
    {                                                                         \
        LLVMValueRef val_rs1 =                                                \
            LLVMBuildLoad2(*builder, LLVMInt32Type(),                         \
                           t2c_gen_rs1_addr(start, builder, ir), "");         \
        LLVMValueRef vaddr = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);   \
        /* MMU write functions: void fn(riscv_t *rv, uint32_t vaddr, val)     \
         * Use proper types to match C function signature. */                 \
        LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),      \
                                     LLVMInt32Type(),                         \
                                     LLVMInt##val_bits##Type()};              \
        LLVMTypeRef mmu_fn_type =                                             \
            LLVMFunctionType(LLVMVoidType(), param_types, 3, 0);              \
        /* Load MMU function pointer from rv->io at runtime.                  \
         * This avoids embedding compile-time addresses that break with ASLR. \
         * Offset = offsetof(riscv_t, io) + offsetof(riscv_io_t, io_field) */ \
        LLVMValueRef rv_param = LLVMGetParam(start, 0);                       \
        LLVMValueRef fn_offset = LLVMConstInt(                                \
            LLVMInt64Type(),                                                  \
            offsetof(riscv_t, io) + offsetof(riscv_io_t, io_field), false);   \
        LLVMValueRef fn_ptr_loc = LLVMBuildInBoundsGEP2(                      \
            *builder, LLVMInt8Type(), rv_param, &fn_offset, 1, "");           \
        LLVMValueRef mmu_fn_ptr = LLVMBuildLoad2(                             \
            *builder, LLVMPointerType(mmu_fn_type, 0), fn_ptr_loc, "");       \
        T2C_LLVM_GEN_LOAD_VMREG(rs2, val_bits,                                \
                                t2c_gen_rs2_addr(start, builder, ir));        \
        LLVMValueRef params[] = {rv_param, vaddr, val_rs2};                   \
        LLVMBuildCall2(*builder, mmu_fn_type, mmu_fn_ptr, params, 3, "");     \
    }

T2C_MMU_LOAD(lb, mmu_read_b, 8, true);
T2C_MMU_LOAD(lbu, mmu_read_b, 8, false);
T2C_MMU_LOAD(lh, mmu_read_s, 16, true);
T2C_MMU_LOAD(lhu, mmu_read_s, 16, false);
T2C_MMU_LOAD(lw, mmu_read_w, 32, true);

T2C_MMU_STORE(sb, mmu_write_b, 8);
T2C_MMU_STORE(sh, mmu_write_s, 16);
T2C_MMU_STORE(sw, mmu_write_w, 32);

/* MMU wrapper for clwsp: load word from sp + imm via MMU */
static void t2c_mmu_wrapper_clwsp(LLVMBuilderRef *builder,
                                  LLVMValueRef start,
                                  rv_insn_t *ir)
{
    /* Load sp value (x2) and add immediate offset */
    LLVMValueRef val_sp = LLVMBuildLoad2(
        *builder, LLVMInt32Type(), t2c_gen_sp_addr(start, builder, ir), "");
    LLVMValueRef vaddr = T2C_LLVM_GEN_ALU32_IMM(Add, val_sp, ir->imm);
    /* MMU read: uint32_t fn(riscv_t *rv, uint32_t vaddr) */
    LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),
                                 LLVMInt32Type()};
    LLVMTypeRef mmu_fn_type =
        LLVMFunctionType(LLVMInt32Type(), param_types, 2, 0);
    LLVMValueRef rv_param = LLVMGetParam(start, 0);
    LLVMValueRef fn_offset = LLVMConstInt(
        LLVMInt64Type(),
        offsetof(riscv_t, io) + offsetof(riscv_io_t, mmu_read_w), false);
    LLVMValueRef fn_ptr_loc = LLVMBuildInBoundsGEP2(
        *builder, LLVMInt8Type(), rv_param, &fn_offset, 1, "");
    LLVMValueRef mmu_fn_ptr = LLVMBuildLoad2(
        *builder, LLVMPointerType(mmu_fn_type, 0), fn_ptr_loc, "");
    LLVMValueRef params[] = {rv_param, vaddr};
    LLVMValueRef ret =
        LLVMBuildCall2(*builder, mmu_fn_type, mmu_fn_ptr, params, 2, "");
    LLVMBuildStore(*builder, ret, t2c_gen_rd_addr(start, builder, ir));
}

/* MMU wrapper for cswsp: store word to sp + imm via MMU */
static void t2c_mmu_wrapper_cswsp(LLVMBuilderRef *builder,
                                  LLVMValueRef start,
                                  rv_insn_t *ir)
{
    /* Load sp value (x2) and add immediate offset */
    LLVMValueRef val_sp = LLVMBuildLoad2(
        *builder, LLVMInt32Type(), t2c_gen_sp_addr(start, builder, ir), "");
    LLVMValueRef vaddr = T2C_LLVM_GEN_ALU32_IMM(Add, val_sp, ir->imm);
    /* MMU write: void fn(riscv_t *rv, uint32_t vaddr, uint32_t val) */
    LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),
                                 LLVMInt32Type(), LLVMInt32Type()};
    LLVMTypeRef mmu_fn_type =
        LLVMFunctionType(LLVMVoidType(), param_types, 3, 0);
    LLVMValueRef rv_param = LLVMGetParam(start, 0);
    LLVMValueRef fn_offset = LLVMConstInt(
        LLVMInt64Type(),
        offsetof(riscv_t, io) + offsetof(riscv_io_t, mmu_write_w), false);
    LLVMValueRef fn_ptr_loc = LLVMBuildInBoundsGEP2(
        *builder, LLVMInt8Type(), rv_param, &fn_offset, 1, "");
    LLVMValueRef mmu_fn_ptr = LLVMBuildLoad2(
        *builder, LLVMPointerType(mmu_fn_type, 0), fn_ptr_loc, "");
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef params[] = {rv_param, vaddr, val_rs2};
    LLVMBuildCall2(*builder, mmu_fn_type, mmu_fn_ptr, params, 3, "");
}

/* MMU wrapper for fuse9: LUI+LW absolute address load
 * addr = ir->imm + ir->imm2, dest = ir->rs2 (not rd!)
 */
static void t2c_mmu_wrapper_fuse9(LLVMBuilderRef *builder,
                                  LLVMValueRef start,
                                  rv_insn_t *ir)
{
    uint32_t addr_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    LLVMValueRef vaddr = LLVMConstInt(LLVMInt32Type(), addr_imm, false);
    LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),
                                 LLVMInt32Type()};
    LLVMTypeRef mmu_fn_type =
        LLVMFunctionType(LLVMInt32Type(), param_types, 2, 0);
    LLVMValueRef rv_param = LLVMGetParam(start, 0);
    LLVMValueRef fn_offset = LLVMConstInt(
        LLVMInt64Type(),
        offsetof(riscv_t, io) + offsetof(riscv_io_t, mmu_read_w), false);
    LLVMValueRef fn_ptr_loc = LLVMBuildInBoundsGEP2(
        *builder, LLVMInt8Type(), rv_param, &fn_offset, 1, "");
    LLVMValueRef mmu_fn_ptr = LLVMBuildLoad2(
        *builder, LLVMPointerType(mmu_fn_type, 0), fn_ptr_loc, "");
    LLVMValueRef params[] = {rv_param, vaddr};
    LLVMValueRef ret =
        LLVMBuildCall2(*builder, mmu_fn_type, mmu_fn_ptr, params, 2, "");
    /* fuse9 uses rs2 as destination, not rd */
    LLVMBuildStore(*builder, ret, t2c_gen_rs2_addr(start, builder, ir));
}

/* MMU wrapper for fuse10: LUI+SW absolute address store
 * addr = ir->imm + ir->imm2, source = ir->rs1
 */
static void t2c_mmu_wrapper_fuse10(LLVMBuilderRef *builder,
                                   LLVMValueRef start,
                                   rv_insn_t *ir)
{
    uint32_t addr_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    LLVMValueRef vaddr = LLVMConstInt(LLVMInt32Type(), addr_imm, false);
    LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),
                                 LLVMInt32Type(), LLVMInt32Type()};
    LLVMTypeRef mmu_fn_type =
        LLVMFunctionType(LLVMVoidType(), param_types, 3, 0);
    LLVMValueRef rv_param = LLVMGetParam(start, 0);
    LLVMValueRef fn_offset = LLVMConstInt(
        LLVMInt64Type(),
        offsetof(riscv_t, io) + offsetof(riscv_io_t, mmu_write_w), false);
    LLVMValueRef fn_ptr_loc = LLVMBuildInBoundsGEP2(
        *builder, LLVMInt8Type(), rv_param, &fn_offset, 1, "");
    LLVMValueRef mmu_fn_ptr = LLVMBuildLoad2(
        *builder, LLVMPointerType(mmu_fn_type, 0), fn_ptr_loc, "");
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef params[] = {rv_param, vaddr, val_rs1};
    LLVMBuildCall2(*builder, mmu_fn_type, mmu_fn_ptr, params, 3, "");
}

/* MMU wrapper for fuse11: LW+ADDI post-increment load
 * addr = X[rs1] + imm, dest = rd, then X[rs1] += imm2
 */
static void t2c_mmu_wrapper_fuse11(LLVMBuilderRef *builder,
                                   LLVMValueRef start,
                                   rv_insn_t *ir)
{
    LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
    LLVMValueRef vaddr = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);
    /* MMU read */
    LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),
                                 LLVMInt32Type()};
    LLVMTypeRef mmu_fn_type =
        LLVMFunctionType(LLVMInt32Type(), param_types, 2, 0);
    LLVMValueRef rv_param = LLVMGetParam(start, 0);
    LLVMValueRef fn_offset = LLVMConstInt(
        LLVMInt64Type(),
        offsetof(riscv_t, io) + offsetof(riscv_io_t, mmu_read_w), false);
    LLVMValueRef fn_ptr_loc = LLVMBuildInBoundsGEP2(
        *builder, LLVMInt8Type(), rv_param, &fn_offset, 1, "");
    LLVMValueRef mmu_fn_ptr = LLVMBuildLoad2(
        *builder, LLVMPointerType(mmu_fn_type, 0), fn_ptr_loc, "");
    LLVMValueRef params[] = {rv_param, vaddr};
    LLVMValueRef ret =
        LLVMBuildCall2(*builder, mmu_fn_type, mmu_fn_ptr, params, 2, "");
    LLVMBuildStore(*builder, ret, t2c_gen_rd_addr(start, builder, ir));
    /* Post-increment rs1 by imm2 */
    LLVMValueRef inc = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm2);
    LLVMBuildStore(*builder, inc, addr_rs1);
}

#endif

T2C_OP(lb, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lb)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            LLVMValueRef res = LLVMBuildSExt(
                *builder,
                LLVMBuildLoad2(*builder, LLVMInt8Type(), mem_loc, "res"),
                LLVMInt32Type(), "sext8to32");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
        });
})

T2C_OP(lh, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lh)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            LLVMValueRef res = LLVMBuildSExt(
                *builder,
                LLVMBuildLoad2(*builder, LLVMInt16Type(), mem_loc, "res"),
                LLVMInt32Type(), "sext16to32");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
        });
})


T2C_OP(lw, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lw)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            LLVMValueRef res =
                LLVMBuildLoad2(*builder, LLVMInt32Type(), mem_loc, "res");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
        });
})

T2C_OP(lbu, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lbu)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            LLVMValueRef res = LLVMBuildZExt(
                *builder,
                LLVMBuildLoad2(*builder, LLVMInt8Type(), mem_loc, "res"),
                LLVMInt32Type(), "zext8to32");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
        });
})

T2C_OP(lhu, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lhu)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            LLVMValueRef res = LLVMBuildZExt(
                *builder,
                LLVMBuildLoad2(*builder, LLVMInt16Type(), mem_loc, "res"),
                LLVMInt32Type(), "zext16to32");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
        });
})

T2C_OP(sb, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sb)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 8,
                                    t2c_gen_rs2_addr(start, builder, ir));
            LLVMBuildStore(*builder, val_rs2, mem_loc);
        });
})

T2C_OP(sh, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sh)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 16,
                                    t2c_gen_rs2_addr(start, builder, ir));
            LLVMBuildStore(*builder, val_rs2, mem_loc);
        });
})

T2C_OP(sw, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sw)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 32,
                                    t2c_gen_rs2_addr(start, builder, ir));
            LLVMBuildStore(*builder, val_rs2, mem_loc);
        });
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
    T2C_STORE_TIMER(*builder, start, insn_counter);
    LLVMBuildRetVoid(*builder);
})

T2C_OP(ebreak, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    t2c_gen_call_io_func(start, builder, param_types, 9);
    T2C_STORE_TIMER(*builder, start, insn_counter);
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
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lw)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            LLVMValueRef res =
                LLVMBuildLoad2(*builder, LLVMInt32Type(), mem_loc, "res");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
        });
})

T2C_OP(csw, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sw)(builder, start, ir); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 32,
                                    t2c_gen_rs2_addr(start, builder, ir));
            LLVMBuildStore(*builder, val_rs2, mem_loc);
        });
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
        T2C_STORE_TIMER(*builder, start, insn_counter);
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
        T2C_STORE_TIMER(*builder, start, insn_counter);
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
        T2C_STORE_TIMER(builder2, start, insn_counter);
        LLVMBuildRetVoid(builder2);
    }

    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken)
        *untaken_builder = builder3;
    else {
        T2C_LLVM_GEN_STORE_IMM32(builder3, ir->pc + 2, addr_PC);
        T2C_STORE_TIMER(builder3, start, insn_counter);
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
        T2C_STORE_TIMER(builder2, start, insn_counter);
        LLVMBuildRetVoid(builder2);
    }

    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken)
        *untaken_builder = builder3;
    else {
        T2C_LLVM_GEN_STORE_IMM32(builder3, ir->pc + 2, addr_PC);
        T2C_STORE_TIMER(builder3, start, insn_counter);
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
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper_clwsp(builder, start, ir); },
        {
            LLVMValueRef val_sp = LLVMBuildZExt(
                *builder,
                LLVMBuildLoad2(*builder, LLVMInt32Type(),
                               t2c_gen_sp_addr(start, builder, ir), "val_sp"),
                LLVMInt64Type(), "zext32to64");
            LLVMValueRef addr = LLVMBuildAdd(
                *builder, val_sp,
                LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true),
                "addr");
            LLVMValueRef cast_addr = LLVMBuildIntToPtr(
                *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
            LLVMValueRef res =
                LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
        });
})

T2C_OP(cjr, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    t2c_jit_cache_helper(builder, start, val_rs1, rv, block, ir, insn_counter);
})

T2C_OP(cmv, {
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMBuildStore(*builder, val_rs2, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cebreak, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    t2c_gen_call_io_func(start, builder, param_types, 9);
    T2C_STORE_TIMER(*builder, start, insn_counter);
    LLVMBuildRetVoid(*builder);
})

T2C_OP(cjalr, {
    /* The register which stores the indirect address needs to be loaded first
     * to avoid being overriden by other operation.
     */
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 2,
                             t2c_gen_ra_addr(start, builder, ir));
    t2c_jit_cache_helper(builder, start, val_rs1, rv, block, ir, insn_counter);
})

T2C_OP(cadd, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cswsp, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper_cswsp(builder, start, ir); },
        {
            LLVMValueRef addr_rs2 = t2c_gen_rs2_addr(start, builder, ir);
            LLVMValueRef val_sp = LLVMBuildZExt(
                *builder,
                LLVMBuildLoad2(*builder, LLVMInt32Type(),
                               t2c_gen_sp_addr(start, builder, ir), "val_sp"),
                LLVMInt64Type(), "zext32to64");
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, addr_rs2);
            LLVMValueRef addr = LLVMBuildAdd(
                *builder, val_sp,
                LLVMConstInt(LLVMInt64Type(), ir->imm + mem_base, true),
                "addr");
            LLVMValueRef cast_addr = LLVMBuildIntToPtr(
                *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
            LLVMBuildStore(*builder, val_rs2, cast_addr);
        });
})
#endif

#if RV32_HAS(EXT_C) && RV32_HAS(EXT_F)
T2C_OP(cflwsp, { __UNREACHABLE; })

T2C_OP(cfswsp, { __UNREACHABLE; })

T2C_OP(cflw, { __UNREACHABLE; })

T2C_OP(cfsw, { __UNREACHABLE; })
#endif

#if RV32_HAS(Zba)
T2C_OP(sh1add, { __UNREACHABLE; })

T2C_OP(sh2add, { __UNREACHABLE; })

T2C_OP(sh3add, { __UNREACHABLE; })
#endif

#if RV32_HAS(Zbb)
T2C_OP(andn, { __UNREACHABLE; })

T2C_OP(orn, { __UNREACHABLE; })

T2C_OP(xnor, { __UNREACHABLE; })

T2C_OP(clz, { __UNREACHABLE; })

T2C_OP(ctz, { __UNREACHABLE; })

T2C_OP(cpop, { __UNREACHABLE; })

T2C_OP(max, { __UNREACHABLE; })

T2C_OP(maxu, { __UNREACHABLE; })

T2C_OP(min, { __UNREACHABLE; })

T2C_OP(minu, { __UNREACHABLE; })

T2C_OP(sextb, { __UNREACHABLE; })

T2C_OP(sexth, { __UNREACHABLE; })

T2C_OP(zexth, { __UNREACHABLE; })

T2C_OP(rol, { __UNREACHABLE; })

T2C_OP(ror, { __UNREACHABLE; })

T2C_OP(rori, { __UNREACHABLE; })

T2C_OP(orcb, { __UNREACHABLE; })

T2C_OP(rev8, { __UNREACHABLE; })
#endif

#if RV32_HAS(Zbc)
T2C_OP(clmul, { __UNREACHABLE; })

T2C_OP(clmulh, { __UNREACHABLE; })

T2C_OP(clmulr, { __UNREACHABLE; })
#endif

#if RV32_HAS(Zbs)
T2C_OP(bclr, { __UNREACHABLE; })

T2C_OP(bclri, { __UNREACHABLE; })

T2C_OP(bext, { __UNREACHABLE; })

T2C_OP(bexti, { __UNREACHABLE; })

T2C_OP(binv, { __UNREACHABLE; })

T2C_OP(binvi, { __UNREACHABLE; })

T2C_OP(bset, { __UNREACHABLE; })

T2C_OP(bseti, { __UNREACHABLE; })
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
        IIF(RV32_HAS(SYSTEM))(
            { t2c_mmu_wrapper(sw)(builder, start, (rv_insn_t *) (&fuse[i])); },
            {
                LLVMValueRef mem_loc = t2c_gen_mem_loc(
                    start, builder, (rv_insn_t *) (&fuse[i]), mem_base);
                T2C_LLVM_GEN_LOAD_VMREG(
                    rs2, 32,
                    t2c_gen_rs2_addr(start, builder, (rv_insn_t *) (&fuse[i])));
                LLVMBuildStore(*builder, val_rs2, mem_loc);
            });
    }
})

T2C_OP(fuse4, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        IIF(RV32_HAS(SYSTEM))(
            { t2c_mmu_wrapper(lw)(builder, start, (rv_insn_t *) (&fuse[i])); },
            {
                LLVMValueRef mem_loc = t2c_gen_mem_loc(
                    start, builder, (rv_insn_t *) (&fuse[i]), mem_base);
                LLVMValueRef res =
                    LLVMBuildLoad2(*builder, LLVMInt32Type(), mem_loc, "res");
                LLVMBuildStore(
                    *builder, res,
                    t2c_gen_rd_addr(start, builder, (rv_insn_t *) (&fuse[i])));
            });
    }
})

T2C_OP(fuse5, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            t2c_slli(builder, param_types, start, entry, taken_builder,
                     untaken_builder, rv, mem_base, block,
                     (rv_insn_t *) (&fuse[i]), insn_counter);
            break;
        case rv_insn_srli:
            t2c_srli(builder, param_types, start, entry, taken_builder,
                     untaken_builder, rv, mem_base, block,
                     (rv_insn_t *) (&fuse[i]), insn_counter);
            break;
        case rv_insn_srai:
            t2c_srai(builder, param_types, start, entry, taken_builder,
                     untaken_builder, rv, mem_base, block,
                     (rv_insn_t *) (&fuse[i]), insn_counter);
            break;
        default:
            __UNREACHABLE;
            break;
        }
    }
})

/* fused LI a7, imm + ECALL
 * This fusion is only available in standard RV32I/M/A/F/C since RV32E
 * uses a different syscall convention (t0 instead of a7).
 */
#if !RV32_HAS(RV32E)
T2C_OP(fuse6, {
    /* Store syscall number (imm) to a7 register */
    LLVMValueRef a7_offset = LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + rv_reg_a7, true);
    LLVMValueRef addr_a7 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              &a7_offset, 1, "addr_a7");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), ir->imm, true),
                   addr_a7);
    /* Store PC and call ecall handler.
     * ECALL is at ir->pc + 4 (second instruction in fused pair).
     */
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 4,
                             t2c_gen_PC_addr(start, builder, ir));
    t2c_gen_call_io_func(start, builder, param_types, 8);
    T2C_STORE_TIMER(*builder, start, insn_counter);
    LLVMBuildRetVoid(*builder);
})
#else
/* RV32E stub: fuse6 pattern is never generated for RV32E.
 * Defensive fallback - return void if unexpectedly reached.
 */
T2C_OP(fuse6, {
    assert(!"fuse6 should not be called in RV32E mode");
    T2C_STORE_TIMER(*builder, start, insn_counter);
    LLVMBuildRetVoid(*builder);
})
#endif

/* fused multiple ADDI */
T2C_OP(fuse7, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        LLVMValueRef rs1_offset = LLVMConstInt(
            LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + fuse[i].rs1,
            true);
        LLVMValueRef addr_rs1 = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32Type(), LLVMGetParam(start, 0), &rs1_offset, 1,
            "addr_rs1");
        LLVMValueRef val_rs1 =
            LLVMBuildLoad2(*builder, LLVMInt32Type(), addr_rs1, "val_rs1");
        LLVMValueRef res = LLVMBuildAdd(
            *builder, val_rs1, LLVMConstInt(LLVMInt32Type(), fuse[i].imm, true),
            "add");
        LLVMValueRef rd_offset =
            LLVMConstInt(LLVMInt32Type(),
                         offsetof(riscv_t, X) / sizeof(int) + fuse[i].rd, true);
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),
                                                     LLVMGetParam(start, 0),
                                                     &rd_offset, 1, "addr_rd");
        LLVMBuildStore(*builder, res, addr_rd);
    }
})

/* fused LUI + ADDI: 32-bit constant load (li pseudo-op)
 * rd = (lui_imm << 12) + addi_imm = ir->imm + ir->imm2
 */
T2C_OP(fuse8, {
    /* Compute combined immediate and store to rd.
     * Cast to uint32_t to avoid signed overflow UB.
     */
    uint32_t combined_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    LLVMValueRef rd_offset = LLVMConstInt(
        LLVMInt32Type(), offsetof(riscv_t, X) / sizeof(int) + ir->rd, true);
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(), LLVMGetParam(start, 0),
                              &rd_offset, 1, "addr_rd");
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), combined_imm, true),
                   addr_rd);
})

/* fused LUI + LW: absolute address load
 * addr = ir->imm (lui << 12) + ir->imm2 (lw offset)
 * ir->rs2 = destination register for load
 */
T2C_OP(fuse9, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper_fuse9(builder, start, ir); },
        {
            uint32_t addr_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
            LLVMValueRef addr = LLVMConstInt(
                LLVMInt64Type(), (uint64_t) addr_imm + mem_base, false);
            LLVMValueRef cast_addr = LLVMBuildIntToPtr(
                *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
            LLVMValueRef res =
                LLVMBuildLoad2(*builder, LLVMInt32Type(), cast_addr, "res");
            LLVMBuildStore(*builder, res, t2c_gen_rs2_addr(start, builder, ir));
        });
})

/* fused LUI + SW: absolute address store
 * addr = ir->imm (lui << 12) + ir->imm2 (sw offset)
 * ir->rs1 = source register for store
 */
T2C_OP(fuse10, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper_fuse10(builder, start, ir); },
        {
            uint32_t addr_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
            LLVMValueRef addr = LLVMConstInt(
                LLVMInt64Type(), (uint64_t) addr_imm + mem_base, false);
            LLVMValueRef cast_addr = LLVMBuildIntToPtr(
                *builder, addr, LLVMPointerType(LLVMInt32Type(), 0), "cast");
            T2C_LLVM_GEN_LOAD_VMREG(rs1, 32,
                                    t2c_gen_rs1_addr(start, builder, ir));
            LLVMBuildStore(*builder, val_rs1, cast_addr);
        });
})

/* fused LW + ADDI (post-increment load)
 * addr = rv->X[ir->rs1] + ir->imm
 * ir->rd = load destination
 * ir->rs1 += ir->imm2 (increment)
 *
 * Note: Pattern matching in match_pattern() requires rd != rs1 to avoid
 * clobbering the base register before use in the increment. This lets us
 * safely use the original rs1 value for the post-increment.
 */
T2C_OP(fuse11, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper_fuse11(builder, start, ir); },
        {
            LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
            T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
            /* Compute address and load */
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, mem_base);
            LLVMValueRef res =
                LLVMBuildLoad2(*builder, LLVMInt32Type(), mem_loc, "res");
            LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
            /* Increment rs1 by imm2 (rd != rs1 guaranteed by fusion constraint)
             */
            LLVMValueRef inc_val =
                T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm2);
            LLVMBuildStore(*builder, inc_val, addr_rs1);
        });
})

/* fused ADDI + BNE (loop counter decrement-branch)
 * rd = rs1 + imm
 * if rd != 0, branch to PC + 4 + imm2
 */
T2C_OP(fuse12, {
    LLVMValueRef addr_PC = t2c_gen_PC_addr(start, builder, ir);
    /* Compute rd = rs1 + imm */
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
    /* Compare rd with 0 */
    T2C_LLVM_GEN_CMP_IMM32(NE, res, 0);
    /* Create taken and untaken branches */
    LLVMBasicBlockRef taken = LLVMAppendBasicBlock(start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (ir->branch_taken &&
        t2c_check_valid_blk(rv, block, ir->branch_taken->pc)) {
        *taken_builder = builder2;
    } else {
        /* PC = ir->pc + 4 + ir->imm2 (ADDI is 4 bytes, then branch offset) */
        T2C_LLVM_GEN_STORE_IMM32(builder2, ir->pc + 4 + ir->imm2, addr_PC);
        T2C_STORE_TIMER(builder2, start, insn_counter);
        LLVMBuildRetVoid(builder2);
    }
    LLVMBasicBlockRef untaken = LLVMAppendBasicBlock(start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (ir->branch_untaken &&
        t2c_check_valid_blk(rv, block, ir->branch_untaken->pc)) {
        *untaken_builder = builder3;
    } else {
        /* PC = ir->pc + 8 (skip both ADDI and BNE, each 4 bytes) */
        T2C_LLVM_GEN_STORE_IMM32(builder3, ir->pc + 8, addr_PC);
        T2C_STORE_TIMER(builder3, start, insn_counter);
        LLVMBuildRetVoid(builder3);
    }
    LLVMBuildCondBr(*builder, cmp, taken, untaken);
})
