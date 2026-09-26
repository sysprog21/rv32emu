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

#if RV32_HAS(SYSTEM_MMIO)
/* Layout invariants for the T2C inline dTLB fast path.  Mirrors the asserts
 * in jit.c so the LLVM IR emission can hard-code offsets safely.
 */
_Static_assert(sizeof(tlb_entry_t) == 16, "tlb_entry_t must be 16 bytes");
_Static_assert(offsetof(tlb_entry_t, vpn) == 0, "tlb_entry_t.vpn offset");
_Static_assert(offsetof(tlb_entry_t, ppn) == 4, "tlb_entry_t.ppn offset");
_Static_assert(offsetof(tlb_entry_t, perm) == 12, "tlb_entry_t.perm offset");
_Static_assert(offsetof(tlb_entry_t, valid) == 13, "tlb_entry_t.valid offset");
_Static_assert(offsetof(tlb_entry_t, dirty) == 14, "tlb_entry_t.dirty offset");
_Static_assert(offsetof(tlb_entry_t, level) == 15, "tlb_entry_t.level offset");
_Static_assert((TLB_SIZE & (TLB_SIZE - 1)) == 0,
               "TLB_SIZE must be a power of two");
/* Mirrors the assert in jit.c: the T1 x86-64 fast path uses opcode 0x83 /4
 * (imm8 sign-extended) for `and esi, TLB_SIZE - 1`.  Keep both asserts in
 * lockstep so a TLB_SIZE bump here cannot silently diverge from the T1
 * emitter's encoding.
 */
_Static_assert(TLB_SIZE <= 128,
               "TLB_SIZE > 128 needs imm32 AND encoding in the x86-64 "
               "fast path (see jit.c emit path)");
_Static_assert(RV_PG_SHIFT == 12, "fast path assumes 4 KiB pages");
#endif

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

/* T2C_OP generates code for each RISC-V instruction.
 *
 * Cycle counting: instead of updating rv->csr_cycle per instruction, each block
 * entry adds the block's cycle cost to a local counter (alloca), which is
 * stored to csr_cycle only at region exits. LLVM's mem2reg pass promotes the
 * alloca to a register. The cost counts the instructions a fused one replaced,
 * as the interpreter does.
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
        code;                                                                  \
    }

/* Index of rv->X[0] among the 32-bit words of riscv_t. */
#define X_BASE (offsetof(riscv_t, X) / sizeof(uint32_t))

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

FORCE_INLINE LLVMValueRef t2c_gen_rv_field_ptr(LLVMValueRef start,
                                               LLVMBuilderRef *builder,
                                               size_t byte_offset,
                                               LLVMTypeRef field_type)
{
    LLVMValueRef rv_bytes =
        LLVMBuildBitCast(*builder, LLVMGetParam(start, 0),
                         LLVMPointerType(LLVMInt8Type(), 0), "");
    LLVMValueRef offset =
        LLVMConstInt(LLVMInt64Type(), (uint64_t) byte_offset, false);
    LLVMValueRef field_bytes =
        LLVMBuildGEP2(*builder, LLVMInt8Type(), rv_bytes, &offset, 1, "");
    return LLVMBuildBitCast(*builder, field_bytes,
                            LLVMPointerType(field_type, 0), "");
}

FORCE_INLINE LLVMValueRef t2c_gen_csr_cycle_addr(LLVMValueRef start,
                                                 LLVMBuilderRef *builder,
                                                 UNUSED rv_insn_t *ir)
{
    return t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, csr_cycle),
                                LLVMInt64Type());
}

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

/* Branch on cond to the rare block, else to the common one, and keep the rare
 * path out of the way of the common one.
 */
static void t2c_gen_unlikely_br(LLVMBuilderRef builder,
                                LLVMValueRef cond,
                                LLVMBasicBlockRef rare,
                                LLVMBasicBlockRef common)
{
    LLVMValueRef br = LLVMBuildCondBr(builder, cond, rare, common);
    LLVMContextRef ctx = LLVMGetGlobalContext();
    LLVMMetadataRef weights[] = {
        LLVMMDStringInContext2(ctx, "branch_weights", 14),
        LLVMValueAsMetadata(LLVMConstInt(LLVMInt32Type(), 1, false)),
        LLVMValueAsMetadata(LLVMConstInt(LLVMInt32Type(), 1 << 20, false)),
    };
    LLVMSetMetadata(
        br, LLVMGetMDKindIDInContext(ctx, "prof", 4),
        LLVMMetadataAsValue(ctx, LLVMMDNodeInContext2(ctx, weights, 3)));
}

/* Tier-2 counterpart of emit_misalign_guard() in jit.c: when a halfword or word
 * access at vaddr is misaligned, and misaligned accesses are not allowed, store
 * the faulting pc, raise the exception through jit_misaligned_trap(), and
 * return from the block. Generated code keeps guest registers in rv->X, so
 * nothing needs writing back first. A constant vaddr folds the branch. The
 * helper runs in this process, so its address can be embedded directly.
 */
static void t2c_gen_misalign_guard(LLVMBuilderRef *builder,
                                   LLVMValueRef start,
                                   riscv_t *rv,
                                   LLVMValueRef vaddr,
                                   uint32_t size,
                                   uint32_t flags,
                                   uint32_t pc,
                                   LLVMValueRef insn_counter)
{
    if (PRIV(rv)->allow_misalign || rv->no_trap_vector)
        return;

    LLVMValueRef low = LLVMBuildAnd(
        *builder, vaddr, LLVMConstInt(LLVMInt32Type(), size - 1, false), "");
    LLVMValueRef misaligned =
        LLVMBuildICmp(*builder, LLVMIntNE, low,
                      LLVMConstInt(LLVMInt32Type(), 0, false), "misaligned");
    LLVMBasicBlockRef trap = LLVMAppendBasicBlock(start, "misaligned");
    LLVMBasicBlockRef aligned = LLVMAppendBasicBlock(start, "aligned");
    t2c_gen_unlikely_br(*builder, misaligned, trap, aligned);

    LLVMPositionBuilderAtEnd(*builder, trap);
    LLVMBuildStore(*builder, LLVMConstInt(LLVMInt32Type(), pc, false),
                   t2c_gen_PC_addr(start, builder, NULL));
    LLVMTypeRef param_types[] = {LLVMPointerType(LLVMVoidType(), 0),
                                 LLVMInt32Type(), LLVMInt32Type()};
    LLVMTypeRef fn_type =
        LLVMFunctionType(LLVMVoidType(), param_types, 3, false);
    LLVMValueRef fn = LLVMConstIntToPtr(
        LLVMConstInt(LLVMInt64Type(), (uintptr_t) &jit_misaligned_trap, false),
        LLVMPointerType(fn_type, 0));
    LLVMValueRef args[] = {LLVMGetParam(start, 0), vaddr,
                           LLVMConstInt(LLVMInt32Type(), flags, false)};
    LLVMBuildCall2(*builder, fn_type, fn, args, 3, "");
    T2C_STORE_TIMER(*builder, start, insn_counter);
    LLVMBuildRetVoid(*builder);

    LLVMPositionBuilderAtEnd(*builder, aligned);

    /* State the proven alignment, so that later checks of the same address bits
     * fold away.
     */
    unsigned assume_id = LLVMLookupIntrinsicID("llvm.assume", 11);
    LLVMValueRef assume = LLVMGetIntrinsicDeclaration(
        LLVMGetGlobalParent(start), assume_id, NULL, 0);
    LLVMValueRef aligned_cond = LLVMBuildNot(*builder, misaligned, "");
    LLVMBuildCall2(
        *builder,
        LLVMIntrinsicGetType(LLVMGetGlobalContext(), assume_id, NULL, 0),
        assume, &aligned_cond, 1, "");
}

/* The address rs1 + imm that a load or store accesses. */
static LLVMValueRef t2c_gen_vaddr(LLVMValueRef start,
                                  LLVMBuilderRef *builder,
                                  rv_insn_t *ir)
{
    LLVMValueRef val_rs1 = LLVMBuildLoad2(
        *builder, LLVMInt32Type(), t2c_gen_rs1_addr(start, builder, ir), "");
    return LLVMBuildAdd(*builder, val_rs1,
                        LLVMConstInt(LLVMInt32Type(), ir->imm, true), "vaddr");
}

/* Load the function pointer at io_field in rv->io, and call it with rv and the
 * n values in args, returning its result.
 *
 * io_field is an offset in riscv_io_t, which is correct whether or not SYSTEM
 * adds the MMU function pointers. The pointer is loaded at run time, from the
 * real riscv_t base rather than the synthetic prefix type used for the T2C
 * function parameter.
 */
static LLVMValueRef t2c_gen_call_io_func(LLVMValueRef start,
                                         LLVMBuilderRef *builder,
                                         size_t io_field,
                                         LLVMTypeRef ret_type,
                                         LLVMValueRef *args,
                                         unsigned n)
{
    LLVMValueRef params[3] = {LLVMGetParam(start, 0)};
    LLVMTypeRef types[3] = {LLVMPointerType(LLVMVoidType(), 0)};
    assert(n < 3);
    for (unsigned i = 0; i < n; i++) {
        params[i + 1] = args[i];
        types[i + 1] = LLVMTypeOf(args[i]);
    }
    LLVMTypeRef fn_type = LLVMFunctionType(ret_type, types, n + 1, false);
    LLVMTypeRef fn_ptr_type = LLVMPointerType(fn_type, 0);
    LLVMValueRef fn = LLVMBuildLoad2(
        *builder, fn_ptr_type,
        t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, io) + io_field,
                             fn_ptr_type),
        "");
    return LLVMBuildCall2(*builder, fn_type, fn, params, n + 1, "");
}

static LLVMTypeRef t2c_jit_cache_func_type;
static LLVMTypeRef t2c_jit_cache_struct_type;
static LLVMTypeRef t2c_inline_cache_struct_type;

/* Leave the region at pc: store it, flush the instruction count, return. */
static void t2c_gen_exit(LLVMBuilderRef builder,
                         LLVMValueRef start,
                         uint32_t pc,
                         LLVMValueRef insn_counter)
{
    T2C_LLVM_GEN_STORE_IMM32(builder, pc,
                             t2c_gen_PC_addr(start, &builder, NULL));
    T2C_STORE_TIMER(builder, start, insn_counter);
    LLVMBuildRetVoid(builder);
}

/* Region state, kept in file scope because a single thread runs t2c_compile.
 * t2c_region_insns counts the IR instructions traced into the current region;
 * past T2C_REGION_BUDGET, successors are linked through the jit-cache rather
 * than traced, which bounds the code a region duplicates from others.
 * t2c_predicted_pc passes an indirect jump's predicted target from its handler
 * to t2c_trace_ebb().
 */
#define T2C_REGION_BUDGET 2048
static uint32_t t2c_region_insns;
static uint32_t t2c_predicted_pc;

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

FORCE_INLINE bool t2c_insn_is_terminal(uint16_t opcode)
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
                          LLVMTypeRef *param_types,
                          LLVMValueRef start,
                          LLVMBasicBlockRef *entry,
                          riscv_t *rv,
                          block_t *block,
                          set_t *set,
                          struct LLVM_block_map *map,
                          LLVMValueRef insn_counter);

/* Continue from the path at the end of from to the block at pc: branch to it if
 * the region already holds it, trace it into the region, or, past the region
 * budget, jump to its compiled code through the jit-cache. A block that cannot
 * be translated ends the path with an exit to pc. A path that already ended, as
 * a branch handler ends one whose target it could not validate, is left alone.
 */
static void t2c_follow(LLVMBuilderRef from,
                       uint32_t pc,
                       LLVMTypeRef *param_types,
                       LLVMValueRef start,
                       riscv_t *rv,
                       block_t *block,
                       set_t *set,
                       struct LLVM_block_map *map,
                       LLVMValueRef insn_counter)
{
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(from)))
        return;
    if (set_has(set, pc)) {
        LLVMBuildBr(from, t2c_block_map_search(map, pc));
        return;
    }
    block_t *blk = t2c_check_valid_blk(rv, block, pc);
    if (!blk) {
        t2c_gen_exit(from, start, pc, insn_counter);
        return;
    }
    if (t2c_region_insns >= T2C_REGION_BUDGET) {
        t2c_jit_cache_helper(&from, start,
                             LLVMConstInt(LLVMInt32Type(), pc, false), rv,
                             block, NULL, insn_counter);
        return;
    }
    LLVMBasicBlockRef next_entry = LLVMAppendBasicBlock(start, "next_entry");
    LLVMBuilderRef next = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(next, next_entry);
    LLVMBuildBr(from, next_entry);
    t2c_trace_ebb(&next, param_types, start, &next_entry, rv, blk, set, map,
                  insn_counter);
    LLVMDisposeBuilder(next);
}

static void t2c_trace_ebb(LLVMBuilderRef *builder,
                          LLVMTypeRef *param_types,
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

    LLVMValueRef cnt =
        LLVMBuildLoad2(*builder, LLVMInt64Type(), insn_counter, "");
    LLVMBuildStore(*builder,
                   T2C_LLVM_GEN_ALU64_IMM(Add, cnt, block->cycle_cost),
                   insn_counter);

    /* Get mem_base once at the start, not on every instruction */
    vm_attr_t *priv = PRIV(rv);
    uint64_t mem_base = (uint64_t) ((memory_t *) priv->mem)->mem_base;

    while (1) {
        t2c_region_insns++;
        ((t2c_codegen_block_func_t) dispatch_table[ir->opcode])(
            builder, param_types, start, entry, &tk, &utk, rv, mem_base, block,
            ir, insn_counter);
        if (!ir->next)
            break;
        ir = ir->next;
    }

    if (t2c_insn_is_terminal(ir->opcode)) {
        /* An indirect jump with a dominant target left that path open. */
        uint32_t predicted = t2c_predicted_pc;
        t2c_predicted_pc = 0;
        if (predicted)
            t2c_follow(tk, predicted, param_types, start, rv, block, set, map,
                       insn_counter);
    } else {
        /* For non-branch instructions that have fall-through continuation,
         * use the current builder since the instruction handler doesn't
         * create a separate taken/untaken path.
         * Branch instruction handlers (jal, beq, etc.) set tk/utk themselves,
         * but non-branch instruction handlers (lw, sw, add, etc.) don't.
         */
        /* Read each chain pointer once, atomically: the emulator thread
         * installs edges without holding cache_lock, so a plain load here
         * would race with it. The block it names is in the cache and cannot
         * be evicted while this thread holds the lock, so whichever value
         * arrives is safe to follow.
         */
        const rv_insn_t *taken = ATOMIC_LOAD(&ir->branch_taken, ATOMIC_RELAXED);
        const rv_insn_t *untaken =
            ATOMIC_LOAD(&ir->branch_untaken, ATOMIC_RELAXED);

        if (!tk && taken)
            tk = *builder;
        if (!utk && untaken)
            utk = *builder;

        if (untaken)
            t2c_follow(utk, untaken->pc, param_types, start, rv, block, set,
                       map, insn_counter);
        if (taken)
            t2c_follow(tk, taken->pc, param_types, start, rv, block, set, map,
                       insn_counter);

        /* Ensure the basic block has a terminator. When a block ends with a
         * non-branching instruction (e.g., addi, lw, sw) whose branch_taken
         * and branch_untaken are both NULL, no terminator is emitted above.
         * This produces malformed LLVM IR that crashes LLVMRunPasses.
         *
         * Store the next PC (block->pc_end) and return to the interpreter.
         */
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(*builder)))
            t2c_gen_exit(*builder, start, block->pc_end, insn_counter);
    }

    if (tk && tk != *builder)
        LLVMDisposeBuilder(tk);
    if (utk && utk != *builder)
        LLVMDisposeBuilder(utk);
}

/* Guest register promotion.
 *
 * Instruction handlers read and write guest registers in rv->X directly, and
 * LLVM must keep every such access: it cannot tell rv->X from guest memory,
 * which is reached through integer-to-pointer casts. Redirect them to one local
 * variable per register instead, which LLVM keeps in host registers. The
 * variables are filled from rv->X on entry, written back before any call or
 * return, since callees and the dispatcher read rv->X, and refilled after a
 * call that returns here, since callees may write it. The guest register an
 * address names: an i32 GEP off rv with a constant index into X. -1 for any
 * other address.
 */
static int t2c_guest_reg(LLVMValueRef ptr, LLVMValueRef rv_arg)
{
    if (!LLVMIsAGetElementPtrInst(ptr) || LLVMGetOperand(ptr, 0) != rv_arg ||
        LLVMGetNumOperands(ptr) != 2 ||
        LLVMGetGEPSourceElementType(ptr) != LLVMInt32Type())
        return -1;
    LLVMValueRef idx = LLVMGetOperand(ptr, 1);
    if (!LLVMIsAConstantInt(idx))
        return -1;
    long long i = LLVMConstIntGetSExtValue(idx);
    return i >= (long long) X_BASE && i < (long long) X_BASE + N_RV_REGS
               ? (int) (i - X_BASE)
               : -1;
}

/* Whether a use of rv, other than through t2c_guest_reg(), may reach X. Such a
 * function is left alone, so a promoted value can never be read stale.
 */
static bool t2c_may_alias_x(LLVMValueRef user, LLVMValueRef rv_arg)
{
    if (LLVMIsACallInst(user))
        return false; /* calls are bracketed by write-back and refill */
    if (!LLVMIsAGetElementPtrInst(user))
        return true;
    if (t2c_guest_reg(user, rv_arg) >= 0)
        return false;
    if (LLVMGetNumOperands(user) != 2 ||
        !LLVMIsAConstantInt(LLVMGetOperand(user, 1)))
        return true;
    long long scale;
    LLVMTypeRef type = LLVMGetGEPSourceElementType(user);
    if (type == LLVMInt8Type())
        scale = 1;
    else if (type == LLVMInt32Type())
        scale = 4;
    else if (type == LLVMInt64Type())
        scale = 8;
    else
        return true;
    long long off = LLVMConstIntGetSExtValue(LLVMGetOperand(user, 1)) * scale;
    long long x = offsetof(riscv_t, X);
    return off + 8 > x && off < x + (long long) sizeof(((riscv_t *) 0)->X);
}

/* Copy the registers in mask from one set of addresses to the other. */
static void t2c_copy_regs(LLVMBuilderRef b,
                          uint32_t mask,
                          const LLVMValueRef *from,
                          const LLVMValueRef *to)
{
    for (int r = 1; r < N_RV_REGS; r++) {
        if (mask & (UINT32_C(1) << r))
            LLVMBuildStore(b, LLVMBuildLoad2(b, LLVMInt32Type(), from[r], ""),
                           to[r]);
    }
}

/* Whether the instructions after a call run straight to a return without
 * touching guest registers or calling again. Such a call needs no refill, and
 * the return no write-back of its own.
 */
static bool t2c_runs_to_return(LLVMValueRef call, const LLVMValueRef *var)
{
    for (LLVMValueRef in = LLVMGetNextInstruction(call); in;
         in = LLVMGetNextInstruction(in)) {
        if (LLVMIsAReturnInst(in))
            return true;
        if (LLVMIsACallInst(in) && !LLVMIsAIntrinsicInst(in))
            return false;
        LLVMValueRef ptr = LLVMIsALoadInst(in)    ? LLVMGetOperand(in, 0)
                           : LLVMIsAStoreInst(in) ? LLVMGetOperand(in, 1)
                                                  : NULL;
        for (int r = 1; ptr && r < N_RV_REGS; r++) {
            if (ptr == var[r])
                return false;
        }
    }
    return false;
}

static void t2c_promote_guest_regs(LLVMValueRef fn, LLVMBasicBlockRef entry)
{
    LLVMValueRef rv_arg = LLVMGetParam(fn, 0);
    uint32_t used = 0, written = 0;
    for (LLVMUseRef use = LLVMGetFirstUse(rv_arg); use;
         use = LLVMGetNextUse(use)) {
        LLVMValueRef user = LLVMGetUser(use);
        if (t2c_may_alias_x(user, rv_arg))
            goto keep_in_memory;

        /* x0 stays in memory: it must read zero, whatever a handler stores
         * there. A promoted address must only be loaded from or stored to.
         */
        int r = t2c_guest_reg(user, rv_arg);
        if (r <= 0)
            continue;
        uint32_t bit = UINT32_C(1) << r;
        for (LLVMUseRef u = LLVMGetFirstUse(user); u; u = LLVMGetNextUse(u)) {
            LLVMValueRef in = LLVMGetUser(u);
            bool store = LLVMIsAStoreInst(in) && LLVMGetOperand(in, 1) == user;
            if (!(LLVMIsALoadInst(in) || store) ||
                LLVMGetOrdering(in) != LLVMAtomicOrderingNotAtomic)
                goto keep_in_memory;
            used |= bit;
            if (store)
                written |= bit;
        }
    }
    if (!used)
        return;

    /* Fill the variables in the entry block, where mem2reg promotes them. */
    LLVMBuilderRef b = LLVMCreateBuilder();
    LLVMPositionBuilderBefore(b, LLVMGetBasicBlockTerminator(entry));
    LLVMValueRef var[N_RV_REGS] = {0}, home[N_RV_REGS] = {0};
    for (int r = 1; r < N_RV_REGS; r++) {
        if (!(used & (UINT32_C(1) << r)))
            continue;
        LLVMValueRef idx = LLVMConstInt(LLVMInt32Type(), X_BASE + r, false);
        home[r] =
            LLVMBuildInBoundsGEP2(b, LLVMInt32Type(), rv_arg, &idx, 1, "x");
        var[r] = LLVMBuildAlloca(b, LLVMInt32Type(), "reg");
    }
    t2c_copy_regs(b, used, home, var);

    /* Redirect the handlers' accesses. The addresses themselves stay users of
     * rv, so the walk is unaffected.
     */
    for (LLVMUseRef use = LLVMGetFirstUse(rv_arg); use;
         use = LLVMGetNextUse(use)) {
        LLVMValueRef user = LLVMGetUser(use);
        int r = t2c_guest_reg(user, rv_arg);
        if (r > 0 && user != home[r])
            LLVMReplaceAllUsesWith(user, var[r]);
    }

    /* Write back before calls and returns; refill after calls that return here
     * and then touch guest registers again.
     */
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
        bool written_back = false;
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in)) {
            bool call = LLVMIsACallInst(in) &&
                        !LLVMIsAInlineAsm(LLVMGetCalledValue(in)) &&
                        !LLVMIsAIntrinsicInst(in);
            if (LLVMIsAReturnInst(in)) {
                if (!written_back) {
                    LLVMPositionBuilderBefore(b, in);
                    t2c_copy_regs(b, written, var, home);
                }
                continue;
            }
            if (!call)
                continue;
            LLVMPositionBuilderBefore(b, in);
            t2c_copy_regs(b, written, var, home);
            /* A tail call does not come back, and nothing may follow it. */
            written_back =
                LLVMGetTailCallKind(in) == LLVMTailCallKindMustTail ||
                t2c_runs_to_return(in, var);
            if (written_back)
                continue;
            LLVMValueRef next = LLVMGetNextInstruction(in);
            LLVMPositionBuilderBefore(b, next);
            t2c_copy_regs(b, used, home, var);
            in = LLVMGetPreviousInstruction(next);
        }
    }
    LLVMDisposeBuilder(b);
    return;

keep_in_memory:
    rv_log_debug("T2C: guest registers left in memory");
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
     * Synthetic riscv_internal prefix layout used by typed GEPs (see
     * riscv_private.h):
     *   1. bool halt (1 byte + padding)
     *   2. uint32_t X[32] (128 bytes)
     *   3. uint32_t PC (4 bytes)
     *   4. uint64_t timer (8 bytes)
     *   5. riscv_user_t data (pointer, 8 bytes)
     *   6. riscv_io_t io (function pointers)
     *
     * Fields beyond this prefix (for example csr_cycle, csr_satp, jit_cache,
     * inline_cache, and dtlb) are accessed via byte offsets from the real rv
     * base pointer, not via typed inbounds GEPs on this truncated struct.
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
        goto abandon;
    }
    set_init(set);
    struct LLVM_block_map map;
    map.count = 0;
    /* Translate custom IR into LLVM IR */
    t2c_region_insns = 0;
    t2c_predicted_pc = 0;
    t2c_trace_ebb(&builder, param_types, start, &entry, rv, block, set, &map,
                  insn_counter);
    t2c_promote_guest_regs(start, first_block);

    /* Malformed IR would otherwise surface as a crash deep inside an LLVM pass.
     * Leave such a block to tier-1 instead.
     */
    char *verify_msg = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &verify_msg)) {
        rv_log_error("T2C built invalid IR for 0x%08x; skipped: %s",
                     block->pc_start, verify_msg);
        LLVMDisposeMessage(verify_msg);
        goto abandon;
    }
    LLVMDisposeMessage(verify_msg);

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
     * - Arm64: Use Small model. The Large model materializes every 64-bit
     *   constant with a movz/movk sequence, which MCJIT mishandles on Apple
     *   Silicon and which costs instructions on every region exit elsewhere.
     * - Other platforms: Use Large model per LLVM MCJIT recommendations.
     */
#if defined(__aarch64__)
    LLVMCodeModel code_model = LLVMCodeModelSmall;
#else
    LLVMCodeModel code_model = LLVMCodeModelLarge;
#endif
    char *cpu_name = LLVMGetHostCPUName();
    char *cpu_features = LLVMGetHostCPUFeatures();
    /* MCJIT builds its own target machine without a CPU, so name the host on
     * the function itself; the attributes steer both the passes and codegen.
     */
    LLVMAddAttributeAtIndex(
        start, LLVMAttributeFunctionIndex,
        LLVMCreateStringAttribute(LLVMGetGlobalContext(), "target-cpu", 10,
                                  cpu_name, strlen(cpu_name)));
    LLVMAddAttributeAtIndex(
        start, LLVMAttributeFunctionIndex,
        LLVMCreateStringAttribute(LLVMGetGlobalContext(), "target-features", 15,
                                  cpu_features, strlen(cpu_features)));
    LLVMTargetMachineRef tm =
        LLVMCreateTargetMachine(target, triple, cpu_name, cpu_features,
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
    LLVMDisposeMessage(cpu_name);
    LLVMDisposeMessage(cpu_features);

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
                free(ir->branch_table);
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
            free(ir->branch_table);
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
    return;

    /* Leave the block to tier-1, before any machine code exists. */
abandon:
    LLVMDisposeBuilder(first_builder);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
    free(set);
    pthread_mutex_unlock(cache_lock);
}

struct jit_cache *jit_cache_init(void)
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

/* The engine of an evicted block. The emulator thread evicts blocks, but must
 * not dispose their engines itself: the T2C thread optimizes and emits code
 * without holding cache_lock, and LLVM state is not safe to touch from two
 * threads at once.
 */
struct t2c_retired_engine {
    struct t2c_retired_engine *next;
    void *engine;
};

/* Hand an evicted block's engine to the T2C thread. The caller holds cache_lock
 * and has already unpublished the block's code.
 */
void t2c_retire_engine(riscv_t *rv, void *engine)
{
    if (!engine)
        return;
    struct t2c_retired_engine *node = malloc(sizeof(*node));
    if (unlikely(!node))
        return; /* leaking the engine is safe; disposing it here is not */
    node->engine = engine;
    node->next = rv->retired_engines;
    rv->retired_engines = node;
}

/* Dispose the retired engines. Called by the T2C thread between compilations
 * and at shutdown, without cache_lock held.
 */
void t2c_reap_engines(riscv_t *rv)
{
    pthread_mutex_lock(&rv->cache_lock);
    struct t2c_retired_engine *node = rv->retired_engines;
    rv->retired_engines = NULL;
    pthread_mutex_unlock(&rv->cache_lock);

    while (node) {
        struct t2c_retired_engine *next = node->next;
        LLVMDisposeExecutionEngine((LLVMExecutionEngineRef) node->engine);
        free(node);
        node = next;
    }
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
    /* The key packs satp above pc. Mixing satp into the slot spreads entries
     * from different address spaces across the table.
     */
    uint32_t pos = jit_cache_slot((uint32_t) key, (uint32_t) (key >> 32));

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
