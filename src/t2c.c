/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <inttypes.h>
#include <limits.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm/Config/llvm-config.h>
#include <stdlib.h>
#include <unistd.h>

/* Bitcode collection uses the LLVM C API; native compilation is offline. */
#if LLVM_VERSION_MAJOR < 19
#error "T2C requires LLVM 19 or later. Found LLVM " LLVM_VERSION_STRING
#elif LLVM_VERSION_MAJOR > 23
#warning "LLVM version > 23 detected. T2C supports LLVM 19-23."
#endif

#include "jit.h"
#include "mpool.h"
#include "riscv_private.h"

RV_T2C_EXPORT const uint64_t rv32_aot_collector_abi = RV_AOT_ABI;
RV_T2C_EXPORT const uint32_t rv32_aot_producer_llvm = LLVM_VERSION_MAJOR;

#if RV32_HAS(SYSTEM_MMIO)
/* The inline probe indexes rv->jit_dtlb by page number modulo TLB_SIZE and
 * compares 4 KiB page tags; it takes field offsets from the declarations.
 */
_Static_assert((TLB_SIZE & (TLB_SIZE - 1)) == 0,
               "TLB_SIZE must be a power of two");
_Static_assert(RV_PG_SHIFT == 12, "fast path assumes 4 KiB pages");
#endif

/* Collection is serialized. Each region owns its context, including every
 * type, builder, and metadata node; LLVM 22+ deprecates the global context.
 */
static LLVMContextRef t2c_context;

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
            LLVMInt32TypeInContext(t2c_context),                              \
            offsetof(riscv_t, rv_member) / sizeof(int) + ir_member, true);    \
        return LLVMBuildInBoundsGEP2(*builder,                                \
                                     LLVMInt32TypeInContext(t2c_context),     \
                                     LLVMGetParam(start, 0), &offset, 1, ""); \
    }

T2C_LLVM_GEN_ADDR(rs1, X, ir->rs1);
T2C_LLVM_GEN_ADDR(rs2, X, ir->rs2);
T2C_LLVM_GEN_ADDR(rd, X, ir->rd);

/* Store a loaded value to destination, or, for a load into x0, which has no
 * destination, keep the access: direct RAM loads become volatile, and MMU
 * callbacks carry their own side effects.
 */
static void t2c_gen_load_result(LLVMBuilderRef *builder,
                                LLVMValueRef value,
                                LLVMValueRef destination,
                                bool is_signed)
{
    if (!destination) {
        if (LLVMIsALoadInst(value))
            LLVMSetVolatile(value, true);
        return;
    }
    value = LLVMBuildIntCast2(
        *builder, value, LLVMInt32TypeInContext(t2c_context), is_signed, "");
    LLVMBuildStore(*builder, value, destination);
}

static LLVMValueRef t2c_gen_load_rd_addr(LLVMValueRef start,
                                         LLVMBuilderRef *builder,
                                         rv_insn_t *ir)
{
    return ir->rd ? t2c_gen_rd_addr(start, builder, ir) : NULL;
}
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
    LLVMValueRef rv_bytes = LLVMBuildBitCast(
        *builder, LLVMGetParam(start, 0),
        LLVMPointerType(LLVMInt8TypeInContext(t2c_context), 0), "");
    LLVMValueRef offset = LLVMConstInt(LLVMInt64TypeInContext(t2c_context),
                                       (uint64_t) byte_offset, false);
    LLVMValueRef field_bytes = LLVMBuildGEP2(
        *builder, LLVMInt8TypeInContext(t2c_context), rv_bytes, &offset, 1, "");
    return LLVMBuildBitCast(*builder, field_bytes,
                            LLVMPointerType(field_type, 0), "");
}

FORCE_INLINE LLVMValueRef t2c_gen_csr_cycle_addr(LLVMValueRef start,
                                                 LLVMBuilderRef *builder,
                                                 UNUSED rv_insn_t *ir)
{
    return t2c_gen_rv_field_ptr(start, builder, offsetof(riscv_t, csr_cycle),
                                LLVMInt64TypeInContext(t2c_context));
}

#define T2C_LLVM_GEN_STORE_IMM32(builder, val, addr)                           \
    LLVMBuildStore(                                                            \
        builder, LLVMConstInt(LLVMInt32TypeInContext(t2c_context), val, true), \
        addr)

#define T2C_LLVM_GEN_LOAD_VMREG(reg, size, addr) \
    LLVMValueRef val_##reg = LLVMBuildLoad2(     \
        *builder, LLVMInt##size##TypeInContext(t2c_context), addr, "");

#define T2C_LLVM_GEN_ALU32_IMM(op, dst, imm) \
    LLVMBuild##op(                           \
        *builder, dst,                       \
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), imm, true), "")

#define T2C_LLVM_GEN_ALU64_IMM(op, dst, imm) \
    LLVMBuild##op(                           \
        *builder, dst,                       \
        LLVMConstInt(LLVMInt64TypeInContext(t2c_context), imm, true), "")

#define T2C_LLVM_GEN_CMP(cond, rs1, rs2) \
    LLVMValueRef cmp = LLVMBuildICmp(*builder, LLVMInt##cond, rs1, rs2, "")

#define T2C_LLVM_GEN_CMP_IMM32(cond, rs1, imm) \
    LLVMValueRef cmp = LLVMBuildICmp(          \
        *builder, LLVMInt##cond, rs1,          \
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), imm, false), "")

/* Store accumulated instruction count to rv->csr_cycle before block exit.
 * Called before every LLVMBuildRetVoid() to flush the counter.
 * The insn_counter is an alloca that LLVM's mem2reg promotes to a register.
 *
 * Guest execution and collection are serialized on the hart thread; publish
 * the count with ordinary loads/stores, as T1 and the interpreter do.
 *
 * Using csr_cycle instead of timer ensures:
 * - SYSTEM mode: timer interrupts work correctly (timer = csr_cycle + offset)
 * - Non-SYSTEM mode: RDCYCLE instruction returns accurate counts
 */
static void t2c_add_stat(LLVMBuilderRef builder,
                         LLVMValueRef start,
                         size_t offset,
                         LLVMValueRef increment)
{
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    LLVMValueRef address = t2c_gen_rv_field_ptr(start, &builder, offset, i64);
    LLVMValueRef old = LLVMBuildLoad2(builder, i64, address, "");
    LLVMBuildStore(builder, LLVMBuildAdd(builder, old, increment, ""), address);
}

#define T2C_STORE_TIMER(bldr, start_val, counter)                             \
    do {                                                                      \
        LLVMValueRef _cycle_ptr =                                             \
            t2c_gen_csr_cycle_addr(start_val, &(bldr), NULL);                 \
        LLVMValueRef _cnt = LLVMBuildLoad2(                                   \
            bldr, LLVMInt64TypeInContext(t2c_context), counter, "");          \
        LLVMValueRef _old = LLVMBuildLoad2(                                   \
            bldr, LLVMInt64TypeInContext(t2c_context), _cycle_ptr, "");       \
        LLVMBuildStore(bldr, LLVMBuildAdd(bldr, _old, _cnt, ""), _cycle_ptr); \
        t2c_add_stat(bldr, start_val, offsetof(riscv_t, aot_native_insns),    \
                     _cnt);                                                   \
    } while (0)

#if !RV32_HAS(SYSTEM)
/* Guest RAM is an instance field, so artifacts survive ASLR and new VMs. System
 * accesses take the host address from the RAM tags instead.
 */
static LLVMValueRef t2c_gen_ram_base(LLVMValueRef start,
                                     LLVMBuilderRef *builder)
{
    LLVMTypeRef ptr_type =
        LLVMPointerType(LLVMInt8TypeInContext(t2c_context), 0);
    LLVMValueRef ptr = t2c_gen_rv_field_ptr(
        start, builder, offsetof(riscv_t, ram_base), ptr_type);
    return LLVMBuildPtrToInt(
        *builder, LLVMBuildLoad2(*builder, ptr_type, ptr, "ram_base"),
        LLVMInt64TypeInContext(t2c_context), "");
}
#endif

/* Branch on cond to the rare block, else to the common one, and keep the rare
 * path out of the way of the common one.
 */
static void t2c_gen_unlikely_br(LLVMBuilderRef builder,
                                LLVMValueRef cond,
                                LLVMBasicBlockRef rare,
                                LLVMBasicBlockRef common)
{
    LLVMValueRef br = LLVMBuildCondBr(builder, cond, rare, common);
    LLVMContextRef ctx = t2c_context;
    LLVMMetadataRef weights[] = {
        LLVMMDStringInContext2(ctx, "branch_weights", 14),
        LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 1, false)),
        LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 1 << 20, false)),
    };
    LLVMSetMetadata(
        br, LLVMGetMDKindIDInContext(ctx, "prof", 4),
        LLVMMetadataAsValue(ctx, LLVMMDNodeInContext2(ctx, weights, 3)));
}

/* The block whose instructions are being emitted. */
static const block_t *t2c_current_block;

#if RV32_HAS(SYSTEM)
/* Whether the page graph being compiled runs in U-mode. A page is executable
 * in U-mode or in S-mode, never both, and the graph's entry checks the class.
 */
static bool t2c_user_graph;
#endif

#if RV32_HAS(SYSTEM)

/* Retry a potentially faulting instruction through the existing precise T1
 * path. Publish only the retired prefix, including instructions within fusions.
 */
static unsigned t2c_retired_prefix(uint32_t pc)
{
    const block_t *block = t2c_current_block;
    unsigned end = (pc - block->pc_start) / 2;
    assert(end < 2048);
    unsigned prefix = 0;
    for (unsigned i = 0; i < end / 64; i++)
        prefix += __builtin_popcountll(block->aot_insn_starts[i]);
    unsigned bits = end % 64;
    if (bits)
        prefix += __builtin_popcountll(block->aot_insn_starts[end / 64] &
                                       ((UINT64_C(1) << bits) - 1));
    return prefix;
}

static void t2c_gen_fault_fallback(LLVMBuilderRef builder,
                                   LLVMValueRef start,
                                   uint32_t pc,
                                   LLVMValueRef insn_counter)
{
    const block_t *block = t2c_current_block;
    unsigned prefix = t2c_retired_prefix(pc);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    LLVMValueRef count = LLVMBuildLoad2(builder, i64, insn_counter, "");
    LLVMBuildStore(
        builder,
        LLVMBuildSub(builder, count,
                     LLVMConstInt(i64, block->cycle_cost - prefix, false), ""),
        insn_counter);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(t2c_context);
    LLVMBuildStore(builder, LLVMConstInt(i8, 1, false),
                   t2c_gen_rv_field_ptr(start, &builder,
                                        offsetof(riscv_t, aot_fallback), i8));
    T2C_LLVM_GEN_STORE_IMM32(builder, pc,
                             t2c_gen_PC_addr(start, &builder, NULL));
    T2C_STORE_TIMER(builder, start, insn_counter);
    LLVMBuildRetVoid(builder);
}
#endif

/* Load an instance helper by its real riscv_t byte offset and call it with
 * the current VM and n arguments. No process address is captured in bitcode.
 */
static LLVMValueRef t2c_gen_call_func(LLVMValueRef start,
                                      LLVMBuilderRef *builder,
                                      size_t function_offset,
                                      LLVMTypeRef ret_type,
                                      LLVMValueRef *args,
                                      unsigned n)
{
    LLVMValueRef params[3] = {LLVMGetParam(start, 0)};
    LLVMTypeRef types[3] = {
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0)};
    assert(n < 3);
    for (unsigned i = 0; i < n; i++) {
        params[i + 1] = args[i];
        types[i + 1] = LLVMTypeOf(args[i]);
    }
    LLVMTypeRef fn_type = LLVMFunctionType(ret_type, types, n + 1, false);
    LLVMTypeRef fn_ptr_type = LLVMPointerType(fn_type, 0);
    LLVMValueRef fn = LLVMBuildLoad2(
        *builder, fn_ptr_type,
        t2c_gen_rv_field_ptr(start, builder, function_offset, fn_ptr_type), "");
    return LLVMBuildCall2(*builder, fn_type, fn, params, n + 1, "");
}

/* The address rs1 + imm that a load or store accesses. */
static LLVMValueRef t2c_gen_vaddr(LLVMValueRef start,
                                  LLVMBuilderRef *builder,
                                  rv_insn_t *ir)
{
    LLVMValueRef val_rs1 =
        LLVMBuildLoad2(*builder, LLVMInt32TypeInContext(t2c_context),
                       t2c_gen_rs1_addr(start, builder, ir), "");
    return LLVMBuildAdd(
        *builder, val_rs1,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), ir->imm, true),
        "vaddr");
}

#if !RV32_HAS(SYSTEM)
/* Guarding a changing pointer costs more than a wrapping add. Apply the
 * widened form only to regions rooted at stack-heavy blocks,
 * where folded operands can replace enough live address temporaries to pay
 * for the guards. This is a profitability choice, not a guest ABI assumption.
 */
#define T2C_STACK_DISPLACEMENTS 8
static bool t2c_fold_stack_displacements;

static void t2c_add_stack_displacement(int rs1,
                                       int32_t imm,
                                       int32_t *offsets,
                                       unsigned *count)
{
    if (rs1 != rv_reg_sp || !imm || *count == T2C_STACK_DISPLACEMENTS)
        return;
    for (unsigned i = 0; i < *count; i++) {
        if (offsets[i] == imm)
            return;
    }
    offsets[(*count)++] = imm;
}

static bool t2c_dense_stack_accesses(const block_t *block)
{
    int32_t offsets[T2C_STACK_DISPLACEMENTS];
    unsigned count = 0;
    for (const rv_insn_t *ir = block->ir_head; ir; ir = block_next_ir(ir)) {
        switch (ir->opcode) {
        case rv_insn_fuse3:
        case rv_insn_fuse4:
            for (int i = 0; i < ir->imm2; i++) {
                t2c_add_stack_displacement(ir->fuse[i].rs1, ir->fuse[i].imm,
                                           offsets, &count);
            }
            break;
        case rv_insn_lb:
        case rv_insn_lh:
        case rv_insn_lw:
        case rv_insn_lbu:
        case rv_insn_lhu:
        case rv_insn_sb:
        case rv_insn_sh:
        case rv_insn_sw:
#if RV32_HAS(EXT_C)
        case rv_insn_clw:
        case rv_insn_csw:
        case rv_insn_clwsp:
        case rv_insn_cswsp:
#endif
        case rv_insn_fuse11:
        case rv_insn_fuse13:
            t2c_add_stack_displacement(ir->rs1, ir->imm, offsets, &count);
            break;
        default:
            break;
        }
    }
    return count == T2C_STACK_DISPLACEMENTS;
}

/* Leave through rv->aot_misaligned_trap with pc and the guest registers
 * published. The block was charged in full on entry: refund the instructions
 * after pc, and the one at pc as well unless the callback retires it.
 */
static void t2c_gen_user_trap(LLVMValueRef start,
                              LLVMBuilderRef *builder,
                              LLVMValueRef insn_counter,
                              uint32_t pc,
                              bool retires,
                              LLVMValueRef vaddr,
                              uint32_t flags)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    LLVMBuildStore(*builder, LLVMConstInt(i32, pc, false),
                   t2c_gen_PC_addr(start, builder, NULL));
    unsigned refund =
        block_unretired_after(t2c_current_block, pc) + (retires ? 0 : 1);
    LLVMValueRef count = LLVMBuildLoad2(*builder, i64, insn_counter, "");
    LLVMBuildStore(
        *builder,
        LLVMBuildSub(*builder, count, LLVMConstInt(i64, refund, false), ""),
        insn_counter);
    T2C_STORE_TIMER(*builder, start, insn_counter);
    LLVMValueRef args[] = {vaddr, LLVMConstInt(i32, flags, false)};
    t2c_gen_call_func(start, builder, offsetof(riscv_t, aot_misaligned_trap),
                      LLVMVoidTypeInContext(t2c_context), args, 2);
    LLVMBuildRetVoid(*builder);
}

/* Widen before adding the displacement so host loads/stores can fold it into
 * their addressing mode. A 32-bit add followed by a zero extension forces a
 * separate address temporary for every displacement; loop-invariant ones can
 * spill throughout a hot trace. Wrapping accesses leave through a cold
 * interpreter callback.
 */
FORCE_INLINE LLVMValueRef t2c_gen_mem_loc(LLVMValueRef start,
                                          LLVMBuilderRef *builder,
                                          rv_insn_t *ir,
                                          LLVMValueRef insn_counter,
                                          uint32_t pc)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    if (!t2c_fold_stack_displacements) {
        LLVMValueRef addr = LLVMBuildAdd(
            *builder,
            LLVMBuildZExt(*builder, t2c_gen_vaddr(start, builder, ir), i64, ""),
            t2c_gen_ram_base(start, builder), "");
        return LLVMBuildIntToPtr(*builder, addr, LLVMPointerType(i32, 0), "");
    }
    LLVMValueRef base =
        LLVMBuildLoad2(*builder, i32, t2c_gen_rs1_addr(start, builder, ir), "");
    if (ir->imm) {
        uint32_t limit = ir->imm < 0 ? 0u - (uint32_t) ir->imm
                                     : UINT32_MAX - (uint32_t) ir->imm;
        LLVMValueRef wraps =
            LLVMBuildICmp(*builder, ir->imm < 0 ? LLVMIntULT : LLVMIntUGT, base,
                          LLVMConstInt(i32, limit, false), "address_wraps");
        LLVMBasicBlockRef slow =
            LLVMAppendBasicBlockInContext(t2c_context, start, "wrapped");
        LLVMBasicBlockRef fast =
            LLVMAppendBasicBlockInContext(t2c_context, start, "unwrapped");
        t2c_gen_unlikely_br(*builder, wraps, slow, fast);
        LLVMPositionBuilderAtEnd(*builder, slow);
        /* The interpreter resumes at the access, which also covers an arena
         * whose mirror pages could not be mapped.
         */
        t2c_gen_user_trap(start, builder, insn_counter, pc, false,
                          LLVMConstNull(i32), AOT_USER_INTERPRET);
        LLVMPositionBuilderAtEnd(*builder, fast);
    }
    LLVMValueRef offset =
        LLVMBuildAdd(*builder, LLVMBuildZExt(*builder, base, i64, ""),
                     LLVMConstInt(i64, (uint64_t) (int64_t) ir->imm, true), "");
    LLVMValueRef addr =
        LLVMBuildAdd(*builder, t2c_gen_ram_base(start, builder), offset, "");
    return LLVMBuildIntToPtr(*builder, addr, LLVMPointerType(i32, 0), "");
}
#endif

/* Tier-2 counterpart of emit_misalign_guard() in jit.c: when a halfword or word
 * access at vaddr is misaligned, and misaligned accesses are not allowed, store
 * the faulting pc, raise the exception through jit_misaligned_trap(), and
 * return from the block. Generated code keeps guest registers in rv->X, so
 * nothing needs writing back first. A constant vaddr folds the branch. The
 * helper is loaded from the current VM instance.
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
    if (rv->jit_elide_align_checks)
        return;

    LLVMValueRef low = LLVMBuildAnd(
        *builder, vaddr,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), size - 1, false), "");
    LLVMValueRef misaligned = LLVMBuildICmp(
        *builder, LLVMIntNE, low,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), 0, false),
        "misaligned");
    LLVMBasicBlockRef trap =
        LLVMAppendBasicBlockInContext(t2c_context, start, "misaligned");
    LLVMBasicBlockRef aligned =
        LLVMAppendBasicBlockInContext(t2c_context, start, "aligned");
    t2c_gen_unlikely_br(*builder, misaligned, trap, aligned);

    LLVMPositionBuilderAtEnd(*builder, trap);
#if RV32_HAS(SYSTEM)
    (void) flags;
    t2c_gen_fault_fallback(*builder, start, pc, insn_counter);
#else
    /* The handler emulates the access, so it retires. */
    t2c_gen_user_trap(start, builder, insn_counter, pc, true, vaddr, flags);
#endif
    LLVMPositionBuilderAtEnd(*builder, aligned);

    /* State the proven alignment, so that later checks of the same address bits
     * fold away.
     */
    unsigned assume_id = LLVMLookupIntrinsicID("llvm.assume", 11);
    LLVMValueRef assume = LLVMGetIntrinsicDeclaration(
        LLVMGetGlobalParent(start), assume_id, NULL, 0);
    LLVMValueRef aligned_cond = LLVMBuildNot(*builder, misaligned, "");
    LLVMBuildCall2(*builder,
                   LLVMIntrinsicGetType(t2c_context, assume_id, NULL, 0),
                   assume, &aligned_cond, 1, "");
}

static LLVMValueRef t2c_gen_call_io_func(LLVMValueRef start,
                                         LLVMBuilderRef *builder,
                                         size_t io_field,
                                         LLVMTypeRef ret_type,
                                         LLVMValueRef *args,
                                         unsigned n)
{
    return t2c_gen_call_func(start, builder, offsetof(riscv_t, io) + io_field,
                             ret_type, args, n);
}

static LLVMTypeRef t2c_jit_cache_func_type;

/* User regions tail-call each other at every exit. On x86-64 they use
 * preserve_none, which saves no callee-saved registers, so an exit is a jump
 * rather than six pops before it and six pushes after it:
 * CoreMark runs 3% faster. The emulator enters them through rv32_aot_enter(),
 * which tools/aot.py adds when the manifest names this convention. System
 * regions call C helpers on every slow path, around which preserve_none code
 * must save its live values; that made them 12% larger and no faster.
 */
#if defined(__x86_64__) && !RV32_HAS(SYSTEM)
#define T2C_CALL_CONV 21 /* CallingConv::PreserveNone */
#else
#define T2C_CALL_CONV LLVMCCallConv
#endif
/* The emulator records it in the collection manifest for tools/aot.py. */
RV_T2C_EXPORT const uint32_t rv32_aot_call_conv = T2C_CALL_CONV;
static LLVMTypeRef t2c_jit_cache_struct_type;
static LLVMTypeRef t2c_inline_cache_struct_type;

/* Leave the region at pc: store it, flush the instruction count, return. */
static void t2c_gen_exit_raw(LLVMBuilderRef builder,
                             LLVMValueRef start,
                             uint32_t pc,
                             LLVMValueRef insn_counter)
{
    T2C_LLVM_GEN_STORE_IMM32(builder, pc,
                             t2c_gen_PC_addr(start, &builder, NULL));
    T2C_STORE_TIMER(builder, start, insn_counter);
    LLVMBuildRetVoid(builder);
}

FORCE_INLINE void t2c_jit_cache_helper(LLVMBuilderRef *builder,
                                       LLVMValueRef start,
                                       LLVMValueRef addr,
                                       riscv_t *rv,
                                       block_t *block,
                                       rv_insn_t *ir,
                                       LLVMValueRef insn_counter);

#if !RV32_HAS(SYSTEM)
/* The libc exit address of the program being compiled; see t2c_gen_exit(). */
static uint32_t t2c_exit_addr;
#endif

/* Leave the region for pc: tail-call the native entry bound for it, or return
 * to the dispatcher when there is none. Entering libc exit changes host syscall
 * policy in rv_step(), so that edge always returns.
 */
static void t2c_gen_exit(LLVMBuilderRef builder,
                         LLVMValueRef start,
                         uint32_t pc,
                         LLVMValueRef insn_counter)
{
#if !RV32_HAS(SYSTEM)
    if (t2c_exit_addr && pc == t2c_exit_addr) {
        t2c_gen_exit_raw(builder, start, pc, insn_counter);
        return;
    }
#endif
    t2c_jit_cache_helper(
        &builder, start,
        LLVMConstInt(LLVMInt32TypeInContext(t2c_context), pc, false), NULL,
        NULL, NULL, insn_counter);
}

#if RV32_HAS(SYSTEM)
/* Page-graph state for the page t2c_compile() is lowering: its blocks by
 * halfword, the local indirect-jump switch, and its coverage sidecar.
 */
static block_t *t2c_page_blocks[RV_PG_SIZE / 2];
static LLVMBasicBlockRef t2c_local_indirect;
static LLVMValueRef t2c_local_indirect_pc, t2c_local_indirect_switch;
static bool t2c_local_targets[RV_PG_SIZE / 2];
/* Blocks whose tracing has finished, by start halfword offset in the page. An
 * edge to a block in the graph but not yet finished closes a cycle.
 */
static bool t2c_traced[RV_PG_SIZE / 2];
static char t2c_coverage_path[PATH_MAX];
static uint8_t t2c_coverage[RV_PG_SIZE / 16];

/* Gather the decoded blocks of root's page, with the same address space,
 * privilege and contents, into one page graph.
 */
static bool t2c_prepare_page(riscv_t *rv, block_t *root, const char *path)
{
    memset(t2c_page_blocks, 0, sizeof(t2c_page_blocks));
    memset(t2c_traced, 0, sizeof(t2c_traced));
    memset(t2c_local_targets, 0, sizeof(t2c_local_targets));
    t2c_local_indirect = NULL;
    t2c_local_indirect_pc = t2c_local_indirect_switch = NULL;
    block_t *peer;
    list_for_each_entry (peer, &rv->block_list, list) {
        if (!peer->aot_translatable || peer->satp != root->satp ||
            peer->priv_mode != root->priv_mode ||
            peer->aot_page_hash != root->aot_page_hash ||
            memcmp(peer->aot_page_digest, root->aot_page_digest, 32) ||
            ((peer->pc_start ^ root->pc_start) & ~(RV_PG_SIZE - 1)))
            continue;
        t2c_page_blocks[(peer->pc_start & (RV_PG_SIZE - 1)) / 2] = peer;
        peer->aot_saved = true;
    }
    t2c_page_blocks[(root->pc_start & (RV_PG_SIZE - 1)) / 2] = root;
    /* Keep the local switch focused on actual indirect targets and static
     * return addresses. An all-block switch creates artificial irreducible
     * edges, huge SSA joins, and unnecessary register pressure. New targets
     * remain correct through the shared runtime-cache fallback.
     */
    for (unsigned i = 0; i < RV_PG_SIZE / 2; i++) {
        block_t *block = t2c_page_blocks[i];
        if (!block)
            continue;
        for (rv_insn_t *ir = block->ir_head; ir; ir = block_next_ir(ir)) {
            uint32_t next = 0;
            if ((ir->opcode == rv_insn_jal || ir->opcode == rv_insn_jalr) &&
                ir->rd)
                next = ir->pc + 4;
#if RV32_HAS(EXT_C)
            if (ir->opcode == rv_insn_cjal || ir->opcode == rv_insn_cjalr)
                next = ir->pc + 2;
#endif
            if (next && !((next ^ root->pc_start) & ~(RV_PG_SIZE - 1)))
                t2c_local_targets[(next & (RV_PG_SIZE - 1)) / 2] = true;
            if (!ir->branch_table)
                continue;
            for (unsigned target = 0; target < HISTORY_SIZE; target++) {
                const branch_history_table_t *bt = ir->branch_table;
                uint32_t pc = bt->PC[target];
                if (bt->times[target] && bt->satp[target] == root->satp &&
                    !((pc ^ root->pc_start) & ~(RV_PG_SIZE - 1)))
                    t2c_local_targets[(pc & (RV_PG_SIZE - 1)) / 2] = true;
            }
        }
    }
    char digest[65];
    for (unsigned i = 0; i < 32; i++)
        snprintf(digest + i * 2, 3, "%02x", root->aot_page_digest[i]);
    int length = snprintf(t2c_coverage_path, sizeof(t2c_coverage_path),
                          "%s.cov_%s", path, digest);
    if (length <= 0 || (size_t) length >= sizeof(t2c_coverage_path)) {
        t2c_coverage_path[0] = '\0';
        return false;
    }
    memset(t2c_coverage, 0, sizeof(t2c_coverage));
    FILE *coverage = fopen(t2c_coverage_path, "rb");
    if (coverage) {
        if (fread(t2c_coverage, 1, sizeof(t2c_coverage), coverage) !=
            sizeof(t2c_coverage))
            memset(t2c_coverage, 0, sizeof(t2c_coverage));
        fclose(coverage);
    }
    bool added = false;
    for (unsigned i = 0; i < RV_PG_SIZE / 2; i++) {
        if (!t2c_page_blocks[i])
            continue;
        uint8_t bit = 1u << (i % 8);
        added |= !(t2c_coverage[i / 8] & bit);
        t2c_coverage[i / 8] |= bit;
    }
    /* Only skip a subset. Equal-sized but different entry sets contribute a
     * new graph; the offline index merges their union after symbol renaming.
     */
    return !added;
}
#endif

#if RV32_HAS(SYSTEM)
/* Open "<path><suffix>" for writing a collection sidecar. */
static FILE *t2c_open_sidecar(const char *path, const char *suffix)
{
    char name[PATH_MAX];
    int length = snprintf(name, sizeof(name), "%s%s", path, suffix);
    if (length <= 0 || (size_t) length >= sizeof(name))
        return NULL;
    return fopen(name, "wb");
}

/* Close a sidecar. Fails if it never opened, a write failed, or close did. */
static bool t2c_close_sidecar(FILE *file, bool written)
{
    if (!file)
        return false;
    return !fclose(file) && written;
}

/* Write size bytes of data as "<path><suffix>". */
static bool t2c_write_sidecar(const char *path,
                              const char *suffix,
                              const void *data,
                              size_t size)
{
    FILE *file = t2c_open_sidecar(path, suffix);
    return t2c_close_sidecar(file, file && fwrite(data, 1, size, file) == size);
}
#endif

/* Region state, kept in file scope because a single thread runs t2c_compile.
 * t2c_region_insns counts the IR instructions traced into the current region;
 * past T2C_REGION_BUDGET, successors are linked through the jit-cache rather
 * than traced, which bounds the code a region duplicates from others.
 * t2c_predicted_pc passes an indirect jump's predicted target from its handler
 * to t2c_trace_ebb().
 */
#define T2C_REGION_BUDGET 8192
static uint32_t t2c_region_insns;
static uint32_t t2c_predicted_pc;
#if !RV32_HAS(SYSTEM)
/* The blocks traced into the current user region so far. */
static set_t *t2c_region_set;
#endif

#if !RV32_HAS(SYSTEM)
/* Returns of a user region. Each traced call records where it returns. A
 * return with no dominant target branches to t2c_return_dispatch, a switch on
 * the return address whose cases are the recorded return points the region
 * traced, so a callee shared by several call sites returns into the region
 * instead of leaving it on every call. Other addresses use the jit-cache.
 */
#define T2C_MAX_RETURNS 256
static uint32_t t2c_return_pcs[T2C_MAX_RETURNS];
static unsigned t2c_n_returns;
static LLVMBasicBlockRef t2c_return_dispatch;
static LLVMValueRef t2c_return_pc, t2c_return_switch;
#endif

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
    case rv_insn_wfi:
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
#if RV32_HAS(SYSTEM)
/* Poll at function entries and back edges, edges to blocks whose tracing is
 * still in progress. Tracing is depth first, so every cycle in the graph has
 * such an edge; joins to finished blocks need no poll. Indirect jumps re-enter
 * through polled entry gates, and a region is page-bounded, so an acyclic path
 * is bounded too.
 */
static void t2c_gen_budget_check(LLVMBuilderRef *builder,
                                 LLVMValueRef start,
                                 uint32_t pc,
                                 LLVMValueRef insn_counter)
{
    LLVMTypeRef i64 = LLVMInt64TypeInContext(t2c_context);
    LLVMValueRef retired = LLVMBuildLoad2(*builder, i64, insn_counter, "");
    LLVMValueRef cycle = LLVMBuildLoad2(
        *builder, i64, t2c_gen_csr_cycle_addr(start, builder, NULL), "");
    LLVMValueRef limit = LLVMBuildLoad2(
        *builder, i64,
        t2c_gen_rv_field_ptr(start, builder,
                             offsetof(riscv_t, branch_chain_cycle_target), i64),
        "");
    LLVMValueRef expired = LLVMBuildICmp(
        *builder, LLVMIntUGE, LLVMBuildAdd(*builder, cycle, retired, ""), limit,
        "budget_expired");
    LLVMBasicBlockRef yield =
        LLVMAppendBasicBlockInContext(t2c_context, start, "yield");
    LLVMBasicBlockRef body =
        LLVMAppendBasicBlockInContext(t2c_context, start, "body");
    t2c_gen_unlikely_br(*builder, expired, yield, body);
    LLVMPositionBuilderAtEnd(*builder, yield);
    t2c_gen_exit_raw(*builder, start, pc, insn_counter);
    LLVMPositionBuilderAtEnd(*builder, body);
}
#endif

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
#if RV32_HAS(SYSTEM)
        if (!t2c_traced[(pc & (RV_PG_SIZE - 1)) / 2])
            t2c_gen_budget_check(&from, start, pc, insn_counter);
#endif
        LLVMBuildBr(from, t2c_block_map_search(map, pc));
        return;
    }
    block_t *blk = t2c_check_valid_blk(rv, block, pc);
    if (!blk) {
        t2c_gen_exit(from, start, pc, insn_counter);
        return;
    }
    if (t2c_region_insns >= T2C_REGION_BUDGET || map->count >= MAX_BLOCKS) {
        t2c_jit_cache_helper(
            &from, start,
            LLVMConstInt(LLVMInt32TypeInContext(t2c_context), pc, false), rv,
            block, NULL, insn_counter);
        return;
    }
    LLVMBasicBlockRef next_entry =
        LLVMAppendBasicBlockInContext(t2c_context, start, "next_entry");
    LLVMBuilderRef next = LLVMCreateBuilderInContext(t2c_context);
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
    t2c_current_block = block;

    if (set_has(set, ir->pc))
        return;
    set_add(set, ir->pc);
    t2c_block_map_insert(map, entry, ir->pc);
    LLVMBuilderRef tk = NULL, utk = NULL;

    LLVMValueRef cnt = LLVMBuildLoad2(
        *builder, LLVMInt64TypeInContext(t2c_context), insn_counter, "");
    LLVMBuildStore(*builder,
                   T2C_LLVM_GEN_ALU64_IMM(Add, cnt, block->cycle_cost),
                   insn_counter);

    /* Kept in the handler signature for system instruction templates. */
    uint64_t mem_base = 0;

    while (1) {
        t2c_region_insns++;
        ((t2c_codegen_block_func_t) dispatch_table[ir->opcode])(
            builder, param_types, start, entry, &tk, &utk, rv, mem_base, block,
            ir, insn_counter);
        if (ir == block->ir_tail)
            break;
        ir = block_next_ir(ir);
    }

    if (t2c_insn_is_terminal(ir->opcode)) {
        /* An indirect jump with a dominant target left that path open. */
        uint32_t predicted = t2c_predicted_pc;
        t2c_predicted_pc = 0;
        if (predicted)
            t2c_follow(tk, predicted, param_types, start, rv, block, set, map,
                       insn_counter);
    } else {
        /* Direct edges are known from instruction PCs, independently of the
         * interpreter's edge links, which T1C execution never sets and IR
         * eviction and SFENCE remove.
         */
        if (utk)
            t2c_follow(utk, block->pc_end, param_types, start, rv, block, set,
                       map, insn_counter);
        if (tk) {
            uint32_t pc = ir->opcode == rv_insn_fuse12 ? ir->pc + 4 + ir->imm2
                                                       : ir->pc + ir->imm;
            t2c_follow(tk, pc, param_types, start, rv, block, set, map,
                       insn_counter);
        }
        if (!tk && !utk &&
            !LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(*builder)))
            t2c_follow(*builder, block->pc_end, param_types, start, rv, block,
                       set, map, insn_counter);
    }

    if (tk && tk != *builder)
        LLVMDisposeBuilder(tk);
    if (utk && utk != *builder)
        LLVMDisposeBuilder(utk);
#if RV32_HAS(SYSTEM)
    t2c_traced[(block->pc_start & (RV_PG_SIZE - 1)) / 2] = true;
#endif
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
        LLVMGetGEPSourceElementType(ptr) != LLVMInt32TypeInContext(t2c_context))
        return -1;
    LLVMValueRef idx = LLVMGetOperand(ptr, 1);
    if (!LLVMIsAConstantInt(idx))
        return -1;
    long long i = LLVMConstIntGetSExtValue(idx);
    return i >= (long long) X_BASE && i < (long long) X_BASE + N_RV_REGS
               ? (int) (i - X_BASE)
               : -1;
}

/* x0 discards results, but a load into x0 still performs its memory access.
 * Keep that access observable before deleting its register write; otherwise
 * LLVM could erase a faulting load together with its unused value.
 */
static void t2c_preserve_zero(LLVMValueRef fn)
{
    LLVMValueRef rv_arg = LLVMGetParam(fn, 0);
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb), next; in;
             in = next) {
            next = LLVMGetNextInstruction(in);
            LLVMValueRef ptr = LLVMIsALoadInst(in)    ? LLVMGetOperand(in, 0)
                               : LLVMIsAStoreInst(in) ? LLVMGetOperand(in, 1)
                                                      : NULL;
            if (!ptr || t2c_guest_reg(ptr, rv_arg) != 0)
                continue;
            if (LLVMIsALoadInst(in)) {
                LLVMReplaceAllUsesWith(in, LLVMConstNull(LLVMTypeOf(in)));
            } else {
                LLVMValueRef value = LLVMGetOperand(in, 0);
                while (LLVMIsACastInst(value))
                    value = LLVMGetOperand(value, 0);
                if (LLVMIsALoadInst(value))
                    LLVMSetVolatile(value, true);
            }
            LLVMInstructionEraseFromParent(in);
        }
    }
}

#if !RV32_HAS(SYSTEM)
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
    if (type == LLVMInt8TypeInContext(t2c_context))
        scale = 1;
    else if (type == LLVMInt32TypeInContext(t2c_context))
        scale = 4;
    else if (type == LLVMInt64TypeInContext(t2c_context))
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
            LLVMBuildStore(
                b,
                LLVMBuildLoad2(b, LLVMInt32TypeInContext(t2c_context), from[r],
                               ""),
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

/* Whether in calls a helper or native function, which reads guest registers
 * from rv->X, rather than an intrinsic or inline assembly.
 */
static bool t2c_is_call(LLVMValueRef in)
{
    return LLVMIsACallInst(in) && !LLVMIsAInlineAsm(LLVMGetCalledValue(in)) &&
           !LLVMIsAIntrinsicInst(in);
}

/* Update the set of promoted registers whose variable may differ from rv->X
 * after instruction in. A store that copies a register's home value back is
 * clean; any other store dirties the register; a call writes back first.
 */
static uint32_t t2c_dirty_after(LLVMValueRef in,
                                uint32_t dirty,
                                const LLVMValueRef *var,
                                const LLVMValueRef *home)
{
    if (t2c_is_call(in))
        return 0;
    if (!LLVMIsAStoreInst(in))
        return dirty;
    LLVMValueRef ptr = LLVMGetOperand(in, 1);
    if (!LLVMIsAAllocaInst(ptr))
        return dirty;
    for (int r = 1; r < N_RV_REGS; r++) {
        if (ptr != var[r])
            continue;
        LLVMValueRef value = LLVMGetOperand(in, 0);
        bool copy =
            LLVMIsALoadInst(value) && LLVMGetOperand(value, 0) == home[r];
        return copy ? dirty & ~(UINT32_C(1) << r) : dirty | (UINT32_C(1) << r);
    }
    return dirty;
}

struct t2c_block_order {
    uintptr_t block;
    unsigned index;
};

static int t2c_block_order_cmp(const void *a, const void *b)
{
    uintptr_t x = ((const struct t2c_block_order *) a)->block;
    uintptr_t y = ((const struct t2c_block_order *) b)->block;
    return x < y ? -1 : x > y;
}

/* Registers that may be dirty on entry to each block, in layout order: a
 * forward union over predecessors, starting clean at the function entry.
 * LLVM-C cannot number blocks, so successors are found in a sorted copy.
 * Returns NULL on allocation failure.
 */
static uint32_t *t2c_dirty_on_entry(LLVMValueRef fn,
                                    const LLVMValueRef *var,
                                    const LLVMValueRef *home)
{
    unsigned n = LLVMCountBasicBlocks(fn);
    LLVMBasicBlockRef *blocks = malloc(n * sizeof(*blocks));
    struct t2c_block_order *sorted = malloc(n * sizeof(*sorted));
    unsigned *work = malloc(n * sizeof(*work));
    bool *queued = malloc(n * sizeof(*queued));
    uint32_t *in = calloc(n, sizeof(*in));
    if (!blocks || !sorted || !work || !queued || !in) {
        free(in);
        in = NULL;
        goto out;
    }
    LLVMGetBasicBlocks(fn, blocks);
    for (unsigned i = 0; i < n; i++)
        sorted[i] = (struct t2c_block_order) {(uintptr_t) blocks[i], i};
    qsort(sorted, n, sizeof(*sorted), t2c_block_order_cmp);
    /* A stack seeded in reverse visits the entry block first. */
    unsigned top = 0;
    for (unsigned i = n; i--;) {
        work[top++] = i;
        queued[i] = true;
    }
    while (top) {
        unsigned b = work[--top];
        queued[b] = false;
        uint32_t d = in[b];
        for (LLVMValueRef ins = LLVMGetFirstInstruction(blocks[b]); ins;
             ins = LLVMGetNextInstruction(ins))
            d = t2c_dirty_after(ins, d, var, home);
        LLVMValueRef term = LLVMGetBasicBlockTerminator(blocks[b]);
        unsigned succ = term ? LLVMGetNumSuccessors(term) : 0;
        for (unsigned k = 0; k < succ; k++) {
            struct t2c_block_order key = {(uintptr_t) LLVMGetSuccessor(term, k),
                                          0};
            const struct t2c_block_order *found =
                bsearch(&key, sorted, n, sizeof(*sorted), t2c_block_order_cmp);
            unsigned t = found->index;
            if ((in[t] | d) != in[t]) {
                in[t] |= d;
                if (!queued[t]) {
                    queued[t] = true;
                    work[top++] = t;
                }
            }
        }
    }
out:
    free(blocks);
    free(sorted);
    free(work);
    free(queued);
    return in;
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
    LLVMBuilderRef b = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderBefore(b, LLVMGetBasicBlockTerminator(entry));
    LLVMValueRef var[N_RV_REGS] = {0}, home[N_RV_REGS] = {0};
    for (int r = 1; r < N_RV_REGS; r++) {
        if (!(used & (UINT32_C(1) << r)))
            continue;
        LLVMValueRef idx = LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                        X_BASE + r, false);
        home[r] = LLVMBuildInBoundsGEP2(b, LLVMInt32TypeInContext(t2c_context),
                                        rv_arg, &idx, 1, "x");
        var[r] = LLVMBuildAlloca(b, LLVMInt32TypeInContext(t2c_context), "reg");
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
     * and then touch guest registers again. Only registers that may have been
     * written since the last fill reach rv->X, so an exit that changed few
     * registers stores few.
     */
    uint32_t *dirty_in = t2c_dirty_on_entry(fn, var, home);
    unsigned i = 0;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb), i++) {
        bool written_back = false;
        /* Without the analysis, every written register may be dirty. */
        uint32_t dirty = dirty_in ? dirty_in[i] : written;
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in)) {
            if (LLVMIsAReturnInst(in)) {
                if (!written_back) {
                    LLVMPositionBuilderBefore(b, in);
                    t2c_copy_regs(b, dirty & written, var, home);
                }
                continue;
            }
            if (!t2c_is_call(in)) {
                dirty = t2c_dirty_after(in, dirty, var, home);
                continue;
            }
            LLVMPositionBuilderBefore(b, in);
            t2c_copy_regs(b, dirty & written, var, home);
            dirty = 0;
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
    free(dirty_in);
    LLVMDisposeBuilder(b);
    return;

keep_in_memory:
    rv_log_debug("T2C: guest registers left in memory");
}
#endif

/* Where an address points: 1 for guest RAM, formed by casting a range-checked
 * integer address to a pointer, possibly offset; 0 for a field of rv; -1 for
 * anything else, such as local variables or pointers loaded from memory.
 */
static int t2c_address_space(LLVMValueRef ptr, LLVMValueRef rv_arg)
{
    for (;;) {
        if (ptr == rv_arg)
            return 0;
        if (LLVMIsAIntToPtrInst(ptr))
            return 1;
        if (!LLVMIsAGetElementPtrInst(ptr))
            return -1;
        ptr = LLVMGetOperand(ptr, 0);
    }
}

/* All direct guest accesses passed RAM range checks. Their storage cannot
 * overlap VM state, local register slots, or dispatch tables. Tell LLVM this
 * even when RAM addresses were formed through integer arithmetic. Accesses of
 * unknown origin keep no type tag, so LLVM treats them conservatively.
 */
static void t2c_disambiguate_memory(LLVMValueRef fn)
{
    LLVMValueRef rv_arg = LLVMGetParam(fn, 0);
    LLVMMetadataRef name =
        LLVMMDStringInContext2(t2c_context, "rv32emu memory", 14);
    LLVMMetadataRef root = LLVMMDNodeInContext2(t2c_context, &name, 1);
    LLVMMetadataRef offset = LLVMValueAsMetadata(
        LLVMConstInt(LLVMInt64TypeInContext(t2c_context), 0, false));
    LLVMMetadataRef types[2];
    const char *names[] = {"vm state", "guest RAM"};
    for (unsigned i = 0; i < 2; i++) {
        LLVMMetadataRef fields[] = {
            LLVMMDStringInContext2(t2c_context, names[i], strlen(names[i])),
            root, offset};
        LLVMMetadataRef type = LLVMMDNodeInContext2(t2c_context, fields, 3);
        LLVMMetadataRef tag[] = {type, type, offset};
        types[i] = LLVMMDNodeInContext2(t2c_context, tag, 3);
    }
    unsigned kind = LLVMGetMDKindIDInContext(t2c_context, "tbaa", 4);
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in)) {
            LLVMValueRef ptr = LLVMIsALoadInst(in)    ? LLVMGetOperand(in, 0)
                               : LLVMIsAStoreInst(in) ? LLVMGetOperand(in, 1)
                                                      : NULL;
            int space = ptr ? t2c_address_space(ptr, rv_arg) : -1;
            if (space >= 0)
                LLVMSetMetadata(in, kind,
                                LLVMMetadataAsValue(t2c_context, types[space]));
        }
    }
}

#if !RV32_HAS(SYSTEM)
static void t2c_allow_unaligned_ram(LLVMValueRef fn)
{
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in)) {
            LLVMValueRef ptr = LLVMIsALoadInst(in)    ? LLVMGetOperand(in, 0)
                               : LLVMIsAStoreInst(in) ? LLVMGetOperand(in, 1)
                                                      : NULL;
            if (ptr && t2c_address_space(ptr, LLVMGetParam(fn, 0)) == 1)
                LLVMSetAlignment(in, 1);
        }
    }
}
#endif

bool t2c_compile(riscv_t *rv, block_t *block, const char *path)
{
#if RV32_HAS(SYSTEM)
    bool covered = t2c_prepare_page(rv, block, path);
    if (!t2c_coverage_path[0])
        return false;
    if (covered)
        return true;
    char variant[PATH_MAX];
    if (!access(path, F_OK)) {
        int length =
            snprintf(variant, sizeof(variant), "%.*s_v%016" PRIx64 ".bc",
                     (int) strlen(path) - 3, path, rv->aot_collected);
        if (length <= 0 || (size_t) length >= sizeof(variant))
            return false;
        path = variant;
    }
#endif
    t2c_context = LLVMContextCreate();
    char name[64];
#if RV32_HAS(SYSTEM)
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(name, sizeof(name), "rv32_aot_%.*s", (int) strlen(base) - 3, base);
#else
    snprintf(name, sizeof(name), "rv32_aot_%08x", block->pc_start);
#endif
    LLVMModuleRef module =
        LLVMModuleCreateWithNameInContext("rv32emu_aot", t2c_context);
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, triple);
    LLVMDisposeMessage(triple);
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
     * This prefix is nominal: riscv_internal has more fields between them
     * (allow_misalign and ram_base follow PC, for example). Every access,
     * X[] and PC included, uses offsetof() from the real rv base pointer,
     * not typed inbounds GEPs on this truncated struct.
     */
    LLVMTypeRef io_members[] = {
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0)};
    LLVMTypeRef struct_io =
        LLVMStructTypeInContext(t2c_context, io_members, 12, false);
    LLVMTypeRef arr_X = LLVMArrayType(LLVMInt32TypeInContext(t2c_context), 32);
    /* Match actual riscv_internal layout order */
    LLVMTypeRef rv_members[] = {
        LLVMInt8TypeInContext(t2c_context),                     /* halt */
        arr_X,                                                  /* X[32] */
        LLVMInt32TypeInContext(t2c_context),                    /* PC */
        LLVMInt64TypeInContext(t2c_context),                    /* timer */
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0), /* data */
        struct_io                                               /* io */
    };
    LLVMTypeRef struct_rv =
        LLVMStructTypeInContext(t2c_context, rv_members, 6, false);
    LLVMTypeRef param_types[] = {LLVMPointerType(struct_rv, 0)};
    LLVMValueRef start =
        LLVMAddFunction(module, name,
                        LLVMFunctionType(LLVMVoidTypeInContext(t2c_context),
                                         param_types, 1, 0));

    LLVMSetVisibility(start, LLVMHiddenVisibility);
    LLVMSetFunctionCallConv(start, T2C_CALL_CONV);
    /* VM state and the separately allocated guest RAM do not overlap. This
     * allows offline LLVM optimization to hoist state/TLB loads across guest
     * RAM accesses rather than treating every guest store as a state mutation.
     */
    unsigned nounwind = LLVMGetEnumAttributeKindForName("nounwind", 8);
    LLVMAddAttributeAtIndex(start, LLVMAttributeFunctionIndex,
                            LLVMCreateEnumAttribute(t2c_context, nounwind, 0));
    unsigned noalias = LLVMGetEnumAttributeKindForName("noalias", 7);
    LLVMAddAttributeAtIndex(start, 1,
                            LLVMCreateEnumAttribute(t2c_context, noalias, 0));

    /* Function type for calling T2C blocks via jit_cache lookup.
     * Must match the actual T2C block signature: void f(riscv_t *rv)
     * Using pointer type (not i64) for correct cross-block calling. */
    LLVMTypeRef t2c_args[1] = {
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0)};
    t2c_jit_cache_func_type = LLVMFunctionType(
        LLVMVoidTypeInContext(t2c_context), t2c_args, 1, false);

    /* jit_cache struct: { uint32_t seq, priv_or_pad, uint64_t key, void *entry
     * }. SYSTEM uses the alignment word after seq for fetch privilege. LLVM
     * does not add this padding automatically, so add an explicit i32 field.
     * Field indices: 0=seq, 1=priv_or_pad, 2=key, 3=entry */
    LLVMTypeRef jit_cache_memb[4] = {
        LLVMInt32TypeInContext(t2c_context),
        LLVMInt32TypeInContext(t2c_context),
        LLVMInt64TypeInContext(t2c_context),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0)};
    t2c_jit_cache_struct_type =
        LLVMStructTypeInContext(t2c_context, jit_cache_memb, 4, false);

    /* inline_cache: { key, entry [, priv_mode] }. LLVM GEPs derive the
     * stride from this type (16 bytes user, 24 bytes SYSTEM). */
    LLVMTypeRef inline_cache_memb[] = {
        LLVMInt64TypeInContext(t2c_context),
        LLVMPointerType(LLVMVoidTypeInContext(t2c_context), 0),
#if RV32_HAS(SYSTEM)
        LLVMInt32TypeInContext(t2c_context),
#endif
    };
    t2c_inline_cache_struct_type = LLVMStructTypeInContext(
        t2c_context, inline_cache_memb, ARRAY_SIZE(inline_cache_memb), false);

    LLVMBasicBlockRef first_block =
        LLVMAppendBasicBlockInContext(t2c_context, start, "first_block");
    LLVMBuilderRef first_builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(first_builder, first_block);
    /* Count native tail entries too, rather than only dispatcher calls. */
    t2c_add_stat(first_builder, start, offsetof(riscv_t, aot_calls),
                 LLVMConstInt(LLVMInt64TypeInContext(t2c_context), 1, false));

    /* Create instruction counter alloca in entry block for mem2reg promotion.
     * LLVM's mem2reg pass promotes allocas in the entry block to SSA registers,
     * eliminating per-instruction memory traffic. The counter is initialized to
     * 0 and incremented by each T2C_OP. Timer is updated only at block exits.
     */
    LLVMValueRef insn_counter = LLVMBuildAlloca(
        first_builder, LLVMInt64TypeInContext(t2c_context), "insn_counter");
    LLVMBuildStore(first_builder,
                   LLVMConstInt(LLVMInt64TypeInContext(t2c_context), 0, false),
                   insn_counter);

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(t2c_context, start, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(t2c_context);
    LLVMPositionBuilderAtEnd(builder, entry);
#if !RV32_HAS(SYSTEM)
    LLVMBuildBr(first_builder, entry);
#endif
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
#if !RV32_HAS(SYSTEM)
    t2c_region_set = set;
#endif
    struct LLVM_block_map map;
    map.count = 0;
    /* Translate custom IR into LLVM IR */
    t2c_region_insns = 0;
#if !RV32_HAS(SYSTEM)
    t2c_exit_addr = PRIV(rv)->exit_addr;
#endif
    t2c_predicted_pc = 0;
#if RV32_HAS(SYSTEM)
    t2c_user_graph = block->priv_mode == RV_PRIV_U_MODE;
#else
    t2c_fold_stack_displacements = t2c_dense_stack_accesses(block);
    t2c_n_returns = 0;
    t2c_return_dispatch = NULL;
    t2c_return_pc = t2c_return_switch = NULL;
#endif
    t2c_trace_ebb(&builder, param_types, start, &entry, rv, block, set, &map,
                  insn_counter);
#if RV32_HAS(SYSTEM)
    /* Form one graph per code page. Every decoded instruction block is emitted
     * once, including joins and secondary entries reached by indirect branches.
     * The dispatcher and native tail calls pass the entry PC in rv->PC.
     */
    for (unsigned i = 0; i < RV_PG_SIZE / 2; i++) {
        block_t *candidate = t2c_page_blocks[i];
        if (!candidate || set_has(set, candidate->pc_start) ||
            map.count >= MAX_BLOCKS)
            continue;
        LLVMBasicBlockRef secondary =
            LLVMAppendBasicBlockInContext(t2c_context, start, "secondary");
        LLVMBuilderRef secondary_builder =
            LLVMCreateBuilderInContext(t2c_context);
        LLVMPositionBuilderAtEnd(secondary_builder, secondary);
        t2c_trace_ebb(&secondary_builder, param_types, start, &secondary, rv,
                      candidate, set, &map, insn_counter);
        LLVMDisposeBuilder(secondary_builder);
    }
    LLVMBasicBlockRef unknown =
        LLVMAppendBasicBlockInContext(t2c_context, start, "unknown_entry");
    /* The graph's code assumes the privilege class it was collected in,
     * which a fetch from its page proves. Leave if another class enters.
     */
    LLVMTypeRef i32 = LLVMInt32TypeInContext(t2c_context);
    LLVMValueRef mode =
        LLVMBuildLoad2(first_builder, i32,
                       t2c_gen_rv_field_ptr(start, &first_builder,
                                            offsetof(riscv_t, priv_mode), i32),
                       "entry_priv");
    LLVMValueRef user =
        LLVMBuildICmp(first_builder, LLVMIntEQ, mode,
                      LLVMConstInt(i32, RV_PRIV_U_MODE, false), "");
    LLVMBasicBlockRef entered =
        LLVMAppendBasicBlockInContext(t2c_context, start, "entry_class");
    t2c_gen_unlikely_br(
        first_builder,
        LLVMBuildICmp(first_builder, LLVMIntNE, user,
                      LLVMConstInt(LLVMInt1TypeInContext(t2c_context),
                                   t2c_user_graph, false),
                      ""),
        unknown, entered);
    LLVMPositionBuilderAtEnd(first_builder, entered);
    LLVMValueRef pc = LLVMBuildLoad2(
        first_builder, LLVMInt32TypeInContext(t2c_context),
        t2c_gen_PC_addr(start, &first_builder, NULL), "entry_pc");
    LLVMValueRef dispatch =
        LLVMBuildSwitch(first_builder, pc, unknown, map.count);
    /* Nothing has run yet. Have the dispatcher run this PC once through T1 or
     * the interpreter, or it would enter the same binding again.
     */
    LLVMPositionBuilderAtEnd(builder, unknown);
    LLVMBuildStore(
        builder, LLVMConstInt(LLVMInt8TypeInContext(t2c_context), 1, false),
        t2c_gen_rv_field_ptr(start, &builder, offsetof(riscv_t, aot_fallback),
                             LLVMInt8TypeInContext(t2c_context)));
    LLVMBuildRetVoid(builder);
    for (unsigned i = 0; i < map.count; i++) {
        LLVMBasicBlockRef gate =
            LLVMAppendBasicBlockInContext(t2c_context, start, "entry_budget");
        LLVMPositionBuilderAtEnd(builder, gate);
        LLVMAddCase(dispatch,
                    LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                 map.map[i].pc, false),
                    gate);
        if (t2c_local_indirect_switch &&
            t2c_local_targets[(map.map[i].pc & (RV_PG_SIZE - 1)) / 2])
            LLVMAddCase(t2c_local_indirect_switch,
                        LLVMConstInt(LLVMInt32TypeInContext(t2c_context),
                                     map.map[i].pc, false),
                        gate);
        t2c_gen_budget_check(&builder, start, map.map[i].pc, insn_counter);
        LLVMBuildBr(builder, map.map[i].block);
    }
#endif
#if !RV32_HAS(SYSTEM)
    /* Every return point the region traced is a case of its return switch. */
    for (unsigned i = 0; t2c_return_switch && i < t2c_n_returns; i++) {
        const uint32_t pc = t2c_return_pcs[i];
        bool seen = false;
        for (unsigned j = 0; j < i && !seen; j++)
            seen = t2c_return_pcs[j] == pc;
        LLVMBasicBlockRef target = seen ? NULL : t2c_block_map_search(&map, pc);
        if (target)
            LLVMAddCase(
                t2c_return_switch,
                LLVMConstInt(LLVMInt32TypeInContext(t2c_context), pc, false),
                target);
    }
    if (rv->jit_elide_align_checks)
        t2c_allow_unaligned_ram(start);
#endif
    t2c_preserve_zero(start);
#if RV32_HAS(SYSTEM)
    /* System page graphs keep guest registers in rv->X. A page has many exits
     * (budget yields, MMU slow paths, faults, indirect jumps), and promoted
     * registers must be written back at each of them. That roughly doubled
     * the Linux artifact and made instruction-cache misses dominate boot.
     */
#else
    t2c_promote_guest_regs(start, first_block);
#endif
    t2c_disambiguate_memory(start);

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

    /* Persist portable IR. Optimization and code generation run in
     * tools/aot.py, after collection has finished, and never in the execution
     * loop.
     */
    bool ok = LLVMWriteBitcodeToFile(module, path) == 0;
#if RV32_HAS(SYSTEM)
    FILE *entries = ok ? t2c_open_sidecar(path, ".entries") : NULL;
    bool written = entries;
    for (unsigned i = 0; written && i < map.count; i++)
        written = fprintf(entries, "%08x\n", map.map[i].pc) >= 0;
    ok = t2c_close_sidecar(entries, written) &&
         t2c_write_sidecar(path, ".digest", block->aot_page_digest, 32) &&
         /* Record coverage last: it lets a later collection skip this page. */
         t2c_write_sidecar(t2c_coverage_path, "", t2c_coverage,
                           sizeof(t2c_coverage));
#endif
    if (!ok)
        rv_log_error("Cannot write AOT bitcode: %s", path);
    LLVMDisposeBuilder(first_builder);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
    LLVMContextDispose(t2c_context);
    t2c_context = NULL;
    free(set);
    return ok;

abandon:
    LLVMDisposeBuilder(first_builder);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
    LLVMContextDispose(t2c_context);
    t2c_context = NULL;
    free(set);
    return false;
}
