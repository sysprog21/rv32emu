/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm/Config/llvm-config.h>
#include <stdlib.h>

/* LLVM version compatibility check.
 * T2C requires LLVM 18-21 for the following APIs:
 * - LLVMRunPasses (new pass manager, added in LLVM 13)
 * - LLVMGetInlineAsm with 9 arguments (CanThrow param added in LLVM 13)
 * - LLVMBuildAtomicRMW (stable across 18-21)
 * - LLVMCreateTargetMachine (stable across 18-21)
 *
 * Note: LLVM 22+ may deprecate MCJIT in favor of ORC JIT.
 * When upgrading beyond LLVM 21, review:
 * - MCJIT deprecation status
 * - Any LLVMGetInlineAsm signature changes
 * - Code model defaults for JIT on aarch64
 */
#if LLVM_VERSION_MAJOR < 18
#error "T2C requires LLVM 18 or later. Found LLVM " LLVM_VERSION_STRING
#elif LLVM_VERSION_MAJOR > 21
#warning "LLVM version > 21 detected. T2C is tested with LLVM 18-21."
#endif

#include "jit.h"
#include "mpool.h"
#include "riscv_private.h"

#define MAX_BLOCKS 8152

struct LLVM_block_map_entry {
    uint32_t pc;
    LLVMBasicBlockRef block;
};

struct LLVM_block_map {
    uint32_t count;
    struct LLVM_block_map_entry map[MAX_BLOCKS];
};

FORCE_INLINE void t2c_block_map_insert(struct LLVM_block_map *map,
                                       LLVMBasicBlockRef *entry,
                                       uint32_t pc)
{
    struct LLVM_block_map_entry map_entry = {
        .block = *entry,
        .pc = pc,
    };
    map->map[map->count++] = map_entry;
    return;
}

FORCE_INLINE LLVMBasicBlockRef t2c_block_map_search(struct LLVM_block_map *map,
                                                    uint32_t pc)
{
    for (uint32_t i = 0; i < map->count; i++) {
        if (map->map[i].pc == pc) {
            return map->map[i].block;
        }
    }
    return NULL;
}

/* T2C_OP generates code for each RISC-V instruction with batched cycle updates.
 *
 * Cycle optimization: Instead of updating rv->csr_cycle per instruction, we
 * increment a local counter (alloca) and store to csr_cycle only at block
 * exits. This reduces memory traffic significantly - LLVM's mem2reg pass
 * promotes the alloca to a register, making per-instruction increments free.
 *
 * The insn_counter parameter is an alloca created at function entry in
 * t2c_compile(). Before any LLVMBuildRetVoid(), T2C_STORE_TIMER must be called
 * to flush the accumulated count to rv->csr_cycle.
 */
#define T2C_OP(inst, code)                                                     \
    static void t2c_##inst(                                                    \
        LLVMBuilderRef *builder UNUSED, LLVMTypeRef *param_types UNUSED,       \
        LLVMValueRef start UNUSED, LLVMBasicBlockRef *entry UNUSED,            \
        LLVMBuilderRef *taken_builder UNUSED,                                  \
        LLVMBuilderRef *untaken_builder UNUSED, riscv_t *rv UNUSED,            \
        uint64_t mem_base UNUSED, block_t *block UNUSED, rv_insn_t *ir UNUSED, \
        LLVMValueRef insn_counter UNUSED)                                      \
    {                                                                          \
        /* Increment local instruction counter (promoted to register by LLVM)  \
         */                                                                    \
        LLVMValueRef cnt =                                                     \
            LLVMBuildLoad2(*builder, LLVMInt64Type(), insn_counter, "");       \
        cnt = LLVMBuildAdd(*builder, cnt,                                      \
                           LLVMConstInt(LLVMInt64Type(), 1, false), "");       \
        LLVMBuildStore(*builder, cnt, insn_counter);                           \
        code;                                                                  \
    }

#define T2C_LLVM_GEN_ADDR(reg, rv_member, ir_member)                          \
    FORCE_INLINE LLVMValueRef t2c_gen_##reg##_addr(                           \
        LLVMValueRef start, LLVMBuilderRef *builder, UNUSED rv_insn_t *ir)    \
    {                                                                         \
        LLVMValueRef offset = LLVMConstInt(                                   \
            LLVMInt32Type(),                                                  \
            offsetof(riscv_t, rv_member) / sizeof(int) + ir_member, true);    \
        return LLVMBuildInBoundsGEP2(*builder, LLVMInt32Type(),               \
                                     LLVMGetParam(start, 0), &offset, 1, ""); \
    }

T2C_LLVM_GEN_ADDR(rs1, X, ir->rs1);
T2C_LLVM_GEN_ADDR(rs2, X, ir->rs2);
T2C_LLVM_GEN_ADDR(rd, X, ir->rd);
#if RV32_HAS(EXT_C)
T2C_LLVM_GEN_ADDR(ra, X, rv_reg_ra);
T2C_LLVM_GEN_ADDR(sp, X, rv_reg_sp);
#endif
T2C_LLVM_GEN_ADDR(PC, PC, 0);
T2C_LLVM_GEN_ADDR(csr_cycle, csr_cycle, 0);

#define T2C_LLVM_GEN_STORE_IMM32(builder, val, addr) \
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt32Type(), val, true), addr)

#define T2C_LLVM_GEN_LOAD_VMREG(reg, size, addr) \
    LLVMValueRef val_##reg =                     \
        LLVMBuildLoad2(*builder, LLVMInt##size##Type(), addr, "");

#define T2C_LLVM_GEN_ALU32_IMM(op, dst, imm) \
    LLVMBuild##op(*builder, dst, LLVMConstInt(LLVMInt32Type(), imm, true), "")

#define T2C_LLVM_GEN_ALU64_IMM(op, dst, imm) \
    LLVMBuild##op(*builder, dst, LLVMConstInt(LLVMInt64Type(), imm, true), "")

#define T2C_LLVM_GEN_CMP(cond, rs1, rs2) \
    LLVMValueRef cmp = LLVMBuildICmp(*builder, LLVMInt##cond, rs1, rs2, "")

#define T2C_LLVM_GEN_CMP_IMM32(cond, rs1, imm)      \
    LLVMValueRef cmp =                              \
        LLVMBuildICmp(*builder, LLVMInt##cond, rs1, \
                      LLVMConstInt(LLVMInt32Type(), imm, false), "")

/* Store accumulated instruction count to rv->csr_cycle before block exit.
 * Called before every LLVMBuildRetVoid() to flush the counter.
 * The insn_counter is an alloca that LLVM's mem2reg promotes to a register.
 *
 * Uses atomic add (LLVMBuildAtomicRMW) for thread safety:
 * - Prevents torn reads if debugger/monitor reads csr_cycle concurrently
 * - Single atomic instruction vs non-atomic load-add-store sequence
 * - Monotonic ordering sufficient (no synchronization with other memory ops)
 *
 * Using csr_cycle instead of timer ensures:
 * - SYSTEM mode: timer interrupts work correctly (timer = csr_cycle + offset)
 * - Non-SYSTEM mode: RDCYCLE instruction returns accurate counts
 */
#define T2C_STORE_TIMER(bldr, start_val, counter)                         \
    do {                                                                  \
        LLVMValueRef _cycle_ptr =                                         \
            t2c_gen_csr_cycle_addr(start_val, &(bldr), NULL);             \
        LLVMValueRef _cnt =                                               \
            LLVMBuildLoad2(bldr, LLVMInt64Type(), counter, "");           \
        LLVMBuildAtomicRMW(bldr, LLVMAtomicRMWBinOpAdd, _cycle_ptr, _cnt, \
                           LLVMAtomicOrderingMonotonic, false);           \
    } while (0)

UNUSED FORCE_INLINE LLVMValueRef t2c_gen_mem_loc(LLVMValueRef start,
                                                 LLVMBuilderRef *builder,
                                                 UNUSED rv_insn_t *ir,
                                                 uint64_t mem_base)
{
    LLVMValueRef val_rs1 =
        LLVMBuildZExt(*builder,
                      LLVMBuildLoad2(*builder, LLVMInt32Type(),
                                     t2c_gen_rs1_addr(start, builder, ir), ""),
                      LLVMInt64Type(), "");
    LLVMValueRef addr =
        T2C_LLVM_GEN_ALU64_IMM(Add, val_rs1, ir->imm + mem_base);
    addr = LLVMBuildIntToPtr(*builder, addr,
                             LLVMPointerType(LLVMInt32Type(), 0), "");
    return addr;
}

/* Load and call a function pointer from rv->io struct.
 *
 * The byte_offset parameter is the offset from the start of riscv_t to the
 * target function pointer. Callers should use:
 *   - offsetof(riscv_t, io) + offsetof(riscv_io_t, on_ecall) for ecall
 *   - offsetof(riscv_t, io) + offsetof(riscv_io_t, on_ebreak) for ebreak
 *
 * This approach is correct regardless of RV32_HAS(SYSTEM) configuration,
 * which adds extra MMU function pointers to riscv_io_t.
 *
 * Uses manual pointer arithmetic (PtrToInt -> Add -> IntToPtr) to compute
 * the correct address, avoiding issues with GEP and struct layout
 * mismatches that can cause crashes on Apple Silicon.
 */
FORCE_INLINE void t2c_gen_call_io_func(LLVMValueRef start,
                                       LLVMBuilderRef *builder,
                                       LLVMTypeRef *param_types,
                                       size_t byte_offset)
{
    /* Convert rv pointer to integer, add offset, convert back to pointer */
    LLVMValueRef rv_ptr = LLVMGetParam(start, 0);
    LLVMValueRef rv_int =
        LLVMBuildPtrToInt(*builder, rv_ptr, LLVMInt64Type(), "");
    LLVMValueRef offset_val = LLVMConstInt(LLVMInt64Type(), byte_offset, false);
    LLVMValueRef func_ptr_addr = LLVMBuildAdd(*builder, rv_int, offset_val, "");
    LLVMValueRef func_ptr_ptr = LLVMBuildIntToPtr(
        *builder, func_ptr_addr,
        LLVMPointerType(LLVMPointerType(LLVMVoidType(), 0), 0), "");

    /* Load function pointer and call */
    LLVMValueRef io_func = LLVMBuildLoad2(
        *builder, LLVMPointerType(LLVMVoidType(), 0), func_ptr_ptr, "io_func");
    LLVMBuildCall2(*builder,
                   LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), io_func,
                   &rv_ptr, 1, "");
}

static LLVMTypeRef t2c_jit_cache_func_type;
static LLVMTypeRef t2c_jit_cache_struct_type;
static LLVMTypeRef t2c_inline_cache_struct_type;

#include "t2c_template.c"
#undef T2C_OP

static const void *dispatch_table[] = {
/* RV32 instructions */
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = t2c_##inst,
    RV_INSN_LIST
#undef _
/* Macro operation fusion instructions */
#define _(inst) [rv_insn_##inst] = t2c_##inst,
        FUSE_INSN_LIST
#undef _
};

FORCE_INLINE bool t2c_insn_is_terminal(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_ecall:
    case rv_insn_ebreak:
    case rv_insn_jalr:
    case rv_insn_mret:
#if RV32_HAS(SYSTEM)
    case rv_insn_sret:
#endif
#if RV32_HAS(EXT_C)
    case rv_insn_cjalr:
    case rv_insn_cjr:
    case rv_insn_cebreak:
#endif
        return true;
    }
    return false;
}

typedef void (*t2c_codegen_block_func_t)(LLVMBuilderRef *builder UNUSED,
                                         LLVMTypeRef *param_types UNUSED,
                                         LLVMValueRef start UNUSED,
                                         LLVMBasicBlockRef *entry UNUSED,
                                         LLVMBuilderRef *taken_builder UNUSED,
                                         LLVMBuilderRef *untaken_builder UNUSED,
                                         riscv_t *rv UNUSED,
                                         uint64_t mem_base UNUSED,
                                         block_t *block UNUSED,
                                         rv_insn_t *ir UNUSED,
                                         LLVMValueRef insn_counter UNUSED);

static void t2c_trace_ebb(LLVMBuilderRef *builder,
                          LLVMTypeRef *param_types UNUSED,
                          LLVMValueRef start,
                          LLVMBasicBlockRef *entry,
                          riscv_t *rv,
                          block_t *block,
                          set_t *set,
                          struct LLVM_block_map *map,
                          LLVMValueRef insn_counter)
{
    rv_insn_t *ir = block->ir_head;

    if (set_has(set, ir->pc))
        return;
    set_add(set, ir->pc);
    t2c_block_map_insert(map, entry, ir->pc);
    LLVMBuilderRef tk = NULL, utk = NULL;

    /* Get mem_base once at the start, not on every instruction */
    vm_attr_t *priv = PRIV(rv);
    uint64_t mem_base = (uint64_t) ((memory_t *) priv->mem)->mem_base;

    while (1) {
        ((t2c_codegen_block_func_t) dispatch_table[ir->opcode])(
            builder, param_types, start, entry, &tk, &utk, rv, mem_base, block,
            ir, insn_counter);
        if (!ir->next)
            break;
        ir = ir->next;
    }

    if (!t2c_insn_is_terminal(ir->opcode)) {
        /* For non-branch instructions that have fall-through continuation,
         * use the current builder since the instruction handler doesn't
         * create a separate taken/untaken path.
         * Branch instruction handlers (jal, beq, etc.) set tk/utk themselves,
         * but non-branch instruction handlers (lw, sw, add, etc.) don't.
         */
        if (!tk && ir->branch_taken)
            tk = *builder;
        if (!utk && ir->branch_untaken)
            utk = *builder;

        if (ir->branch_untaken) {
            /* Cache untaken_pc to avoid race condition with main thread */
            uint32_t untaken_pc = ir->branch_untaken->pc;
            if (set_has(set, untaken_pc)) {
                LLVMBuildBr(utk, t2c_block_map_search(map, untaken_pc));
            } else {
                block_t *blk = cache_get(rv->block_cache, untaken_pc, false);
                if (blk && blk->translatable
#if RV32_HAS(SYSTEM)
                    && blk->satp == block->satp
#endif
                ) {
                    LLVMBasicBlockRef untaken_entry =
                        LLVMAppendBasicBlock(start, "untaken_entry");
                    LLVMBuilderRef untaken_builder = LLVMCreateBuilder();
                    LLVMPositionBuilderAtEnd(untaken_builder, untaken_entry);
                    LLVMBuildBr(utk, untaken_entry);
                    t2c_trace_ebb(&untaken_builder, param_types, start,
                                  &untaken_entry, rv, blk, set, map,
                                  insn_counter);
                    LLVMDisposeBuilder(untaken_builder);
                }
            }
        }
        if (ir->branch_taken) {
            uint32_t taken_pc = ir->branch_taken->pc;
            if (set_has(set, taken_pc)) {
                LLVMBuildBr(tk, t2c_block_map_search(map, taken_pc));
            } else {
                /* Use stored taken_pc instead of re-reading
                 * ir->branch_taken->pc to avoid race condition with main thread
                 */
                block_t *blk = cache_get(rv->block_cache, taken_pc, false);
                if (blk && blk->translatable
#if RV32_HAS(SYSTEM)
                    && blk->satp == block->satp
#endif
                ) {
                    LLVMBasicBlockRef taken_entry =
                        LLVMAppendBasicBlock(start, "taken_entry");
                    LLVMBuilderRef taken_builder = LLVMCreateBuilder();
                    LLVMPositionBuilderAtEnd(taken_builder, taken_entry);
                    LLVMBuildBr(tk, taken_entry);
                    t2c_trace_ebb(&taken_builder, param_types, start,
                                  &taken_entry, rv, blk, set, map,
                                  insn_counter);
                    LLVMDisposeBuilder(taken_builder);
                }
            }
        }
    }

    if (tk && tk != *builder)
        LLVMDisposeBuilder(tk);
    if (utk && utk != *builder)
        LLVMDisposeBuilder(utk);
}

void t2c_compile(riscv_t *rv, block_t *block, pthread_mutex_t *cache_lock)
{
    /* Skip if already compiled (defensive check) */
    if (ATOMIC_LOAD(&block->hot2, ATOMIC_ACQUIRE)) {
        pthread_mutex_unlock(cache_lock);
        return;
    }

    LLVMModuleRef module = LLVMModuleCreateWithName("my_module");
    /* Build LLVM struct type that matches riscv_internal layout.
     *
     * Actual riscv_internal struct layout (see riscv_private.h):
     *   1. bool halt (1 byte + padding)
     *   2. uint32_t X[32] (128 bytes)
     *   3. uint32_t PC (4 bytes)
     *   4. uint64_t timer (8 bytes)
     *   5. riscv_user_t data (pointer, 8 bytes)
     *   6. riscv_io_t io (function pointers)
     *
     * Note: Additional fields may exist with SYSTEM/EXT_F/etc enabled.
     * The io struct offset is computed using offsetof() in
     * t2c_gen_call_io_func.
     */
    LLVMTypeRef io_members[] = {
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0)};
    LLVMTypeRef struct_io = LLVMStructType(io_members, 12, false);
    LLVMTypeRef arr_X = LLVMArrayType(LLVMInt32Type(), 32);
    /* Match actual riscv_internal layout order */
    LLVMTypeRef rv_members[] = {
        LLVMInt8Type(),                     /* halt */
        arr_X,                              /* X[32] */
        LLVMInt32Type(),                    /* PC */
        LLVMInt64Type(),                    /* timer */
        LLVMPointerType(LLVMVoidType(), 0), /* data */
        struct_io                           /* io */
    };
    LLVMTypeRef struct_rv = LLVMStructType(rv_members, 6, false);
    LLVMTypeRef param_types[] = {LLVMPointerType(struct_rv, 0)};
    LLVMValueRef start =
        LLVMAddFunction(module, "t2c_block",
                        LLVMFunctionType(LLVMVoidType(), param_types, 1, 0));

    /* Function type for calling T2C blocks via jit_cache lookup.
     * Must match the actual T2C block signature: void f(riscv_t *rv)
     * Using pointer type (not i64) for correct cross-block calling. */
    LLVMTypeRef t2c_args[1] = {LLVMPointerType(LLVMVoidType(), 0)};
    t2c_jit_cache_func_type =
        LLVMFunctionType(LLVMVoidType(), t2c_args, 1, false);

    /* jit_cache struct: { uint32_t seq, [pad], uint64_t key, void *entry }
     * C struct has 4 bytes padding after seq for 8-byte alignment of key.
     * LLVM doesn't add this padding automatically, so we add explicit i32 pad.
     * Field indices: 0=seq, 1=pad, 2=key, 3=entry */
    LLVMTypeRef jit_cache_memb[4] = {LLVMInt32Type(), LLVMInt32Type(),
                                     LLVMInt64Type(),
                                     LLVMPointerType(LLVMVoidType(), 0)};
    t2c_jit_cache_struct_type = LLVMStructType(jit_cache_memb, 4, false);

    /* inline_cache struct: { uint64_t key, void *entry }
     * Field indices: 0=key, 1=entry
     * No padding needed - already naturally aligned. */
    LLVMTypeRef inline_cache_memb[2] = {LLVMInt64Type(),
                                        LLVMPointerType(LLVMVoidType(), 0)};
    t2c_inline_cache_struct_type = LLVMStructType(inline_cache_memb, 2, false);

    LLVMBasicBlockRef first_block = LLVMAppendBasicBlock(start, "first_block");
    LLVMBuilderRef first_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(first_builder, first_block);

    /* Create instruction counter alloca in entry block for mem2reg promotion.
     * LLVM's mem2reg pass promotes allocas in the entry block to SSA registers,
     * eliminating per-instruction memory traffic. The counter is initialized to
     * 0 and incremented by each T2C_OP. Timer is updated only at block exits.
     */
    LLVMValueRef insn_counter =
        LLVMBuildAlloca(first_builder, LLVMInt64Type(), "insn_counter");
    LLVMBuildStore(first_builder, LLVMConstInt(LLVMInt64Type(), 0, false),
                   insn_counter);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(start, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMBuildBr(first_builder, entry);
    /* Allocate set on HEAP to avoid stack overflow.
     * set_t is 256KB (1024 * 32 * 8 bytes) in system mode - too large for
     * stack.
     */
    set_t *set = malloc(sizeof(set_t));
    if (!set) {
        rv_log_error("Failed to allocate set for T2C compilation");
        LLVMDisposeBuilder(first_builder);
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(module);
        pthread_mutex_unlock(cache_lock);
        return;
    }
    set_reset(set);
    struct LLVM_block_map map;
    map.count = 0;
    /* Translate custom IR into LLVM IR */
    t2c_trace_ebb(&builder, param_types, start, &entry, rv, block, set, &map,
                  insn_counter);

    block->is_compiling = true; /* Mark block as busy to prevent eviction */

    /* Release lock during expensive LLVM compilation.
     * IR translation is complete; block fields are no longer accessed until
     * we need to write results. SFENCE.VMA can now proceed with minimal delay.
     */
    pthread_mutex_unlock(cache_lock);

    /* Offload LLVM IR to LLVM backend */
    char *error = NULL, *triple = LLVMGetDefaultTargetTriple();
    LLVMExecutionEngineRef engine;
    LLVMTargetRef target;
    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
#if defined(__aarch64__)
    /* Initialize asm parser for inline assembly support in JIT.
     * Required for ARM64 ISB instruction emission in t2c_jit_cache_helper.
     */
    LLVMInitializeNativeAsmParser();
#endif
    if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
        rv_log_fatal("Failed to create target");
        abort();
    }
    /* Use PIC relocation mode for JIT code - helps with indirect calls.
     * Code model selection:
     * - Apple Silicon (ARM64 macOS): Use Small model to avoid MCJIT bugs with
     *   movz/movk sequences that Large model generates for 64-bit constants.
     *   ARM64's limited addressing modes make Large model problematic.
     * - Other platforms: Use Large model per LLVM MCJIT recommendations.
     */
#if defined(__aarch64__) && defined(__APPLE__)
    LLVMCodeModel code_model = LLVMCodeModelSmall;
#else
    LLVMCodeModel code_model = LLVMCodeModelLarge;
#endif
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, LLVMGetHostCPUName(), LLVMGetHostCPUFeatures(),
        LLVMCodeGenLevelNone, LLVMRelocPIC, code_model);
    LLVMPassBuilderOptionsRef pb_option = LLVMCreatePassBuilderOptions();
    /* Run LLVM optimization passes on the generated IR.
     *
     * Optimization level is configurable via CONFIG_T2C_OPT_LEVEL (Kconfig):
     *   O0: No optimization (fastest compile, for debugging only)
     *   O1: Basic optimizations (~50% faster compile than O3)
     *   O2: Balanced compilation/runtime trade-off
     *   O3: Aggressive optimizations, best runtime (default for production)
     *
     * system_jit_defconfig uses O1 for faster CI boot tests.
     * jit_defconfig uses O3 (default) for production performance.
     */
#ifndef CONFIG_T2C_OPT_LEVEL
#define CONFIG_T2C_OPT_LEVEL 3
#endif
    static_assert(CONFIG_T2C_OPT_LEVEL >= 0 && CONFIG_T2C_OPT_LEVEL <= 3,
                  "T2C optimization level must be 0-3");
    static const char *const t2c_opt_passes[] = {
        "default<O0>",
        "default<O1>",
        "default<O2>",
        "default<O3>",
    };
    LLVMRunPasses(module, t2c_opt_passes[CONFIG_T2C_OPT_LEVEL], tm, pb_option);

    /* Use LLVMCreateMCJITCompilerForModule with explicit options.
     * Unlike LLVMCreateExecutionEngineForModule, this respects our code model
     * setting which is critical for Apple Silicon where Small model is needed.
     */
    struct LLVMMCJITCompilerOptions options;
    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = CONFIG_T2C_OPT_LEVEL;
    options.CodeModel = code_model;

    if (LLVMCreateMCJITCompilerForModule(&engine, module, &options,
                                         sizeof(options), &error) != 0) {
        rv_log_fatal("Failed to create MCJIT execution engine: %s", error);
        LLVMDisposeMessage(error);
        abort();
    }

    /* Get function pointer - store in local variable first.
     * We'll write to block->func only under cache_lock to avoid data race
     * with eviction path that reads block->func.
     */
    exec_t2c_func_t func =
        (exec_t2c_func_t) LLVMGetPointerToGlobal(engine, start);

    /* Cleanup LLVM resources - execution engine owns the module */
    LLVMDisposeBuilder(first_builder);
    LLVMDisposeBuilder(builder);
    LLVMDisposePassBuilderOptions(pb_option);
    LLVMDisposeTargetMachine(tm);
    LLVMDisposeMessage(triple);

    /* Reacquire lock to update shared state.
     * All block field writes must happen under lock to avoid data races.
     */
    pthread_mutex_lock(cache_lock);

    block->is_compiling = false;

    /* Defensive check: if LLVM failed to generate code, don't mark as compiled.
     * Must dispose the engine to prevent memory leak.
     */
    if (!func) {
        /* Check if block was evicted - if so, free it and its IRs */
        if (block->should_free) {
            /* Free IRs that main thread skipped during deferred eviction */
            for (rv_insn_t *ir = block->ir_head, *next_ir; ir; ir = next_ir) {
                next_ir = ir->next;
                if (ir->fuse)
                    mpool_free(rv->fuse_mp, ir->fuse);
                mpool_free(rv->block_ir_mp, ir);
            }
            mpool_free(rv->block_mp, block);
        }
        LLVMDisposeExecutionEngine(engine);
        pthread_mutex_unlock(cache_lock);
        free(set);
        return;
    }

    /* Check if block was evicted while we were compiling.
     * If so, we are responsible for freeing it.
     */
    if (block->should_free) {
        /* Dispose engine (we own it) */
        LLVMDisposeExecutionEngine(engine);
        /* Free IRs that main thread skipped during deferred eviction */
        for (rv_insn_t *ir = block->ir_head, *next_ir; ir; ir = next_ir) {
            next_ir = ir->next;
            if (ir->fuse)
                mpool_free(rv->fuse_mp, ir->fuse);
            mpool_free(rv->block_ir_mp, ir);
        }
        mpool_free(rv->block_mp, block);
        pthread_mutex_unlock(cache_lock);
        free(set);
        return;
    }

#if RV32_HAS(SYSTEM)
    uint64_t key = (uint64_t) block->pc_start | ((uint64_t) block->satp << 32);

    /* Check invalidated flag after reacquiring lock. If SFENCE.VMA ran while
     * we were compiling, it set this flag and cleared jit_cache. We must not
     * re-add a stale entry. Dispose engine to prevent leak.
     */
    if (block->invalidated) {
        LLVMDisposeExecutionEngine(engine);
        pthread_mutex_unlock(cache_lock);
        free(set);
        return;
    }
#else
    uint64_t key = (uint64_t) block->pc_start;
#endif

    /* Write to block fields under lock to avoid data race with eviction */
    block->func = func;
    block->llvm_engine = engine;

    jit_cache_update(rv->jit_cache, key, block->func);

    /* Atomic store-release ensures all writes to block->func and jit_cache
     * are visible to other threads before they observe hot2=true.
     * Pairs with atomic load-acquire in rv_step().
     */
    ATOMIC_STORE(&block->hot2, true, ATOMIC_RELEASE);

    pthread_mutex_unlock(cache_lock);
    free(set);
}

struct jit_cache *jit_cache_init()
{
    return calloc(N_JIT_CACHE_ENTRIES, sizeof(struct jit_cache));
}

void jit_cache_exit(struct jit_cache *cache)
{
    free(cache);
}

struct inline_cache *inline_cache_init(void)
{
    return calloc(N_INLINE_CACHE_ENTRIES, sizeof(struct inline_cache));
}

void inline_cache_exit(struct inline_cache *cache)
{
    free(cache);
}

/* Clear all inline cache entries.
 * Called on SFENCE.VMA with rs1=0 (flush all) or when resetting emulator.
 * No seqlock needed - only main thread reads/writes inline cache.
 */
void inline_cache_clear(struct inline_cache *cache)
{
    memset(cache, 0, N_INLINE_CACHE_ENTRIES * sizeof(struct inline_cache));
}

/* Clear inline cache entries for a specific VA page.
 * Called on SFENCE.VMA with specific address.
 * Only clears entries whose PC falls within the target page.
 */
void inline_cache_clear_page(struct inline_cache *cache,
                             uint32_t va,
                             uint32_t satp)
{
    uint32_t va_page = va & ~(RV_PG_SIZE - 1);

    for (uint32_t i = 0; i < N_INLINE_CACHE_ENTRIES; i++) {
        uint64_t key = cache[i].key;
        if (!key)
            continue;

        uint32_t entry_pc = (uint32_t) key;
        uint32_t entry_satp = (uint32_t) (key >> 32);

        if (entry_satp == satp) {
            uint32_t entry_page = entry_pc & ~(RV_PG_SIZE - 1);
            if (entry_page == va_page) {
                cache[i].key = 0;
                cache[i].entry = NULL;
            }
        }
    }
}

/* Clear inline cache entries matching a specific key.
 * Used when evicting a compiled block to prevent stale entry pointers.
 */
void inline_cache_clear_key(struct inline_cache *cache, uint64_t key)
{
    if (!key)
        return;

    for (uint32_t i = 0; i < N_INLINE_CACHE_ENTRIES; i++) {
        if (cache[i].key == key) {
            cache[i].key = 0;
            cache[i].entry = NULL;
        }
    }
}

/* Dispose LLVM execution engine when a T2C-compiled block is freed.
 * The engine owns the memory where block->func points, so it must be
 * disposed before the block is freed to prevent dangling pointers.
 */
void t2c_dispose_engine(void *engine)
{
    if (engine)
        LLVMDisposeExecutionEngine((LLVMExecutionEngineRef) engine);
}

/* Wrapper for clear_cache_hot callback - disposes block's LLVM engine.
 * Called during shutdown via clear_cache_hot to clean up all remaining blocks.
 * Sets both llvm_engine and func to NULL to prevent use-after-free.
 *
 * DISABLE_UBSAN_FUNC: Disable UBSAN function pointer type check.
 * LLVM's cflags can cause function type metadata mismatch between t2c.c
 * and cache.c, triggering false positive when called via clear_func_t.
 */
DISABLE_UBSAN_FUNC
void t2c_dispose_block_engine(void *block)
{
    block_t *blk = (block_t *) block;
    if (blk && blk->llvm_engine) {
        LLVMDisposeExecutionEngine((LLVMExecutionEngineRef) blk->llvm_engine);
        blk->llvm_engine = NULL;
        blk->func = NULL; /* func pointed into engine's memory */
    }
}

void jit_cache_update(struct jit_cache *cache, uint64_t key, void *entry)
{
    /* XOR high 32 bits (satp) with low 32 bits (pc) before masking.
     * This distributes entries from different address spaces across the table,
     * reducing cache thrashing when multiple processes share virtual addresses.
     */
    uint32_t pos =
        ((uint32_t) key ^ (uint32_t) (key >> 32)) & (N_JIT_CACHE_ENTRIES - 1);

    /* Seqlock write pattern:
     * 1. Increment seq to odd (signals write in progress)
     * 2. Write entry and key (atomic relaxed to avoid data race with readers)
     * 3. Increment seq to even (signals write complete)
     * Release ordering on seq ensures readers see consistent state.
     */
    uint32_t seq = ATOMIC_LOAD(&cache[pos].seq, ATOMIC_RELAXED);
    ATOMIC_STORE(&cache[pos].seq, seq + 1, ATOMIC_RELEASE); /* odd = writing */
    ATOMIC_STORE(&cache[pos].entry, entry, ATOMIC_RELEASE);
    ATOMIC_STORE(&cache[pos].key, key, ATOMIC_RELEASE);
    ATOMIC_STORE(&cache[pos].seq, seq + 2, ATOMIC_RELEASE); /* even = done */
}

void jit_cache_clear(struct jit_cache *cache)
{
    /* Clear all entries using seqlock pattern for thread-safe invalidation. */
    for (uint32_t i = 0; i < N_JIT_CACHE_ENTRIES; i++) {
        uint32_t seq = ATOMIC_LOAD(&cache[i].seq, ATOMIC_RELAXED);
        ATOMIC_STORE(&cache[i].seq, seq + 1,
                     ATOMIC_RELEASE); /* odd = writing */
        ATOMIC_STORE(&cache[i].entry, NULL, ATOMIC_RELEASE);
        ATOMIC_STORE(&cache[i].key, 0, ATOMIC_RELEASE);
        ATOMIC_STORE(&cache[i].seq, seq + 2, ATOMIC_RELEASE); /* even = done */
    }
}

/* Selectively clear jit_cache entries for a specific VA page and SATP.
 * This is more efficient than jit_cache_clear() for address-specific
 * SFENCE.VMA operations, avoiding unnecessary invalidation of unrelated
 * entries.
 *
 * Caller must hold cache_lock (rv->cache_lock) to synchronize with the T2C
 * compilation thread. The T2C thread holds this lock when updating jit_cache
 * entries via jit_cache_update(). Without this lock:
 * 1. T2C thread could be writing an entry while we read/clear it
 * 2. Race could cause partially-written keys to be matched incorrectly
 * 3. Entry could be cleared right after T2C writes it, causing wasted work
 *
 * The seqlock pattern used here only protects the main thread's JIT cache
 * lookups from seeing torn reads - it does not provide mutual exclusion for
 * writers. The cache_lock provides that exclusion between the main thread
 * (SFENCE.VMA) and T2C thread (block compilation).
 */
void jit_cache_clear_page(struct jit_cache *cache, uint32_t va, uint32_t satp)
{
    uint32_t va_page = va & ~(RV_PG_SIZE - 1);

    for (uint32_t i = 0; i < N_JIT_CACHE_ENTRIES; i++) {
        uint64_t key = ATOMIC_LOAD(&cache[i].key, ATOMIC_RELAXED);
        if (!key)
            continue;

        uint32_t entry_pc = (uint32_t) key;
        uint32_t entry_satp = (uint32_t) (key >> 32);

        /* Match entries with same SATP and PC in the target page */
        if (entry_satp == satp) {
            uint32_t entry_page = entry_pc & ~(RV_PG_SIZE - 1);
            if (entry_page == va_page) {
                /* Clear using seqlock pattern */
                uint32_t seq = ATOMIC_LOAD(&cache[i].seq, ATOMIC_RELAXED);
                ATOMIC_STORE(&cache[i].seq, seq + 1,
                             ATOMIC_RELEASE); /* odd = writing */
                ATOMIC_STORE(&cache[i].entry, NULL, ATOMIC_RELEASE);
                ATOMIC_STORE(&cache[i].key, 0, ATOMIC_RELEASE);
                ATOMIC_STORE(&cache[i].seq, seq + 2,
                             ATOMIC_RELEASE); /* even = done */
            }
        }
    }
}
