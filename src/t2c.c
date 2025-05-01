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
#include <stdlib.h>

#include "jit.h"
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

#define T2C_OP(inst, code)                                                 \
    static void t2c_##inst(                                                \
        LLVMBuilderRef *builder UNUSED, LLVMTypeRef *param_types UNUSED,   \
        LLVMValueRef start UNUSED, LLVMBasicBlockRef *entry UNUSED,        \
        LLVMBuilderRef *taken_builder UNUSED,                              \
        LLVMBuilderRef *untaken_builder UNUSED, riscv_t *rv UNUSED,        \
        uint64_t mem_base UNUSED, rv_insn_t *ir UNUSED)                    \
    {                                                                      \
        LLVMValueRef timer_ptr = t2c_gen_timer_addr(start, builder, ir);   \
        LLVMValueRef timer =                                               \
            LLVMBuildLoad2(*builder, LLVMInt64Type(), timer_ptr, "");      \
        timer = LLVMBuildAdd(*builder, timer,                              \
                             LLVMConstInt(LLVMInt64Type(), 1, false), ""); \
        LLVMBuildStore(*builder, timer, timer_ptr);                        \
        code;                                                              \
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
T2C_LLVM_GEN_ADDR(timer, timer, 0);

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

FORCE_INLINE LLVMValueRef t2c_gen_mem_loc(LLVMValueRef start,
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

FORCE_INLINE void t2c_gen_call_io_func(LLVMValueRef start,
                                       LLVMBuilderRef *builder,
                                       LLVMTypeRef *param_types,
                                       int offset)
{
    LLVMValueRef func_offset = LLVMConstInt(LLVMInt32Type(), offset, true);
    LLVMValueRef addr_io_func = LLVMBuildInBoundsGEP2(
        *builder, LLVMPointerType(LLVMVoidType(), 0), LLVMGetParam(start, 0),
        &func_offset, 1, "addr_io_func");
    LLVMValueRef io_func = LLVMBuildLoad2(
        *builder,
        LLVMPointerType(LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), 0),
        addr_io_func, "io_func");
    LLVMValueRef io_param = LLVMGetParam(start, 0);
    LLVMBuildCall2(*builder,
                   LLVMFunctionType(LLVMVoidType(), param_types, 1, 0), io_func,
                   &io_param, 1, "");
}

static LLVMTypeRef t2c_jit_cache_func_type;
static LLVMTypeRef t2c_jit_cache_struct_type;

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
                                         rv_insn_t *ir UNUSED);

static void t2c_trace_ebb(LLVMBuilderRef *builder,
                          LLVMTypeRef *param_types UNUSED,
                          LLVMValueRef start,
                          LLVMBasicBlockRef *entry,
                          riscv_t *rv,
                          rv_insn_t *ir,
                          set_t *set,
                          struct LLVM_block_map *map)
{
    if (set_has(set, ir->pc))
        return;
    set_add(set, ir->pc);
    t2c_block_map_insert(map, entry, ir->pc);
    LLVMBuilderRef tk, utk;

    while (1) {
        ((t2c_codegen_block_func_t) dispatch_table[ir->opcode])(
            builder, param_types, start, entry, &tk, &utk, rv,
            (uint64_t) ((memory_t *) PRIV(rv)->mem)->mem_base, ir);
        if (!ir->next)
            break;
        ir = ir->next;
    }

    if (!t2c_insn_is_terminal(ir->opcode)) {
        if (ir->branch_untaken) {
            if (set_has(set, ir->branch_untaken->pc))
                LLVMBuildBr(utk,
                            t2c_block_map_search(map, ir->branch_untaken->pc));
            else {
                LLVMBasicBlockRef untaken_entry =
                    LLVMAppendBasicBlock(start,
                                         "untaken_"
                                         "entry");
                LLVMBuilderRef untaken_builder = LLVMCreateBuilder();
                LLVMPositionBuilderAtEnd(untaken_builder, untaken_entry);
                LLVMBuildBr(utk, untaken_entry);
                t2c_trace_ebb(&untaken_builder, param_types, start,
                              &untaken_entry, rv, ir->branch_untaken, set, map);
            }
        }
        if (ir->branch_taken) {
            if (set_has(set, ir->branch_taken->pc))
                LLVMBuildBr(tk,
                            t2c_block_map_search(map, ir->branch_taken->pc));
            else {
                LLVMBasicBlockRef taken_entry = LLVMAppendBasicBlock(start,
                                                                     "taken_"
                                                                     "entry");
                LLVMBuilderRef taken_builder = LLVMCreateBuilder();
                LLVMPositionBuilderAtEnd(taken_builder, taken_entry);
                LLVMBuildBr(tk, taken_entry);
                t2c_trace_ebb(&taken_builder, param_types, start, &taken_entry,
                              rv, ir->branch_taken, set, map);
            }
        }
    }
}

void t2c_compile(riscv_t *rv, block_t *block)
{
    LLVMModuleRef module = LLVMModuleCreateWithName("my_module");
    LLVMTypeRef io_members[] = {
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMPointerType(LLVMVoidType(), 0),
        LLVMPointerType(LLVMVoidType(), 0), LLVMInt8Type()};
    LLVMTypeRef struct_io = LLVMStructType(io_members, 12, false);
    LLVMTypeRef arr_X = LLVMArrayType(LLVMInt32Type(), 32);
    LLVMTypeRef rv_members[] = {LLVMInt8Type(), struct_io, arr_X,
                                LLVMInt32Type()};
    LLVMTypeRef struct_rv = LLVMStructType(rv_members, 4, false);
    LLVMTypeRef param_types[] = {LLVMPointerType(struct_rv, 0)};
    LLVMValueRef start = LLVMAddFunction(
        module, "start", LLVMFunctionType(LLVMVoidType(), param_types, 1, 0));

    LLVMTypeRef t2c_args[1] = {LLVMInt64Type()};
    t2c_jit_cache_func_type =
        LLVMFunctionType(LLVMVoidType(), t2c_args, 1, false);

    /* Notice to the alignment */
    LLVMTypeRef jit_cache_memb[2] = {LLVMInt64Type(),
                                     LLVMPointerType(LLVMVoidType(), 0)};
    t2c_jit_cache_struct_type = LLVMStructType(jit_cache_memb, 2, false);

    LLVMBasicBlockRef first_block = LLVMAppendBasicBlock(start, "first_block");
    LLVMBuilderRef first_builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(first_builder, first_block);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(start, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMBuildBr(first_builder, entry);
    set_t set;
    set_reset(&set);
    struct LLVM_block_map map;
    map.count = 0;
    /* Translate custon IR into LLVM IR */
    t2c_trace_ebb(&builder, param_types, start, &entry, rv, block->ir_head,
                  &set, &map);
    /* Offload LLVM IR to LLVM backend */
    char *error = NULL, *triple = LLVMGetDefaultTargetTriple();
    LLVMExecutionEngineRef engine;
    LLVMTargetRef target;
    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
        rv_log_fatal("Failed to create target");
        abort();
    }
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, LLVMGetHostCPUName(), LLVMGetHostCPUFeatures(),
        LLVMCodeGenLevelNone, LLVMRelocDefault, LLVMCodeModelJITDefault);
    LLVMPassBuilderOptionsRef pb_option = LLVMCreatePassBuilderOptions();
    /* Run aggressive optimization level and some selected Passes */
    LLVMRunPasses(module, "default<O3>,early-cse<memssa>,instcombine", tm,
                  pb_option);

    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        rv_log_fatal("Failed to create execution engine");
        abort();
    }

    /* Return the function pointer of T2C generated machine code */
    block->func = (exec_t2c_func_t) LLVMGetPointerToGlobal(engine, start);
    jit_cache_update(rv->jit_cache, block->pc_start, block->func);
    block->hot2 = true;
}

struct jit_cache *jit_cache_init()
{
    return calloc(N_JIT_CACHE_ENTRIES, sizeof(struct jit_cache));
}

void jit_cache_exit(struct jit_cache *cache)
{
    free(cache);
}

void jit_cache_update(struct jit_cache *cache, uint32_t pc, void *entry)
{
    uint32_t pos = pc & (N_JIT_CACHE_ENTRIES - 1);

    cache[pos].pc = pc;
    cache[pos].entry = entry;
}

void jit_cache_clear(struct jit_cache *cache)
{
    memset(cache, 0, N_JIT_CACHE_ENTRIES * sizeof(struct jit_cache));
}
