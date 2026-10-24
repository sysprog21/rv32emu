/* Lower custom IR to relocatable LLVM bitcode for offline compilation. */

/* Raise the misaligned-access exception for insn's rs1 + imm, as the
 * interpreter does; see t2c_gen_misalign_guard().
 */
#define T2C_MISALIGN_GUARD(insn, size, flags, pc)                             \
    t2c_gen_misalign_guard(builder, start, rv,                                \
                           t2c_gen_vaddr(start, builder, (rv_insn_t *) insn), \
                           size, flags, pc, insn_counter)

T2C_OP(nop, { return; })

T2C_OP(lui, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(auipc, {
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
})

/* The block at pc if a region may continue into it, or NULL. */
static block_t *t2c_check_valid_blk(riscv_t *rv UNUSED,
                                    block_t *block UNUSED,
                                    uint32_t pc)
{
    /* Entering libc exit changes host syscall policy in rv_step(). Preserve
     * that dispatcher boundary even when a larger AOT region reaches it.
     */
#if !RV32_HAS(SYSTEM)
    if (PRIV(rv)->exit_addr && pc == PRIV(rv)->exit_addr)
        return NULL;
#else
    if ((pc ^ block->pc_start) & ~(RV_PG_SIZE - 1))
        return NULL;
#endif
#if RV32_HAS(SYSTEM)
    block_t *blk = t2c_page_blocks[(pc & (RV_PG_SIZE - 1)) / 2];
#else
    block_t *blk = cache_get(rv->block_cache, pc, false);
#endif
    if (!blk || !blk->aot_translatable)
        return NULL;

#if RV32_HAS(SYSTEM)
    /* Offline graphs are keyed by complete page contents. A translation
     * flush does not prevent grouping IR with the same recorded fingerprint.
     */
    if (blk->satp != block->satp || blk->priv_mode != block->priv_mode ||
        blk->aot_page_hash != block->aot_page_hash)
        return NULL;
#endif

    return blk;
}

/* Offline system graphs know direct targets independently of the interpreter's
 * ephemeral edge links, which reclamation deliberately removes. User traces
 * retain their observed-edge policy and libc-exit dispatcher boundary.
 */
static bool t2c_has_direct_target(riscv_t *rv,
                                  block_t *block,
                                  const rv_insn_t *edge UNUSED,
                                  uint32_t pc UNUSED)
{
#if RV32_HAS(SYSTEM)
    return t2c_check_valid_blk(rv, block, pc) != NULL;
#else
    /* User traces follow observed edges, and unobserved ones into blocks the
     * region already holds or T1C compiled as hot: T1C runs such edges
     * natively, so the interpreter never links them. Other code would only
     * enlarge the region and hinder its register promotion.
     */
    if (edge)
        return t2c_check_valid_blk(rv, block, edge->pc) != NULL;
    const block_t *blk = t2c_check_valid_blk(rv, block, pc);
    return blk && (blk->hot || set_has(t2c_region_set, pc));
#endif
}

/* Record the return point of a call, which links ra, for the region's return
 * switch.
 */
static inline void t2c_note_call(bool call UNUSED, uint32_t return_pc UNUSED)
{
#if !RV32_HAS(SYSTEM)
    if (call && t2c_n_returns < T2C_MAX_RETURNS)
        t2c_return_pcs[t2c_n_returns++] = return_pc;
#endif
}

T2C_OP(jal, {
    t2c_note_call(ir->rd == rv_reg_ra, ir->pc + 4);
    if (ir->rd)
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 4,
                                 t2c_gen_rd_addr(start, builder, ir));

    if (t2c_has_direct_target(rv, block, ir->branch_taken, ir->pc + ir->imm)) {
        *taken_builder = *builder;
    } else {
        t2c_gen_exit(*builder, start, ir->pc + ir->imm, insn_counter);
    }
})

/* Jump to the compiled region fn with a guaranteed tail call: the target runs
 * in this frame, so a chain of indirect jumps neither grows the host stack nor
 * pairs host calls with returns.
 */
static void t2c_gen_tail_jump(LLVMBuilderRef builder,
                              LLVMValueRef fn,
                              LLVMValueRef rv_param)
{
    LLVMValueRef call =
        LLVMBuildCall2(builder, t2c_jit_cache_func_type, fn, &rv_param, 1, "");
    LLVMSetInstructionCallConv(call, T2C_CALL_CONV);
    LLVMSetTailCallKind(call, LLVMTailCallKindMustTail);
    LLVMBuildRetVoid(builder);
}

FORCE_INLINE void t2c_jit_cache_helper(LLVMBuilderRef *builder,
                                       LLVMValueRef start,
                                       LLVMValueRef addr,
                                       riscv_t *rv UNUSED,
                                       block_t *block UNUSED,
                                       rv_insn_t *ir,
                                       LLVMValueRef insn_counter)
{
    /* Inline caching + seqlock pattern for indirect jump resolution.
     *
     * Fast path (inline cache hit):
     *   1. Load cached key from inline_cache[hash]
     *   2. Compare with target address - if match, call cached entry directly
     *   3. No ISB needed on ARM64 - target was already executed successfully
     *
     * Slow path (inline cache miss -> jit_cache lookup):
     *   1. Load seq1 (acquire), if odd (write in progress) -> fallback
     *   2. Load key (acquire), compare with expected -> fallback on mismatch
     *   3. Load entry (acquire), ensuring all data loads complete
     *   4. Load seq2 (monotonic), if seq1 != seq2 or entry == NULL -> fallback
     *   5. Update inline cache with (key, entry) for next time
     *   6. ISB on ARM64 (new block), call entry
     *
     * Inline cache provides ~90% hit rate for stable branch patterns (returns,
     * virtual calls). Removes seqlock overhead and ISB for hot paths.
     */
    LLVMValueRef rv_param = LLVMGetParam(start, 0);

    /* Compute expected key once - used by both inline cache and jit_cache */
#if RV32_HAS(SYSTEM)
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    /* Entries also record the fetch privilege they were bound under. */
    LLVMValueRef priv = LLVMBuildLoad2(
        *builder, i32,
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, priv_mode), i32),
        "priv");
    /* An artifact serves every run, so the address space is read at run
     * time. Kernel code reached through global mappings is bound once for
     * every address space, under SATP_SHARED, so a kernel graph looks its
     * targets up there while S-mode paging is on, once the guest has shown it
     * maps its kernel globally. The dispatcher finds the rest.
     */
    LLVMValueRef satp_early = LLVMBuildLoad2(
        *builder, i32,
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, csr_satp), i32),
        "");
    if (!t2c_user_graph) {
        LLVMTypeRef i8 = LLVMInt8TypeInContext(t2c_context);
        LLVMValueRef shared = LLVMBuildLoad2(
            *builder, i8,
            t2c_gen_rv_field_ptr(start, builder,
                                 offsetof(riscv_t, aot_kernel_shared), i8),
            "");
        LLVMValueRef use_shared = LLVMBuildAnd(
            *builder,
            LLVMBuildAnd(
                *builder,
                LLVMBuildICmp(*builder, LLVMIntSLT, satp_early,
                              LLVMConstInt(i32, 0, false), ""),
                LLVMBuildICmp(*builder, LLVMIntEQ, priv,
                              LLVMConstInt(i32, RV_PRIV_S_MODE, false), ""),
                ""),
            LLVMBuildICmp(*builder, LLVMIntNE, shared,
                          LLVMConstInt(i8, 0, false), ""),
            "");
        satp_early = LLVMBuildSelect(*builder, use_shared,
                                     LLVMConstInt(i32, SATP_SHARED, false),
                                     satp_early, "space");
    }
    LLVMValueRef addr64 = LLVMBuildIntCast2(
        *builder, addr, LLVMInt64TypeInContext(t2c_context), false, "");
    LLVMValueRef satp64 = LLVMBuildIntCast2(
        *builder, satp_early, LLVMInt64TypeInContext(t2c_context), false, "");
    LLVMValueRef satp_shifted = LLVMBuildShl(
        *builder, satp64,
        LLVMConstInt(LLVMInt64TypeInContext(t2c_context), 32, false), "");
    LLVMValueRef expected_key =
        LLVMBuildAdd(*builder, addr64, satp_shifted, "expected_key");
#else
    LLVMValueRef expected_key =
        LLVMBuildIntCast2(*builder, addr, LLVMInt64TypeInContext(t2c_context),
                          false, "expected_key");
#endif

    /* === INLINE CACHE FAST PATH === */

    /* Load inline_cache base address */
    LLVMValueRef ic_ptr =
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, inline_cache),
                             LLVMPointerType(t2c_inline_cache_struct_type, 0));
    LLVMValueRef ic_base = LLVMBuildLoad2(
        *builder, LLVMPointerType(t2c_inline_cache_struct_type, 0), ic_ptr, "");

    /* Compute inline cache index; must match inline_cache_slot(), which
     * evictions use to find the one slot a key can occupy. */
    LLVMValueRef ic_addr_high = LLVMBuildLShr(
        *builder, addr,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 12, false), "");
    LLVMValueRef ic_addr_mixed = LLVMBuildXor(*builder, addr, ic_addr_high, "");
#if RV32_HAS(SYSTEM)
    LLVMValueRef ic_hash_xor =
        LLVMBuildXor(*builder, ic_addr_mixed, satp_early, "");
    LLVMValueRef ic_hash =
        LLVMBuildAnd(*builder, ic_hash_xor,
                     LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                  N_INLINE_CACHE_ENTRIES - 1, false),
                     "");
#else
    LLVMValueRef ic_hash =
        LLVMBuildAnd(*builder, ic_addr_mixed,
                     LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                  N_INLINE_CACHE_ENTRIES - 1, false),
                     "");
#endif

    /* Get inline cache element pointer */
    LLVMValueRef ic_idx = LLVMBuildIntCast2(
        *builder, ic_hash, LLVMInt64TypeInContext(t2c_context), false, "");
    LLVMValueRef ic_element_ptr = LLVMBuildInBoundsGEP2(
        *builder, t2c_inline_cache_struct_type, ic_base, &ic_idx, 1, "");

    /* Load cached key and entry from inline cache */
    LLVMValueRef ic_key_ptr = LLVMBuildStructGEP2(
        *builder, t2c_inline_cache_struct_type, ic_element_ptr, 0, "");
    LLVMValueRef ic_key = LLVMBuildLoad2(
        *builder, LLVMInt64TypeInContext(t2c_context), ic_key_ptr, "ic_key");

    LLVMValueRef ic_entry_ptr = LLVMBuildStructGEP2(
        *builder, t2c_inline_cache_struct_type, ic_element_ptr, 1, "");
    LLVMValueRef ic_entry = LLVMBuildLoad2(
        *builder, LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        ic_entry_ptr, "ic_entry");

    /* Check if inline cache hit: key matches AND entry is not NULL */
    LLVMValueRef ic_key_match =
        LLVMBuildICmp(*builder, LLVMIntEQ, ic_key, expected_key, "");
    LLVMValueRef ic_entry_valid = LLVMBuildIsNotNull(*builder, ic_entry, "");
    LLVMValueRef ic_hit =
        LLVMBuildAnd(*builder, ic_key_match, ic_entry_valid, "ic_hit");
#if RV32_HAS(SYSTEM)
    LLVMValueRef ic_priv_ptr = LLVMBuildStructGEP2(
        *builder, t2c_inline_cache_struct_type, ic_element_ptr, 2, "");
    LLVMValueRef ic_priv = LLVMBuildLoad2(
        *builder, LLVMInt32TypeInContext(t2c_context), ic_priv_ptr, "ic_priv");
    ic_hit = LLVMBuildAnd(*builder, ic_hit,
                          LLVMBuildICmp(*builder, LLVMIntEQ, ic_priv, priv, ""),
                          "ic_context_hit");
#endif

    /* Create basic blocks for inline cache hit and miss paths */
    LLVMBasicBlockRef ic_hit_block =
        LLVMAppendBasicBlockInContext(t2c_context, start, "ic_hit");
    LLVMBuilderRef ic_hit_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(ic_hit_builder, ic_hit_block);

    LLVMBasicBlockRef ic_miss_block =
        LLVMAppendBasicBlockInContext(t2c_context, start, "ic_miss");
    LLVMBuilderRef ic_miss_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(ic_miss_builder, ic_miss_block);

    LLVMBuildCondBr(*builder, ic_hit, ic_hit_block, ic_miss_block);

    /* === INLINE CACHE HIT PATH (fast) ===
     * No ISB needed - we already executed this target successfully before.
     * The instruction cache was coherent at that time.
     */
    LLVMBuildStore(ic_hit_builder, addr,
                   t2c_gen_PC_addr(start, &ic_hit_builder, ir));
    T2C_STORE_TIMER(ic_hit_builder, start, insn_counter);
    t2c_gen_tail_jump(ic_hit_builder, ic_entry, rv_param);

    /* === INLINE CACHE MISS PATH (slow - use seqlock jit_cache) === */

    LLVMBasicBlockRef seq_even =
        LLVMAppendBasicBlockInContext(t2c_context, start, "seq_even");
    LLVMBuilderRef seq_even_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(seq_even_builder, seq_even);

    LLVMBasicBlockRef key_match =
        LLVMAppendBasicBlockInContext(t2c_context, start, "key_match");
    LLVMBuilderRef key_match_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(key_match_builder, key_match);

    LLVMBasicBlockRef call_jit =
        LLVMAppendBasicBlockInContext(t2c_context, start, "call_jit");
    LLVMBuilderRef call_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(call_builder, call_jit);

    LLVMBasicBlockRef fallback =
        LLVMAppendBasicBlockInContext(t2c_context, start, "fallback");
    LLVMBuilderRef fallback_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(fallback_builder, fallback);

    /* Load jit_cache base address */
    LLVMValueRef jit_cache_ptr = t2c_gen_rv_field_ptr(
        start, &ic_miss_builder, offsetof(riscv_t, jit_cache),
        LLVMPointerType(t2c_jit_cache_struct_type, 0));
    LLVMValueRef base = LLVMBuildLoad2(
        ic_miss_builder, LLVMPointerType(t2c_jit_cache_struct_type, 0),
        jit_cache_ptr, "");

    /* Compute jit_cache index; must match jit_cache_slot() */
#if RV32_HAS(SYSTEM)
    LLVMValueRef hash_xor = LLVMBuildXor(ic_miss_builder, addr, satp_early, "");
    LLVMValueRef product =
        LLVMBuildMul(ic_miss_builder, hash_xor,
                     LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                  JIT_CACHE_HASH_MULTIPLIER, false),
                     "");
    LLVMValueRef hash =
        LLVMBuildLShr(ic_miss_builder, product,
                      LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                   32 - JIT_CACHE_INDEX_BITS, false),
                      "");
#else
    LLVMValueRef addr_high = LLVMBuildLShr(
        ic_miss_builder, addr,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 12, false), "");
    LLVMValueRef addr_mixed =
        LLVMBuildXor(ic_miss_builder, addr, addr_high, "");
    LLVMValueRef hash =
        LLVMBuildAnd(ic_miss_builder, addr_mixed,
                     LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                  N_JIT_CACHE_ENTRIES - 1, false),
                     "");
#endif

    /* Get jit_cache element pointer */
    LLVMValueRef cast = LLVMBuildIntCast2(
        ic_miss_builder, hash, LLVMInt64TypeInContext(t2c_context), false, "");
    LLVMValueRef element_ptr = LLVMBuildInBoundsGEP2(
        ic_miss_builder, t2c_jit_cache_struct_type, base, &cast, 1, "");

    /* Step 1: Load seq1 (acquire). Odd value means write in progress. */
    LLVMValueRef seq_ptr = LLVMBuildStructGEP2(
        ic_miss_builder, t2c_jit_cache_struct_type, element_ptr, 0, "");
    LLVMValueRef seq1 = LLVMBuildLoad2(
        ic_miss_builder, LLVMInt32TypeInContext(t2c_context), seq_ptr, "");
    LLVMSetOrdering(seq1, LLVMAtomicOrderingAcquire);
    LLVMValueRef seq_odd = LLVMBuildAnd(
        ic_miss_builder, seq1,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 1, false), "");
    LLVMValueRef is_even = LLVMBuildICmp(
        ic_miss_builder, LLVMIntEQ, seq_odd,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 0, false), "");
    LLVMBuildCondBr(ic_miss_builder, is_even, seq_even, fallback);

    /* Step 2: Load key (acquire) and compare with expected. */
    LLVMValueRef jc_key_ptr = LLVMBuildStructGEP2(
        seq_even_builder, t2c_jit_cache_struct_type, element_ptr, 2, "");
    LLVMValueRef jc_key = LLVMBuildLoad2(
        seq_even_builder, LLVMInt64TypeInContext(t2c_context), jc_key_ptr, "");
    LLVMSetOrdering(jc_key, LLVMAtomicOrderingAcquire);
    LLVMSetAlignment(jc_key, 8);

    LLVMValueRef jc_key_cmp =
        LLVMBuildICmp(seq_even_builder, LLVMIntEQ, jc_key, expected_key, "");
    LLVMBuildCondBr(seq_even_builder, jc_key_cmp, key_match, fallback);

    /* Step 3: Load entry (acquire). */
    LLVMValueRef jc_entry_ptr = LLVMBuildStructGEP2(
        key_match_builder, t2c_jit_cache_struct_type, element_ptr, 3, "");
    LLVMValueRef entry =
        LLVMBuildLoad2(key_match_builder,
                       LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
                       jc_entry_ptr, "");
    LLVMSetOrdering(entry, LLVMAtomicOrderingAcquire);
    LLVMSetAlignment(entry, 8);

    /* Step 4: Load seq2 (monotonic), check seq1 == seq2 AND entry != NULL. */
    LLVMValueRef seq2 = LLVMBuildLoad2(
        key_match_builder, LLVMInt32TypeInContext(t2c_context), seq_ptr, "");
    LLVMSetOrdering(seq2, LLVMAtomicOrderingMonotonic);

    LLVMValueRef seq_cmp =
        LLVMBuildICmp(key_match_builder, LLVMIntEQ, seq1, seq2, "");
    LLVMValueRef entry_not_null =
        LLVMBuildIsNotNull(key_match_builder, entry, "");
    LLVMValueRef valid =
        LLVMBuildAnd(key_match_builder, seq_cmp, entry_not_null, "");
#if RV32_HAS(SYSTEM)
    /* Privilege belongs to the same seqlock snapshot as key and entry. */
    LLVMValueRef jc_priv = LLVMBuildLoad2(
        key_match_builder, LLVMInt32TypeInContext(t2c_context),
        LLVMBuildStructGEP2(key_match_builder, t2c_jit_cache_struct_type,
                            element_ptr, 1, ""),
        "jc_priv");
    LLVMSetOrdering(jc_priv, LLVMAtomicOrderingAcquire);
    valid = LLVMBuildAnd(
        key_match_builder, valid,
        LLVMBuildICmp(key_match_builder, LLVMIntEQ, jc_priv, priv, ""), "");
#endif
    LLVMBuildCondBr(key_match_builder, valid, call_jit, fallback);

    /* Step 5: Update inline cache with the newly looked-up entry.
     * This populates the fast path for subsequent calls to same target.
     * No atomics needed - single-threaded update from main thread only.
     */
    LLVMBuildStore(call_builder, expected_key, ic_key_ptr);
    LLVMBuildStore(call_builder, entry, ic_entry_ptr);
#if RV32_HAS(SYSTEM)
    LLVMBuildStore(call_builder, priv, ic_priv_ptr);
#endif

#if defined(__aarch64__)
    /* ARM64 ISB required on slow path only - this is a newly-discovered block.
     * The inline cache hit path skips ISB because we already executed the
     * target successfully, so icache was coherent at that time.
     */
    LLVMTypeRef isb_func_type =
        LLVMFunctionType(LLVMVoidTypeInContext(t2c_context), NULL, 0, 0);
    LLVMValueRef isb_asm =
        LLVMGetInlineAsm(isb_func_type, "isb", 3, "", 0, true, false,
                         LLVMInlineAsmDialectATT, 0);
    LLVMBuildCall2(call_builder, isb_func_type, isb_asm, NULL, 0, "");
#endif

    LLVMBuildStore(call_builder, addr,
                   t2c_gen_PC_addr(start, &call_builder, ir));
    T2C_STORE_TIMER(call_builder, start, insn_counter);
    t2c_gen_tail_jump(call_builder, entry, rv_param);

    /* Fallback: seq odd, key mismatch, or seq changed - return to interp */
    LLVMBuildStore(fallback_builder, addr,
                   t2c_gen_PC_addr(start, &fallback_builder, ir));
    T2C_STORE_TIMER(fallback_builder, start, insn_counter);
    LLVMBuildRetVoid(fallback_builder);

    /* Dispose temporary builders */
    LLVMDisposeBuilder(ic_hit_builder);
    LLVMDisposeBuilder(ic_miss_builder);
    LLVMDisposeBuilder(seq_even_builder);
    LLVMDisposeBuilder(key_match_builder);
    LLVMDisposeBuilder(call_builder);
    LLVMDisposeBuilder(fallback_builder);
}

/* The target an indirect jump takes, when it takes one target at least
 * T2C_PREDICT_PERCENT of the time. The interpreter records targets in the
 * jump's branch history table while it runs. System targets must share the
 * recorded SATP and page graph, whose entry/backedge budget checks bound
 * interrupt latency even when a predicted return stays inside native code.
 */
#define T2C_PREDICT_PERCENT 90
#define T2C_PREDICT_MIN_HITS 16
static UNUSED uint32_t t2c_predicted_target(riscv_t *rv,
                                            block_t *block,
                                            rv_insn_t *ir)
{
    const branch_history_table_t *bt = ir->branch_table;
    if (!bt || t2c_region_insns >= T2C_REGION_BUDGET)
        return 0;
    uint64_t total = 0;
    uint32_t best = 0, best_times = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
#if RV32_HAS(SYSTEM)
        if (bt->satp[i] != block->satp)
            continue;
#endif
        /* Collection and execution are serialized on the hart thread. */
        uint32_t pc = ATOMIC_LOAD(&bt->PC[i], ATOMIC_RELAXED);
        uint32_t times = ATOMIC_LOAD(&bt->times[i], ATOMIC_RELAXED);
        if (pc == UINT32_MAX || !times)
            continue;
        total += times;
        if (times > best_times) {
            best_times = times;
            best = pc;
        }
    }
    if (best_times < T2C_PREDICT_MIN_HITS ||
        (uint64_t) best_times * 100 < total * T2C_PREDICT_PERCENT ||
        !t2c_check_valid_blk(rv, block, best))
        return 0;
    return best;
}

#if !RV32_HAS(SYSTEM)
/* Return to addr through the region's return switch, created on first use. */
static void t2c_gen_return(LLVMBuilderRef *builder,
                           LLVMValueRef start,
                           LLVMValueRef addr,
                           LLVMValueRef insn_counter)
{
    if (!t2c_return_dispatch) {
        t2c_return_dispatch = LLVMAppendBasicBlockInContext(t2c_context, start,
                                                            "return_dispatch");
        LLVMBuilderRef dispatch = LLVMCreateBuilderInContext(t2c_context);
        LLVMPositionBuilderAtEnd(dispatch, t2c_return_dispatch);
        t2c_return_pc = LLVMBuildPhi(
            dispatch, LLVMInt32TypeInContext(t2c_context), "return_pc");
        LLVMBasicBlockRef other =
            LLVMAppendBasicBlockInContext(t2c_context, start, "return_other");
        t2c_return_switch =
            LLVMBuildSwitch(dispatch, t2c_return_pc, other, T2C_MAX_RETURNS);
        LLVMPositionBuilderAtEnd(dispatch, other);
        t2c_jit_cache_helper(&dispatch, start, t2c_return_pc, NULL, NULL, NULL,
                             insn_counter);
        LLVMDisposeBuilder(dispatch);
    }
    LLVMBasicBlockRef from = LLVMGetInsertBlock(*builder);
    LLVMAddIncoming(t2c_return_pc, &addr, &from, 1);
    LLVMBuildBr(*builder, t2c_return_dispatch);
}
#endif

/* Jump to addr. When the jump has a dominant target, compare against it and
 * leave the matching path to t2c_trace_ebb() to continue into, so the region
 * keeps going instead of leaving through the jit-cache.
 */
static void t2c_indirect_jump(LLVMBuilderRef *builder,
                              LLVMValueRef start,
                              LLVMValueRef addr,
                              riscv_t *rv UNUSED,
                              block_t *block UNUSED,
                              rv_insn_t *ir UNUSED,
                              LLVMValueRef insn_counter,
                              LLVMBuilderRef *taken_builder UNUSED)
{
#if RV32_HAS(SYSTEM)
    /* Calls and returns within a verified page share a single local dispatch.
     * Its cases use the same budget gates as external entries, without
     * publishing/reloading all registers or leaving this native function.
     */
    if (!t2c_local_indirect) {
        t2c_local_indirect =
            LLVMAppendBasicBlockInContext(t2c_context, start, "local_indirect");
        LLVMBuilderRef local = LLVMCreateBuilderInContext(t2c_context);
        LLVMPositionBuilderAtEnd(local, t2c_local_indirect);
        t2c_local_indirect_pc = LLVMBuildPhi(
            local, LLVMInt32TypeInContext(t2c_context), "indirect_pc");
        LLVMBasicBlockRef outside =
            LLVMAppendBasicBlockInContext(t2c_context, start, "outside_page");
        t2c_local_indirect_switch = LLVMBuildSwitch(
            local, t2c_local_indirect_pc, outside, RV_PG_SIZE / 2);
        LLVMPositionBuilderAtEnd(local, outside);
        t2c_jit_cache_helper(&local, start, t2c_local_indirect_pc, NULL, NULL,
                             NULL, insn_counter);
        LLVMDisposeBuilder(local);
    }
    LLVMBasicBlockRef from = LLVMGetInsertBlock(*builder);
    LLVMAddIncoming(t2c_local_indirect_pc, &addr, &from, 1);
    LLVMBuildBr(*builder, t2c_local_indirect);
    return;
#else
    const bool is_return = (ir->opcode == rv_insn_jalr && !ir->rd &&
                            ir->rs1 == rv_reg_ra && !ir->imm)
        IIF(RV32_HAS(EXT_C))(
            || (ir->opcode == rv_insn_cjr && ir->rs1 == rv_reg_ra), );
    uint32_t target = t2c_predicted_target(rv, block, ir);
    if (!target) {
        if (is_return)
            t2c_gen_return(builder, start, addr, insn_counter);
        else
            t2c_jit_cache_helper(builder, start, addr, rv, block, ir,
                                 insn_counter);
        return;
    }
    LLVMValueRef hit = LLVMBuildICmp(
        *builder, LLVMIntEQ, addr,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), target, false),
        "predicted");
    LLVMBasicBlockRef hit_bb =
        LLVMAppendBasicBlockInContext(t2c_context, start, "predicted");
    LLVMBasicBlockRef miss_bb =
        LLVMAppendBasicBlockInContext(t2c_context, start, "mispredicted");
    LLVMValueRef branch = LLVMBuildCondBr(*builder, hit, hit_bb, miss_bb);
    LLVMMetadataRef weights[] = {
        LLVMMDStringInContext2(t2c_context, "branch_weights", 14),
        LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 90, false)),
        LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 10, false))};
    LLVMSetMetadata(
        branch, LLVMGetMDKindIDInContext(t2c_context, "prof", 4),
        LLVMMetadataAsValue(t2c_context,
                            LLVMMDNodeInContext2(t2c_context, weights, 3)));
    /* A rare miss leaves through the jit-cache: joining the return switch
     * there would add edges that only hinder register promotion.
     */
    LLVMBuilderRef miss = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(miss, miss_bb);
    t2c_jit_cache_helper(&miss, start, addr, rv, block, ir, insn_counter);
    LLVMDisposeBuilder(miss);
    *taken_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(*taken_builder, hit_bb);
    t2c_predicted_pc = target;
#endif
}

T2C_OP(jalr, {
    /* The register which stores the indirect address needs to be loaded first
     * to avoid being overriden by other operation.
     */
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    val_rs1 = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);
    val_rs1 = T2C_LLVM_GEN_ALU32_IMM(And, val_rs1, ~1U);

    t2c_note_call(ir->rd == rv_reg_ra, ir->pc + 4);
    if (ir->rd)
        T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 4,
                                 t2c_gen_rd_addr(start, builder, ir));

    t2c_indirect_jump(builder, start, val_rs1, rv, block, ir, insn_counter,
                      taken_builder);
})

#define BRANCH_FUNC(type, cond)                                            \
    T2C_OP(type, {                                                         \
        T2C_LLVM_GEN_LOAD_VMREG(rs1, 32,                                   \
                                t2c_gen_rs1_addr(start, builder, ir));     \
        T2C_LLVM_GEN_LOAD_VMREG(rs2, 32,                                   \
                                t2c_gen_rs2_addr(start, builder, ir));     \
        T2C_LLVM_GEN_CMP(cond, val_rs1, val_rs2);                          \
        LLVMBasicBlockRef taken =                                          \
            LLVMAppendBasicBlockInContext(t2c_context, start, "taken");    \
        LLVMBuilderRef builder2 = LLVMCreateBuilderInContext(t2c_context); \
        LLVMPositionBuilderAtEnd(builder2, taken);                         \
        if (t2c_has_direct_target(rv, block, ir->branch_taken,             \
                                  ir->pc + ir->imm)) {                     \
            *taken_builder = builder2;                                     \
        } else {                                                           \
            t2c_gen_exit(builder2, start, ir->pc + ir->imm, insn_counter); \
            LLVMDisposeBuilder(builder2);                                  \
        }                                                                  \
        LLVMBasicBlockRef untaken =                                        \
            LLVMAppendBasicBlockInContext(t2c_context, start, "untaken");  \
        LLVMBuilderRef builder3 = LLVMCreateBuilderInContext(t2c_context); \
        LLVMPositionBuilderAtEnd(builder3, untaken);                       \
        if (t2c_has_direct_target(rv, block, ir->branch_untaken,           \
                                  block->pc_end)) {                        \
            *untaken_builder = builder3;                                   \
        } else {                                                           \
            t2c_gen_exit(builder3, start, ir->pc + 4, insn_counter);       \
            LLVMDisposeBuilder(builder3);                                  \
        }                                                                  \
        LLVMBuildCondBr(*builder, cmp, taken, untaken);                    \
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

#if RV32_HAS(SYSTEM_MMIO)
/* Emit an inline dTLB fast path for a load or store as LLVM IR.
 *
 * The helper splits the surrounding function into three basic blocks:
 *   - the current block (entry): emits the probe; on miss branches to slow.
 *   - slow_bb: caller fills this with the existing function-call slow path
 *     and then branches to end_bb.
 *   - end_bb: post-merge continuation; the helper returns it positioned.
 *
 * On hit, the helper writes the loaded value (loads) or rs2 (stores) to the
 * appropriate guest register slot and branches straight to end_bb.
 *
 * On exit:
 *   - builder is positioned at slow_bb (empty, ready for caller's slow path).
 *   - Caller should finish with LLVMBuildBr(*builder, end_bb) and then
 *     LLVMPositionBuilderAtEnd(*builder, end_bb) before resuming emission.
 *
 * The host address comes from the current VM's tags, so the artifact embeds
 * neither the RAM base nor its size.
 */
static LLVMBasicBlockRef t2c_emit_mmu_fastpath(LLVMBuilderRef *builder,
                                               LLVMValueRef start,
                                               LLVMValueRef vaddr,
                                               LLVMValueRef data_addr,
                                               uint8_t access_size,
                                               bool is_store,
                                               bool is_signed)
{
    LLVMTypeRef i8 = LLVMInt8TypeInContext(t2c_context);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);

    LLVMBasicBlockRef slow_bb =
        LLVMAppendBasicBlockInContext(t2c_context, start, "fp_slow");
    LLVMBasicBlockRef end_bb =
        LLVMAppendBasicBlockInContext(t2c_context, start, "fp_end");
    LLVMBasicBlockRef hit_bb =
        LLVMAppendBasicBlockInContext(t2c_context, start, "fp_hit");

    /* The RAM tags of the T1 probe: a tag equals the page base plus the
     * current RAM tag context only where the effective privilege may make
     * this access, the page is whole RAM reached through the default
     * callbacks, and for a store the PTE is dirty. Bare translation fills tags
     * as well. Keeping the low address bits of the access size in the compared
     * value also proves alignment.
     */
    LLVMValueRef idx =
        LLVMBuildAnd(*builder,
                     LLVMBuildLShr(*builder, vaddr,
                                   LLVMConstInt(i32, RV_PG_SHIFT, false), ""),
                     LLVMConstInt(i32, TLB_SIZE - 1, false), "");
    LLVMValueRef byte_off =
        LLVMBuildMul(*builder, LLVMBuildZExt(*builder, idx, i64, ""),
                     LLVMConstInt(i64, sizeof(jit_tlb_entry_t), false), "");
    /* rv is only ever offset by constants first, so that guest register
     * promotion can tell these accesses from rv->X.
     */
    LLVMValueRef tags =
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, jit_dtlb), i8);
    LLVMValueRef entry_ptr =
        LLVMBuildGEP2(*builder, i8, tags, &byte_off, 1, "");
    /* mmu_ram_tag_context(): zero in U-mode, the access context otherwise.
     * The graph's entry checked its privilege class, as T1 relies on its
     * block's privilege.
     */
    LLVMValueRef context =
        t2c_user_graph
            ? LLVMConstInt(i32, 0, false)
            : LLVMBuildLoad2(*builder, i32,
                             t2c_gen_rv_field_ptr(
                                 start, builder,
                                 offsetof(riscv_t, mmu_access_context), i32),
                             "tag_context");
    LLVMValueRef tag_off =
        LLVMConstInt(i64,
                     is_store ? offsetof(jit_tlb_entry_t, write_tag)
                              : offsetof(jit_tlb_entry_t, read_tag),
                     false);
    LLVMValueRef tag = LLVMBuildLoad2(
        *builder, i32, LLVMBuildGEP2(*builder, i8, entry_ptr, &tag_off, 1, ""),
        "tag");
    LLVMValueRef wanted = LLVMBuildOr(
        *builder,
        LLVMBuildAnd(
            *builder, vaddr,
            LLVMConstInt(i32, ~(uint32_t) (RV_PG_SIZE - 1) | (access_size - 1),
                         false),
            ""),
        context, "");
    /* A miss calls the MMU helper. Keeping that path out of line keeps the
     * hit path of a page graph dense in the instruction cache.
     */
    t2c_gen_unlikely_br(*builder,
                        LLVMBuildICmp(*builder, LLVMIntNE, tag, wanted, ""),
                        slow_bb, hit_bb);
    LLVMPositionBuilderAtEnd(*builder, hit_bb);

    /* host_addr = addend + vaddr. */
    LLVMValueRef addend_off =
        LLVMConstInt(i64, offsetof(jit_tlb_entry_t, addend), false);
    LLVMValueRef addend = LLVMBuildLoad2(
        *builder, i64,
        LLVMBuildGEP2(*builder, i8, entry_ptr, &addend_off, 1, ""), "addend");
    LLVMValueRef host_addr_int = LLVMBuildAdd(
        *builder, addend, LLVMBuildZExt(*builder, vaddr, i64, ""), "");

    LLVMTypeRef val_type;
    switch (access_size) {
    case 1:
        val_type = LLVMInt8TypeInContext(t2c_context);
        break;
    case 2:
        val_type = LLVMInt16TypeInContext(t2c_context);
        break;
    case 4:
        val_type = LLVMInt32TypeInContext(t2c_context);
        break;
    default:
        __UNREACHABLE;
    }
    LLVMValueRef host_addr_ptr = LLVMBuildIntToPtr(
        *builder, host_addr_int, LLVMPointerType(val_type, 0), "");

    if (is_store) {
        LLVMValueRef val_rs2 = LLVMBuildLoad2(*builder, i32, data_addr, "");
        LLVMValueRef store_val =
            LLVMBuildIntCast2(*builder, val_rs2, val_type, false, "");
        LLVMBuildStore(*builder, store_val, host_addr_ptr);
    } else {
        LLVMValueRef raw =
            LLVMBuildLoad2(*builder, val_type, host_addr_ptr, "");
        t2c_gen_load_result(builder, raw, data_addr, is_signed);
    }

    LLVMBuildBr(*builder, end_bb);

    /* Hand the caller the (empty) slow block; they fill and link to end_bb. */
    LLVMPositionBuilderAtEnd(*builder, slow_bb);
    return end_bb;
}
#endif /* RV32_HAS(SYSTEM_MMIO) */

/* Load a 64-bit riscv_t field. */
static LLVMValueRef t2c_load_rv_u64(LLVMBuilderRef *builder,
                                    LLVMValueRef start,
                                    size_t offset)
{
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    return LLVMBuildLoad2(
        *builder, i64, t2c_gen_rv_field_ptr(start, builder, offset, i64), "");
}

/* Instructions of the current block after the one at pc. */
static unsigned t2c_trailing_insns(uint32_t pc)
{
    return t2c_current_block->cycle_cost - t2c_retired_prefix(pc) - 1;
}

/* Publish the retired prefix before slow MMU/MMIO helpers can run a guest
 * trap handler. Inline RAM hits continue in native code. *code_epoch gets the
 * code epoch seen before the call, for t2c_mmu_access_exit().
 */
static LLVMValueRef t2c_gen_mmu_call(LLVMBuilderRef *builder,
                                     LLVMValueRef start,
                                     size_t function_offset,
                                     LLVMTypeRef ret_type,
                                     LLVMValueRef *args,
                                     unsigned n,
                                     uint32_t pc,
                                     LLVMValueRef insn_counter,
                                     LLVMValueRef *code_epoch)
{
    /* Publish exactly through this access before a helper can run a guest
     * trap handler. The rest of this block has not executed yet.
     */
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    unsigned trailing = t2c_trailing_insns(pc);
    LLVMValueRef count = LLVMBuildLoad2(*builder, i64, insn_counter, "");
    LLVMBuildStore(
        *builder,
        LLVMBuildSub(*builder, count, LLVMConstInt(i64, trailing, false), ""),
        insn_counter);
    T2C_STORE_TIMER(*builder, start, insn_counter);
    LLVMBuildStore(*builder, LLVMConstInt(i64, 0, false), insn_counter);
    T2C_LLVM_GEN_STORE_IMM32(*builder, pc,
                             t2c_gen_PC_addr(start, builder, NULL));
    LLVMValueRef old_trap_epoch =
        t2c_load_rv_u64(builder, start, offsetof(riscv_t, aot_trap_epoch));
    *code_epoch =
        t2c_load_rv_u64(builder, start, offsetof(riscv_t, aot_code_epoch));
    t2c_add_stat(*builder, start, offsetof(riscv_t, aot_slow_calls),
                 LLVMConstInt(i64, 1, false));
    LLVMValueRef ret =
        t2c_gen_call_func(start, builder, function_offset, ret_type, args, n);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(t2c_context);
    LLVMValueRef traps =
        t2c_load_rv_u64(builder, start, offsetof(riscv_t, aot_trap_epoch));
    LLVMValueRef signal_ptr = LLVMBuildLoad2(
        *builder, LLVMPointerType(i8, 0),
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, aot_signal),
                             LLVMPointerType(i8, 0)),
        "");
    LLVMValueRef signal = LLVMBuildLoad2(*builder, i8, signal_ptr, "");
    LLVMValueRef faulted = LLVMBuildOr(
        *builder, LLVMBuildICmp(*builder, LLVMIntNE, traps, old_trap_epoch, ""),
        LLVMBuildICmp(*builder, LLVMIntNE, signal, LLVMConstInt(i8, 0, false),
                      ""),
        "");
    LLVMBasicBlockRef fault =
        LLVMAppendBasicBlockInContext(t2c_context, start, "mmu_fault");
    LLVMBasicBlockRef done =
        LLVMAppendBasicBlockInContext(t2c_context, start, "mmu_done");
    t2c_gen_unlikely_br(*builder, faulted, fault, done);
    LLVMPositionBuilderAtEnd(*builder, fault);
    LLVMBuildRetVoid(*builder);
    LLVMPositionBuilderAtEnd(*builder, done);
    return ret;
}

/* Successful helpers can continue in native code. Publish the completed
 * access before an invalidation exit, so MMIO is never executed twice.
 */
static void t2c_mmu_access_exit(LLVMBuilderRef *builder,
                                LLVMValueRef start,
                                uint32_t pc,
                                LLVMValueRef insn_counter,
                                LLVMValueRef old_code_epoch)
{
    const block_t *block = t2c_current_block;
    unsigned halfword = (pc - block->pc_start) / 2 + 1;
    bool short_insn =
        pc + 2 == block->pc_end ||
        (halfword < 2048 &&
         ((block->aot_insn_starts[halfword / 64] >> (halfword % 64)) & 1));
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    LLVMValueRef epoch =
        t2c_load_rv_u64(builder, start, offsetof(riscv_t, aot_code_epoch));
    LLVMBasicBlockRef exit =
        LLVMAppendBasicBlockInContext(t2c_context, start, "mmu_invalidated");
    LLVMBasicBlockRef resume =
        LLVMAppendBasicBlockInContext(t2c_context, start, "mmu_resume");
    t2c_gen_unlikely_br(
        *builder, LLVMBuildICmp(*builder, LLVMIntNE, epoch, old_code_epoch, ""),
        exit, resume);
    LLVMPositionBuilderAtEnd(*builder, exit);
    t2c_gen_exit_raw(*builder, start, pc + (short_insn ? 2 : 4), insn_counter);
    LLVMPositionBuilderAtEnd(*builder, resume);
    t2c_add_stat(*builder, start, offsetof(riscv_t, aot_slow_resumes),
                 LLVMConstInt(i64, 1, false));
    LLVMBuildStore(*builder, LLVMConstInt(i64, t2c_trailing_insns(pc), false),
                   insn_counter);
}

/* Outline uncommon semantic operations inside a native page graph, retaining
 * precise faults and the interpreter's LR/SC reservation rules. CSR writes
 * return to the dispatcher so newly enabled interrupts are polled immediately.
 */
/* Unused when neither EXT_A nor Zicsr is built. */
static UNUSED void t2c_gen_outlined_insn(LLVMBuilderRef *builder,
                                         LLVMValueRef start,
                                         rv_insn_t *ir,
                                         LLVMValueRef insn_counter)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    bool status_write = false;
#if RV32_HAS(Zicsr)
    status_write =
        (ir->imm & 0xfff) == CSR_SSTATUS &&
        (ir->opcode == rv_insn_csrrw || ir->opcode == rv_insn_csrrwi ||
         ((ir->opcode == rv_insn_csrrs || ir->opcode == rv_insn_csrrc ||
           ir->opcode == rv_insn_csrrsi || ir->opcode == rv_insn_csrrci) &&
          ir->rs1));
#endif
    LLVMValueRef status_ptr = NULL, old_status = NULL;
    if (status_write) {
        status_ptr = t2c_gen_rv_field_ptr(start, builder,
                                          offsetof(riscv_t, csr_sstatus), i32);
        old_status = LLVMBuildLoad2(*builder, i32, status_ptr, "status.before");
    }
    uint32_t operation =
        ir->opcode | (ir->rd << 16) | (ir->rs1 << 21) | (ir->rs2 << 26);
    LLVMValueRef args[] = {LLVMConstInt(i32, operation, false),
                           LLVMConstInt(i32, ir->imm, false)};
    LLVMValueRef code_epoch;
    t2c_gen_mmu_call(builder, start, offsetof(riscv_t, aot_insn),
                     LLVMVoidTypeInContext(t2c_context), args, 2, ir->pc,
                     insn_counter, &code_epoch);
    if (status_write) {
        /* In S-mode, changing only SIE while leaving it clear cannot make
         * an interrupt deliverable. Retain dispatcher boundaries when enabling
         * interrupts, changing other status bits (including FS/SUM/MXR), or
         * executing outside S-mode. Fault/epoch guards still apply below.
         */
        LLVMValueRef status =
            LLVMBuildLoad2(*builder, i32, status_ptr, "status.after");
        LLVMValueRef changed =
            LLVMBuildXor(*builder, status, old_status, "status.changed");
        LLVMValueRef other_changes = LLVMBuildAnd(
            *builder, changed, LLVMConstInt(i32, ~SSTATUS_SIE, false), "");
        LLVMValueRef enabled = LLVMBuildAnd(
            *builder, status, LLVMConstInt(i32, SSTATUS_SIE, false), "");
        LLVMValueRef dispatch_bits =
            LLVMBuildOr(*builder, other_changes, enabled, "");
        LLVMValueRef dispatch =
            LLVMBuildICmp(*builder, LLVMIntNE, dispatch_bits,
                          LLVMConstInt(i32, 0, false), "");
        LLVMValueRef mode = LLVMBuildLoad2(
            *builder, i32,
            t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, priv_mode),
                                 i32),
            "");
        dispatch = LLVMBuildOr(
            *builder, dispatch,
            LLVMBuildICmp(*builder, LLVMIntNE, mode,
                          LLVMConstInt(i32, RV_PRIV_S_MODE, false), ""),
            "");
        LLVMBasicBlockRef exit = LLVMAppendBasicBlockInContext(
            t2c_context, start, "status.dispatch");
        LLVMBasicBlockRef resume = LLVMAppendBasicBlockInContext(
            t2c_context, start, "status.disabled");
        t2c_gen_unlikely_br(*builder, dispatch, exit, resume);
        LLVMPositionBuilderAtEnd(*builder, exit);
        t2c_gen_exit_raw(*builder, start, ir->pc + 4, insn_counter);
        LLVMPositionBuilderAtEnd(*builder, resume);
    }
    t2c_mmu_access_exit(builder, start, ir->pc, insn_counter, code_epoch);
}

#if RV32_HAS(Zicsr)
/* Clearing only SIE in S-mode is a raw, nonfaulting CSR read/modify/write.
 * It does not synchronize time, alter memory permissions, or enable an IRQ.
 * Keep other privilege modes on the existing semantic helper path.
 */
static void t2c_gen_sstatus_clear(LLVMBuilderRef *builder,
                                  LLVMValueRef start,
                                  rv_insn_t *ir,
                                  LLVMValueRef insn_counter)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    LLVMValueRef mode = LLVMBuildLoad2(
        *builder, i32,
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, priv_mode), i32),
        "");
    LLVMValueRef fallback =
        LLVMBuildICmp(*builder, LLVMIntNE, mode,
                      LLVMConstInt(i32, RV_PRIV_S_MODE, false), "");
    LLVMBasicBlockRef fast =
        LLVMAppendBasicBlockInContext(t2c_context, start, "status.clear");
    LLVMBasicBlockRef slow =
        LLVMAppendBasicBlockInContext(t2c_context, start, "status.helper");
    LLVMBasicBlockRef merge =
        LLVMAppendBasicBlockInContext(t2c_context, start, "status.merge");
    t2c_gen_unlikely_br(*builder, fallback, slow, fast);
    LLVMPositionBuilderAtEnd(*builder, fast);
    LLVMValueRef pointer = t2c_gen_rv_field_ptr(
        start, builder, offsetof(riscv_t, csr_sstatus), i32);
    LLVMValueRef old = LLVMBuildLoad2(*builder, i32, pointer, "status.old");
    LLVMBuildStore(
        *builder,
        LLVMBuildAnd(*builder, old, LLVMConstInt(i32, ~SSTATUS_SIE, false), ""),
        pointer);
    if (ir->rd)
        LLVMBuildStore(*builder, old, t2c_gen_rd_addr(start, builder, ir));
    LLVMBuildBr(*builder, merge);
    LLVMPositionBuilderAtEnd(*builder, slow);
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
    LLVMBuildBr(*builder, merge);
    LLVMPositionBuilderAtEnd(*builder, merge);
}

#endif

/* Load through the MMU into the register at rd_addr. The instruction may be a
 * fused entry cast to rv_insn_t, whose pc is not valid, so pc comes apart.
 */
static void t2c_gen_mmu_load(LLVMBuilderRef *builder,
                             LLVMValueRef start,
                             LLVMValueRef vaddr,
                             size_t io_field,
                             unsigned bits,
                             bool is_signed,
                             LLVMValueRef rd_addr,
                             uint32_t pc,
                             LLVMValueRef insn_counter)
{
    LLVMValueRef code_epoch;
    LLVMValueRef ret =
        t2c_gen_mmu_call(builder, start, offsetof(riscv_t, io) + io_field,
                         LLVMIntTypeInContext(t2c_context, bits), &vaddr, 1, pc,
                         insn_counter, &code_epoch);
    t2c_gen_load_result(builder, ret, rd_addr, is_signed);
    t2c_mmu_access_exit(builder, start, pc, insn_counter, code_epoch);
}

/* Store the low bits of the register at src_addr through the MMU. */
static void t2c_gen_mmu_store(LLVMBuilderRef *builder,
                              LLVMValueRef start,
                              LLVMValueRef vaddr,
                              size_t io_field,
                              unsigned bits,
                              LLVMValueRef src_addr,
                              uint32_t pc,
                              LLVMValueRef insn_counter)
{
    /* Pass the value zero-extended to 32 bits: the C helpers take uint8_t or
     * uint16_t, whose upper register bits a clang-built callee assumes clear.
     */
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    LLVMValueRef value = LLVMBuildLoad2(*builder, i32, src_addr, "");
    if (bits < 32)
        value = LLVMBuildAnd(
            *builder, value,
            LLVMConstInt(i32, (UINT32_C(1) << bits) - 1, false), "");
    LLVMValueRef args[] = {vaddr, value};
    LLVMValueRef code_epoch;
    t2c_gen_mmu_call(builder, start, offsetof(riscv_t, io) + io_field,
                     LLVMVoidTypeInContext(t2c_context), args, 2, pc,
                     insn_counter, &code_epoch);
    t2c_mmu_access_exit(builder, start, pc, insn_counter, code_epoch);
}

/* Merge the slow path into the inline dTLB fast path, if one was emitted. */
static void t2c_mmu_join(LLVMBuilderRef *builder, LLVMBasicBlockRef fp_end)
{
    if (!fp_end)
        return;
    LLVMBuildBr(*builder, fp_end);
    LLVMPositionBuilderAtEnd(*builder, fp_end);
}

#if RV32_HAS(SYSTEM_MMIO)
#define T2C_MMU_FASTPATH_ADDR(addr, data, size, is_store, is_signed) \
    t2c_emit_mmu_fastpath(builder, start, addr, data, size, is_store, is_signed)
#else
#define T2C_MMU_FASTPATH_ADDR(addr, data, size, is_store, is_signed) NULL
#endif
#define T2C_MMU_FASTPATH(ir, size, is_store, is_signed)                        \
    T2C_MMU_FASTPATH_ADDR(t2c_gen_vaddr(start, builder, ir),                   \
                          is_store ? t2c_gen_rs2_addr(start, builder, ir)      \
                                   : t2c_gen_load_rd_addr(start, builder, ir), \
                          size, is_store, is_signed)

/* T2C_MMU_LOAD / T2C_MMU_STORE: a load or store at rs1 + imm through the MMU,
 * behind the inline dTLB fast path where there is one.
 *   io_field: the riscv_io_t helper (mmu_read_b ... mmu_write_w)
 *   bits: the width it transfers
 */
#define T2C_MMU_LOAD(opcode, io_field, bits, access_size, is_signed)      \
    static void t2c_mmu_wrapper_##opcode(                                 \
        LLVMBuilderRef *builder, LLVMValueRef start, rv_insn_t *ir,       \
        riscv_t *rv UNUSED, uint32_t pc, LLVMValueRef insn_counter)       \
    {                                                                     \
        LLVMValueRef vaddr = t2c_gen_vaddr(start, builder, ir);           \
        LLVMBasicBlockRef fp_end =                                        \
            T2C_MMU_FASTPATH(ir, access_size, false, is_signed);          \
        t2c_gen_mmu_load(builder, start, vaddr,                           \
                         offsetof(riscv_io_t, io_field), bits, is_signed, \
                         t2c_gen_load_rd_addr(start, builder, ir), pc,    \
                         insn_counter);                                   \
        t2c_mmu_join(builder, fp_end);                                    \
    }

#define T2C_MMU_STORE(opcode, io_field, bits, access_size)               \
    static void t2c_mmu_wrapper_##opcode(                                \
        LLVMBuilderRef *builder, LLVMValueRef start, rv_insn_t *ir,      \
        riscv_t *rv UNUSED, uint32_t pc, LLVMValueRef insn_counter)      \
    {                                                                    \
        LLVMValueRef vaddr = t2c_gen_vaddr(start, builder, ir);          \
        LLVMBasicBlockRef fp_end =                                       \
            T2C_MMU_FASTPATH(ir, access_size, true, false);              \
        t2c_gen_mmu_store(                                               \
            builder, start, vaddr, offsetof(riscv_io_t, io_field), bits, \
            t2c_gen_rs2_addr(start, builder, ir), pc, insn_counter);     \
        t2c_mmu_join(builder, fp_end);                                   \
    }

T2C_MMU_LOAD(lb, mmu_read_b, 8, 1, true);
T2C_MMU_LOAD(lbu, mmu_read_b, 8, 1, false);
T2C_MMU_LOAD(lh, mmu_read_s, 16, 2, true);
T2C_MMU_LOAD(lhu, mmu_read_s, 16, 2, false);
T2C_MMU_LOAD(lw, mmu_read_w, 32, 4, true);

T2C_MMU_STORE(sb, mmu_write_b, 8, 1);
T2C_MMU_STORE(sh, mmu_write_s, 16, 2);
T2C_MMU_STORE(sw, mmu_write_w, 32, 4);

/* fuse9 (LUI + LW) loads from the absolute address imm + imm2 into rs2. */
static void t2c_mmu_wrapper_fuse9(LLVMBuilderRef *builder,
                                  LLVMValueRef start,
                                  rv_insn_t *ir,
                                  riscv_t *rv UNUSED,
                                  uint32_t pc,
                                  LLVMValueRef insn_counter)
{
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    LLVMValueRef vaddr =
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), addr, false);
    LLVMValueRef data = ir->rs2 ? t2c_gen_rs2_addr(start, builder, ir) : NULL;
    LLVMBasicBlockRef fp_end =
        T2C_MMU_FASTPATH_ADDR(vaddr, data, 4, false, false);
    t2c_gen_mmu_load(builder, start, vaddr, offsetof(riscv_io_t, mmu_read_w),
                     32, false, data, pc, insn_counter);
    t2c_mmu_join(builder, fp_end);
}

/* fuse10 (LUI + SW) stores rs1 to the absolute address imm + imm2. */
static void t2c_mmu_wrapper_fuse10(LLVMBuilderRef *builder,
                                   LLVMValueRef start,
                                   rv_insn_t *ir,
                                   riscv_t *rv UNUSED,
                                   uint32_t pc,
                                   LLVMValueRef insn_counter)
{
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    LLVMValueRef vaddr =
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), addr, false);
    LLVMValueRef data = t2c_gen_rs1_addr(start, builder, ir);
    LLVMBasicBlockRef fp_end =
        T2C_MMU_FASTPATH_ADDR(vaddr, data, 4, true, false);
    t2c_gen_mmu_store(builder, start, vaddr, offsetof(riscv_io_t, mmu_write_w),
                      32, data, pc, insn_counter);
    t2c_mmu_join(builder, fp_end);
}

/* fuse11 (LW + ADDI) loads from rs1 + imm into rd, then adds imm2 to rs1. */
static void t2c_mmu_wrapper_fuse11(LLVMBuilderRef *builder,
                                   LLVMValueRef start,
                                   rv_insn_t *ir,
                                   riscv_t *rv UNUSED,
                                   uint32_t pc,
                                   LLVMValueRef insn_counter)
{
    LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
    LLVMValueRef vaddr = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);
    LLVMValueRef rd_addr = t2c_gen_load_rd_addr(start, builder, ir);
    LLVMBasicBlockRef fp_end =
        T2C_MMU_FASTPATH_ADDR(vaddr, rd_addr, 4, false, false);
    t2c_gen_mmu_load(builder, start, vaddr, offsetof(riscv_io_t, mmu_read_w),
                     32, false, rd_addr, pc, insn_counter);
    t2c_mmu_join(builder, fp_end);
    LLVMBuildStore(*builder, T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm2),
                   addr_rs1);
}

#endif

T2C_OP(lb, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lb)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt8TypeInContext(t2c_context), mem_loc, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir), true);
        });
})

T2C_OP(lh, {
    T2C_MISALIGN_GUARD(ir, 2, 0, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lh)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt16TypeInContext(t2c_context), mem_loc, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir), true);
        });
})


T2C_OP(lw, {
    T2C_MISALIGN_GUARD(ir, 4, 0, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lw)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt32TypeInContext(t2c_context), mem_loc, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir),
                                false);
        });
})

T2C_OP(lbu, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lbu)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt8TypeInContext(t2c_context), mem_loc, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir),
                                false);
        });
})

T2C_OP(lhu, {
    T2C_MISALIGN_GUARD(ir, 2, 0, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lhu)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt16TypeInContext(t2c_context), mem_loc, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir),
                                false);
        });
})

T2C_OP(sb, {
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sb)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 8,
                                    t2c_gen_rs2_addr(start, builder, ir));
            LLVMBuildStore(*builder, val_rs2, mem_loc);
        });
})

T2C_OP(sh, {
    T2C_MISALIGN_GUARD(ir, 2, JIT_MISALIGN_STORE, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sh)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 16,
                                    t2c_gen_rs2_addr(start, builder, ir));
            LLVMBuildStore(*builder, val_rs2, mem_loc);
        });
})

T2C_OP(sw, {
    T2C_MISALIGN_GUARD(ir, 4, JIT_MISALIGN_STORE, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sw)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
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
    LLVMValueRef res = LLVMBuildSelect(
        *builder, cmp,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 1, true),
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 0, true), "");
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(sltiu, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP_IMM32(ULT, val_rs1, ir->imm);
    LLVMValueRef res = LLVMBuildSelect(
        *builder, cmp,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 1, true),
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 0, true), "");
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
    LLVMValueRef res = LLVMBuildSelect(
        *builder, cmp,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 1, true),
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 0, true), "");
    LLVMBuildStore(*builder, res, addr_rd);
})

T2C_OP(sltu, {
    LLVMValueRef addr_rd = t2c_gen_rd_addr(start, builder, ir);
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP(ULT, val_rs1, val_rs2);
    LLVMValueRef res = LLVMBuildSelect(
        *builder, cmp,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 1, true),
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 0, true), "");
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
    LLVMBuildStore(
        *builder, LLVMConstInt(LLVMInt8TypeInContext(t2c_context), 0, false),
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, compressed),
                             LLVMInt8TypeInContext(t2c_context)));
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    /* Use offsetof() to compute proper byte offset for on_ecall.
     * This works correctly regardless of SYSTEM mode (which adds MMU ptrs).
     */
    T2C_STORE_TIMER(*builder, start, insn_counter);
    t2c_gen_call_io_func(start, builder, offsetof(riscv_io_t, on_ecall),
                         LLVMVoidTypeInContext(t2c_context), NULL, 0);
    LLVMBuildRetVoid(*builder);
})

T2C_OP(ebreak, {
    LLVMBuildStore(
        *builder, LLVMConstInt(LLVMInt8TypeInContext(t2c_context), 0, false),
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, compressed),
                             LLVMInt8TypeInContext(t2c_context)));
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    /* Use offsetof() to compute proper byte offset for on_ebreak.
     * This works correctly regardless of SYSTEM mode (which adds MMU ptrs).
     */
    T2C_STORE_TIMER(*builder, start, insn_counter);
    t2c_gen_call_io_func(start, builder, offsetof(riscv_io_t, on_ebreak),
                         LLVMVoidTypeInContext(t2c_context), NULL, 0);
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
#if RV32_HAS(SYSTEM)
static void t2c_gen_counter_read(LLVMBuilderRef *builder,
                                 LLVMValueRef start,
                                 rv_insn_t *ir,
                                 LLVMValueRef insn_counter)
{
    assert(t2c_counter_read(ir));
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    LLVMValueRef retired = LLVMBuildLoad2(*builder, i64, insn_counter, "");
    unsigned remaining = t2c_trailing_insns(ir->pc);
    LLVMValueRef cycle = LLVMBuildLoad2(
        *builder, i64, t2c_gen_csr_cycle_addr(start, builder, NULL), "");
    LLVMValueRef value =
        LLVMBuildAdd(*builder, cycle,
                     LLVMBuildSub(*builder, retired,
                                  LLVMConstInt(i64, remaining, false), ""),
                     "");
    unsigned csr = ir->imm & 0xfff;
    if (csr == CSR_TIME || csr == CSR_TIMEH) {
        LLVMValueRef offset =
            t2c_load_rv_u64(builder, start, offsetof(riscv_t, timer_offset));
        value = LLVMBuildAdd(*builder, value, offset, "");
        LLVMBuildStore(*builder, value,
                       t2c_gen_rv_field_ptr(start, builder,
                                            offsetof(riscv_t, timer), i64));
        LLVMBuildStore(
            *builder, LLVMBuildTrunc(*builder, value, i32, ""),
            t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, csr_time[0]),
                                 i32));
        LLVMBuildStore(
            *builder,
            LLVMBuildTrunc(*builder,
                           LLVMBuildLShr(*builder, value,
                                         LLVMConstInt(i64, 32, false), ""),
                           i32, ""),
            t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, csr_time[1]),
                                 i32));
    }
    if (csr & 0x80)
        value =
            LLVMBuildLShr(*builder, value, LLVMConstInt(i64, 32, false), "");
    if (ir->rd)
        LLVMBuildStore(*builder, LLVMBuildTrunc(*builder, value, i32, ""),
                       t2c_gen_rd_addr(start, builder, ir));
}
#define T2C_COUNTER_READ() \
    t2c_gen_counter_read(builder, start, ir, insn_counter)
#else
#define T2C_COUNTER_READ() __UNREACHABLE
#endif

T2C_OP(csrrw, {
#if RV32_HAS(SYSTEM)
    if (t2c_outlined_insn(ir))
        t2c_gen_outlined_insn(builder, start, ir, insn_counter);
    else
#endif
        T2C_COUNTER_READ();
})

T2C_OP(csrrs, {
#if RV32_HAS(SYSTEM)
    if (t2c_outlined_insn(ir))
        t2c_gen_outlined_insn(builder, start, ir, insn_counter);
    else
#endif
        T2C_COUNTER_READ();
})

T2C_OP(csrrc, {
#if RV32_HAS(SYSTEM)
    if (t2c_outlined_insn(ir))
        t2c_gen_outlined_insn(builder, start, ir, insn_counter);
    else
#endif
        T2C_COUNTER_READ();
})

T2C_OP(csrrwi, {
#if RV32_HAS(SYSTEM)
    if (t2c_outlined_insn(ir))
        t2c_gen_outlined_insn(builder, start, ir, insn_counter);
    else
#endif
        T2C_COUNTER_READ();
})

T2C_OP(csrrsi, {
#if RV32_HAS(SYSTEM)
    if (t2c_outlined_insn(ir))
        t2c_gen_outlined_insn(builder, start, ir, insn_counter);
    else
#endif
        T2C_COUNTER_READ();
})

T2C_OP(csrrci, {
#if RV32_HAS(SYSTEM)
    if ((ir->imm & 0xfff) == CSR_SSTATUS && ir->rs1 == SSTATUS_SIE)
        t2c_gen_sstatus_clear(builder, start, ir, insn_counter);
    else if (t2c_outlined_insn(ir))
        t2c_gen_outlined_insn(builder, start, ir, insn_counter);
    else
#endif
        T2C_COUNTER_READ();
})
#endif

#if RV32_HAS(EXT_M)
T2C_OP(mul, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildSExt(*builder, val_rs1,
                            LLVMInt64TypeInContext(t2c_context), "sextrs1to64");
    val_rs2 = LLVMBuildSExt(*builder, val_rs2,
                            LLVMInt64TypeInContext(t2c_context), "sextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(And, res, 0xFFFFFFFF);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32TypeInContext(t2c_context),
                         "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(mulh, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildSExt(*builder, val_rs1,
                            LLVMInt64TypeInContext(t2c_context), "sextrs1to64");
    val_rs2 = LLVMBuildSExt(*builder, val_rs2,
                            LLVMInt64TypeInContext(t2c_context), "sextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(LShr, res, 32);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32TypeInContext(t2c_context),
                         "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(mulhsu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildSExt(*builder, val_rs1,
                            LLVMInt64TypeInContext(t2c_context), "sextrs1to64");
    val_rs2 = LLVMBuildZExt(*builder, val_rs2,
                            LLVMInt64TypeInContext(t2c_context), "zextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(LShr, res, 32);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32TypeInContext(t2c_context),
                         "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(mulhu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    val_rs1 = LLVMBuildZExt(*builder, val_rs1,
                            LLVMInt64TypeInContext(t2c_context), "sextrs1to64");
    val_rs2 = LLVMBuildZExt(*builder, val_rs2,
                            LLVMInt64TypeInContext(t2c_context), "zextrs2to64");
    LLVMValueRef res = LLVMBuildMul(*builder, val_rs1, val_rs2, "mul");
    res = T2C_LLVM_GEN_ALU64_IMM(LShr, res, 32);
    res = LLVMBuildTrunc(*builder, res, LLVMInt32TypeInContext(t2c_context),
                         "sextresto32");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

/* Guard the operations themselves: LLVM division is undefined for zero
 * divisors and signed overflow. A rare branch leaves the common divisor
 * unchanged, avoiding a dependency through a select before hardware division.
 */
static LLVMValueRef t2c_gen_divrem(LLVMBuilderRef builder,
                                   LLVMValueRef dividend,
                                   LLVMValueRef divisor,
                                   bool is_signed,
                                   bool remainder)
{
    LLVMTypeRef type = LLVMInt32TypeInContext(t2c_context);
    LLVMValueRef zero = LLVMConstInt(type, 0, false);
    LLVMValueRef one = LLVMConstInt(type, 1, false);
    LLVMValueRef is_zero =
        LLVMBuildICmp(builder, LLVMIntEQ, divisor, zero, "div.zero");
    LLVMValueRef special = is_zero;
    if (is_signed) {
        LLVMValueRef next = LLVMBuildAdd(builder, divisor, one, "div.next");
        special = LLVMBuildICmp(builder, LLVMIntULE, next, one, "div.special");
    }
    LLVMValueRef function =
        LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
    LLVMBasicBlockRef normal_bb =
        LLVMAppendBasicBlockInContext(t2c_context, function, "div.normal");
    LLVMBasicBlockRef special_bb =
        LLVMAppendBasicBlockInContext(t2c_context, function, "div.exception");
    LLVMBasicBlockRef merge =
        LLVMAppendBasicBlockInContext(t2c_context, function, "div.merge");
    t2c_gen_unlikely_br(builder, special, special_bb, normal_bb);
    LLVMPositionBuilderAtEnd(builder, normal_bb);
    LLVMValueRef normal;
    if (is_signed)
        normal = remainder ? LLVMBuildSRem(builder, dividend, divisor, "srem")
                           : LLVMBuildSDiv(builder, dividend, divisor, "sdiv");
    else
        normal = remainder ? LLVMBuildURem(builder, dividend, divisor, "urem")
                           : LLVMBuildUDiv(builder, dividend, divisor, "udiv");
    LLVMBuildBr(builder, merge);
    LLVMPositionBuilderAtEnd(builder, special_bb);
    LLVMValueRef exceptional;
    if (remainder)
        exceptional = is_signed ? LLVMBuildSelect(builder, is_zero, dividend,
                                                  zero, "rem.exception")
                                : dividend;
    else {
        LLVMValueRef minus_one = LLVMConstInt(type, UINT32_MAX, false);
        exceptional =
            is_signed ? LLVMBuildSelect(
                            builder, is_zero, minus_one,
                            LLVMBuildSub(builder, zero, dividend, "div.neg"),
                            "div.exception")
                      : minus_one;
    }
    LLVMBuildBr(builder, merge);
    LLVMPositionBuilderAtEnd(builder, merge);
    LLVMValueRef result = LLVMBuildPhi(builder, type, "div.result");
    LLVMValueRef values[] = {normal, exceptional};
    LLVMBasicBlockRef blocks[] = {normal_bb, special_bb};
    LLVMAddIncoming(result, values, blocks, 2);
    return result;
}

T2C_OP(div, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = t2c_gen_divrem(*builder, val_rs1, val_rs2, true, false);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(divu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = t2c_gen_divrem(*builder, val_rs1, val_rs2, false, false);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(rem, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = t2c_gen_divrem(*builder, val_rs1, val_rs2, true, true);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(remu, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = t2c_gen_divrem(*builder, val_rs1, val_rs2, false, true);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})
#endif

#if RV32_HAS(EXT_A)
T2C_OP(lrw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(scw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amoswapw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amoaddw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amoxorw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amoandw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amoorw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amominw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amomaxw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amominuw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})

T2C_OP(amomaxuw, {
#if RV32_HAS(SYSTEM)
    t2c_gen_outlined_insn(builder, start, ir, insn_counter);
#else
    __UNREACHABLE;
#endif
})
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
    T2C_MISALIGN_GUARD(ir, 4, JIT_MISALIGN_COMPRESSED, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lw)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt32TypeInContext(t2c_context), mem_loc, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir),
                                false);
        });
})

T2C_OP(csw, {
    T2C_MISALIGN_GUARD(ir, 4, JIT_MISALIGN_STORE | JIT_MISALIGN_COMPRESSED,
                       ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sw)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
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
    t2c_note_call(true, ir->pc + 2);
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 2,
                             t2c_gen_ra_addr(start, builder, ir));
    if (t2c_has_direct_target(rv, block, ir->branch_taken, ir->pc + ir->imm))
        *taken_builder = *builder;
    else {
        t2c_gen_exit(*builder, start, ir->pc + ir->imm, insn_counter);
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
    if (t2c_has_direct_target(rv, block, ir->branch_taken, ir->pc + ir->imm))
        *taken_builder = *builder;
    else {
        t2c_gen_exit(*builder, start, ir->pc + ir->imm, insn_counter);
    }
})

T2C_OP(cbeqz, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP_IMM32(EQ, val_rs1, 0);
    LLVMBasicBlockRef taken =
        LLVMAppendBasicBlockInContext(t2c_context, start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (t2c_has_direct_target(rv, block, ir->branch_taken, ir->pc + ir->imm)) {
        *taken_builder = builder2;
    } else {
        t2c_gen_exit(builder2, start, ir->pc + ir->imm, insn_counter);
        LLVMDisposeBuilder(builder2);
    }

    LLVMBasicBlockRef untaken =
        LLVMAppendBasicBlockInContext(t2c_context, start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (t2c_has_direct_target(rv, block, ir->branch_untaken, block->pc_end)) {
        *untaken_builder = builder3;
    } else {
        t2c_gen_exit(builder3, start, ir->pc + 2, insn_counter);
        LLVMDisposeBuilder(builder3);
    }
    LLVMBuildCondBr(*builder, cmp, taken, untaken);
})

T2C_OP(cbnez, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_CMP_IMM32(NE, val_rs1, 0);
    LLVMBasicBlockRef taken =
        LLVMAppendBasicBlockInContext(t2c_context, start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (t2c_has_direct_target(rv, block, ir->branch_taken, ir->pc + ir->imm)) {
        *taken_builder = builder2;
    } else {
        t2c_gen_exit(builder2, start, ir->pc + ir->imm, insn_counter);
        LLVMDisposeBuilder(builder2);
    }

    LLVMBasicBlockRef untaken =
        LLVMAppendBasicBlockInContext(t2c_context, start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (t2c_has_direct_target(rv, block, ir->branch_untaken, block->pc_end)) {
        *untaken_builder = builder3;
    } else {
        t2c_gen_exit(builder3, start, ir->pc + 2, insn_counter);
        LLVMDisposeBuilder(builder3);
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
    /* The decoder sets rs1 to sp for the stack-pointer forms. */
    T2C_MISALIGN_GUARD(ir, 4, JIT_MISALIGN_COMPRESSED, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(lw)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef cast_addr =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt32TypeInContext(t2c_context), cast_addr, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir),
                                false);
        });
})

T2C_OP(cjr, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    val_rs1 = T2C_LLVM_GEN_ALU32_IMM(And, val_rs1, ~1U);
    t2c_indirect_jump(builder, start, val_rs1, rv, block, ir, insn_counter,
                      taken_builder);
})

T2C_OP(cmv, {
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMBuildStore(*builder, val_rs2, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cebreak, {
    LLVMBuildStore(
        *builder, LLVMConstInt(LLVMInt8TypeInContext(t2c_context), 1, false),
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, compressed),
                             LLVMInt8TypeInContext(t2c_context)));
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc,
                             t2c_gen_PC_addr(start, builder, ir));
    /* Use offsetof() to compute proper byte offset for on_ebreak.
     * This works correctly regardless of SYSTEM mode (which adds MMU ptrs).
     */
    T2C_STORE_TIMER(*builder, start, insn_counter);
    t2c_gen_call_io_func(start, builder, offsetof(riscv_io_t, on_ebreak),
                         LLVMVoidTypeInContext(t2c_context), NULL, 0);
    LLVMBuildRetVoid(*builder);
})

T2C_OP(cjalr, {
    /* The register which stores the indirect address needs to be loaded first
     * to avoid being overriden by other operation.
     */
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    val_rs1 = T2C_LLVM_GEN_ALU32_IMM(And, val_rs1, ~1U);
    t2c_note_call(true, ir->pc + 2);
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 2,
                             t2c_gen_ra_addr(start, builder, ir));
    t2c_indirect_jump(builder, start, val_rs1, rv, block, ir, insn_counter,
                      taken_builder);
})

T2C_OP(cadd, {
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, t2c_gen_rs2_addr(start, builder, ir));
    LLVMValueRef res = LLVMBuildAdd(*builder, val_rs1, val_rs2, "add");
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
})

T2C_OP(cswsp, {
    /* The decoder sets rs1 to sp for the stack-pointer forms. */
    T2C_MISALIGN_GUARD(ir, 4, JIT_MISALIGN_STORE | JIT_MISALIGN_COMPRESSED,
                       ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        { t2c_mmu_wrapper(sw)(builder, start, ir, rv, ir->pc, insn_counter); },
        {
            LLVMValueRef addr_rs2 = t2c_gen_rs2_addr(start, builder, ir);

            T2C_LLVM_GEN_LOAD_VMREG(rs2, 32, addr_rs2);
            LLVMValueRef cast_addr =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
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
#define T2C_SHIFT_ADD(inst, shift)                                          \
    T2C_OP(inst, {                                                          \
        T2C_LLVM_GEN_LOAD_VMREG(rs1, 32,                                    \
                                t2c_gen_rs1_addr(start, builder, ir));      \
        T2C_LLVM_GEN_LOAD_VMREG(rs2, 32,                                    \
                                t2c_gen_rs2_addr(start, builder, ir));      \
        LLVMValueRef shifted = T2C_LLVM_GEN_ALU32_IMM(Shl, val_rs1, shift); \
        LLVMValueRef res = LLVMBuildAdd(*builder, shifted, val_rs2, #inst); \
        LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir)); \
    })
T2C_SHIFT_ADD(sh1add, 1)
T2C_SHIFT_ADD(sh2add, 2)
T2C_SHIFT_ADD(sh3add, 3)
#undef T2C_SHIFT_ADD
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
            LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                         offsetof(riscv_t, X) / sizeof(int) + fuse[i].rd, true);
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32TypeInContext(t2c_context),
            LLVMGetParam(start, 0), &rd_offset, 1, "addr_rd");
        LLVMBuildStore(*builder,
                       LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                    fuse[i].imm, true),
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
        T2C_MISALIGN_GUARD(&fuse[i], 4, JIT_MISALIGN_STORE, ir->pc + 4 * i);
        IIF(RV32_HAS(SYSTEM))(
            {
                t2c_mmu_wrapper(sw)(builder, start, (rv_insn_t *) (&fuse[i]),
                                    rv, ir->pc + 4 * i, insn_counter);
            },
            {
                LLVMValueRef mem_loc =
                    t2c_gen_mem_loc(start, builder, (rv_insn_t *) (&fuse[i]),
                                    insn_counter, ir->pc + 4 * i);
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
        T2C_MISALIGN_GUARD(&fuse[i], 4, 0, ir->pc + 4 * i);
        IIF(RV32_HAS(SYSTEM))(
            {
                t2c_mmu_wrapper(lw)(builder, start, (rv_insn_t *) (&fuse[i]),
                                    rv, ir->pc + 4 * i, insn_counter);
            },
            {
                LLVMValueRef mem_loc =
                    t2c_gen_mem_loc(start, builder, (rv_insn_t *) (&fuse[i]),
                                    insn_counter, ir->pc + 4 * i);
                LLVMValueRef value = LLVMBuildLoad2(
                    *builder, LLVMInt32TypeInContext(t2c_context), mem_loc, "");
                t2c_gen_load_result(
                    builder, value,
                    t2c_gen_load_rd_addr(start, builder,
                                         (rv_insn_t *) (&fuse[i])),
                    false);
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
    LLVMBuildStore(
        *builder, LLVMConstInt(LLVMInt8TypeInContext(t2c_context), 0, false),
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, compressed),
                             LLVMInt8TypeInContext(t2c_context)));
    /* Store syscall number (imm) to a7 register */
    LLVMValueRef a7_offset =
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                     offsetof(riscv_t, X) / sizeof(int) + rv_reg_a7, true);
    LLVMValueRef addr_a7 =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32TypeInContext(t2c_context),
                              LLVMGetParam(start, 0), &a7_offset, 1, "addr_a7");
    LLVMBuildStore(
        *builder,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), ir->imm, true),
        addr_a7);
    /* Store PC and call ecall handler.
     * ECALL is at ir->pc + 4 (second instruction in fused pair).
     * Use offsetof() to compute proper byte offset for on_ecall.
     */
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->pc + 4,
                             t2c_gen_PC_addr(start, builder, ir));
    T2C_STORE_TIMER(*builder, start, insn_counter);
    t2c_gen_call_io_func(start, builder, offsetof(riscv_io_t, on_ecall),
                         LLVMVoidTypeInContext(t2c_context), NULL, 0);
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
            LLVMInt32TypeInContext(t2c_context),
            offsetof(riscv_t, X) / sizeof(int) + fuse[i].rs1, true);
        LLVMValueRef addr_rs1 = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32TypeInContext(t2c_context),
            LLVMGetParam(start, 0), &rs1_offset, 1, "addr_rs1");
        LLVMValueRef val_rs1 = LLVMBuildLoad2(
            *builder, LLVMInt32TypeInContext(t2c_context), addr_rs1, "val_rs1");
        LLVMValueRef res =
            LLVMBuildAdd(*builder, val_rs1,
                         LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                      fuse[i].imm, true),
                         "add");
        LLVMValueRef rd_offset =
            LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                         offsetof(riscv_t, X) / sizeof(int) + fuse[i].rd, true);
        LLVMValueRef addr_rd = LLVMBuildInBoundsGEP2(
            *builder, LLVMInt32TypeInContext(t2c_context),
            LLVMGetParam(start, 0), &rd_offset, 1, "addr_rd");
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
    LLVMValueRef rd_offset =
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                     offsetof(riscv_t, X) / sizeof(int) + ir->rd, true);
    LLVMValueRef addr_rd =
        LLVMBuildInBoundsGEP2(*builder, LLVMInt32TypeInContext(t2c_context),
                              LLVMGetParam(start, 0), &rd_offset, 1, "addr_rd");
    LLVMBuildStore(
        *builder,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), combined_imm, true),
        addr_rd);
})

/* fused LUI + LW: absolute address load
 * addr = ir->imm (lui << 12) + ir->imm2 (lw offset)
 * ir->rs2 = destination register for load
 */
T2C_OP(fuse9, {
    /* The interpreter and tier-1 write the LUI result as well. */
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
    IIF(RV32_HAS(SYSTEM))(
        {
            t2c_mmu_wrapper_fuse9(builder, start, ir, rv, ir->pc + 4,
                                  insn_counter);
        },
        {
            uint32_t addr_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
            LLVMValueRef addr =
                LLVMBuildAdd(*builder, t2c_gen_ram_base(start, builder),
                             LLVMConstInt(LLVMInt64TypeInContext(t2c_context),
                                          addr_imm, false),
                             "");
            LLVMValueRef cast_addr = LLVMBuildIntToPtr(
                *builder, addr,
                LLVMPointerType(LLVMInt32TypeInContext(t2c_context), 0),
                "cast");
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt32TypeInContext(t2c_context), cast_addr, "");
            t2c_gen_load_result(
                builder, value,
                ir->rs2 ? t2c_gen_rs2_addr(start, builder, ir) : NULL, false);
        });
})

/* fused LUI + SW: absolute address store
 * addr = ir->imm (lui << 12) + ir->imm2 (sw offset)
 * ir->rs1 = source register for store
 */
T2C_OP(fuse10, {
    /* The interpreter and tier-1 write the LUI result as well. */
    T2C_LLVM_GEN_STORE_IMM32(*builder, ir->imm,
                             t2c_gen_rd_addr(start, builder, ir));
    IIF(RV32_HAS(SYSTEM))(
        {
            t2c_mmu_wrapper_fuse10(builder, start, ir, rv, ir->pc + 4,
                                   insn_counter);
        },
        {
            uint32_t addr_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
            LLVMValueRef addr =
                LLVMBuildAdd(*builder, t2c_gen_ram_base(start, builder),
                             LLVMConstInt(LLVMInt64TypeInContext(t2c_context),
                                          addr_imm, false),
                             "");
            LLVMValueRef cast_addr = LLVMBuildIntToPtr(
                *builder, addr,
                LLVMPointerType(LLVMInt32TypeInContext(t2c_context), 0),
                "cast");
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
    T2C_MISALIGN_GUARD(ir, 4, 0, ir->pc);
    IIF(RV32_HAS(SYSTEM))(
        {
            t2c_mmu_wrapper_fuse11(builder, start, ir, rv, ir->pc,
                                   insn_counter);
        },
        {
            LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
            T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
            /* Compute address and load */
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMValueRef value = LLVMBuildLoad2(
                *builder, LLVMInt32TypeInContext(t2c_context), mem_loc, "");
            t2c_gen_load_result(builder, value,
                                t2c_gen_load_rd_addr(start, builder, ir),
                                false);
            /* Increment rs1 by imm2 (rd != rs1 guaranteed by fusion constraint)
             */
            LLVMValueRef inc_val =
                T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm2);
            LLVMBuildStore(*builder, inc_val, addr_rs1);
        });
})

/* fused SW + ADDI (post-increment store). This operation is deliberately formed
 * only without SYSTEM support, so the direct-RAM sequence preserves the
 * original instruction ordering without eliding an MMIO/fault boundary.
 */
T2C_OP(fuse13, {
    /* The block cost already counts the ADDI: it is set before fusion. */
    IIF(RV32_HAS(SYSTEM))(
        { __UNREACHABLE; },
        {
            T2C_MISALIGN_GUARD(ir, 4, JIT_MISALIGN_STORE, ir->pc);
            LLVMValueRef addr_rs1 = t2c_gen_rs1_addr(start, builder, ir);
            T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, addr_rs1);
            T2C_LLVM_GEN_LOAD_VMREG(rs2, 32,
                                    t2c_gen_rs2_addr(start, builder, ir));
            LLVMValueRef mem_loc =
                t2c_gen_mem_loc(start, builder, ir, insn_counter, ir->pc);
            LLVMBuildStore(*builder, val_rs2, mem_loc);
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
    /* Compute rd = rs1 + imm */
    T2C_LLVM_GEN_LOAD_VMREG(rs1, 32, t2c_gen_rs1_addr(start, builder, ir));
    LLVMValueRef res = T2C_LLVM_GEN_ALU32_IMM(Add, val_rs1, ir->imm);
    LLVMBuildStore(*builder, res, t2c_gen_rd_addr(start, builder, ir));
    /* Compare rd with 0 */
    T2C_LLVM_GEN_CMP_IMM32(NE, res, 0);
    /* Create taken and untaken branches */
    LLVMBasicBlockRef taken =
        LLVMAppendBasicBlockInContext(t2c_context, start, "taken");
    LLVMBuilderRef builder2 = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(builder2, taken);
    if (t2c_has_direct_target(rv, block, ir->branch_taken,
                              ir->pc + 4 + ir->imm2)) {
        *taken_builder = builder2;
    } else {
        /* PC = ir->pc + 4 + ir->imm2 (ADDI is 4 bytes, then branch offset) */
        t2c_gen_exit(builder2, start, ir->pc + 4 + ir->imm2, insn_counter);
        LLVMDisposeBuilder(builder2);
    }
    LLVMBasicBlockRef untaken =
        LLVMAppendBasicBlockInContext(t2c_context, start, "untaken");
    LLVMBuilderRef builder3 = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(builder3, untaken);
    if (t2c_has_direct_target(rv, block, ir->branch_untaken, block->pc_end)) {
        *untaken_builder = builder3;
    } else {
        /* PC = ir->pc + 8 (skip both ADDI and BNE, each 4 bytes) */
        t2c_gen_exit(builder3, start, ir->pc + 8, insn_counter);
        LLVMDisposeBuilder(builder3);
    }
    LLVMBuildCondBr(*builder, cmp, taken, untaken);
})
