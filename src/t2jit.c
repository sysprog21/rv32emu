#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdlib.h>

#include "riscv_private.h"
#include "t2jit.h"

#define MAX_BLOCKS 8152

struct LLVM_block_map_entry {
    uint32_t pc;
    LLVMBasicBlockRef block;
};

struct LLVM_block_map {
    uint32_t count;
    struct LLVM_block_map_entry map[MAX_BLOCKS];
};

FORCE_INLINE void LLVM_block_map_insert(struct LLVM_block_map *map,
                                        LLVMBasicBlockRef *entry,
                                        uint32_t pc)
{
    struct LLVM_block_map_entry map_entry;
    map_entry.block = *entry;
    map_entry.pc = pc;
    map->map[map->count++] = map_entry;
    return;
}

FORCE_INLINE LLVMBasicBlockRef LLVM_block_map_search(struct LLVM_block_map *map,
                                                     uint32_t pc)
{
    for (uint32_t i = 0; i < map->count; i++) {
        if (map->map[i].pc == pc) {
            return map->map[i].block;
        }
    }
    return NULL;
}

#define RVT2OP(inst, code)                                                \
    static void t2_##inst(                                                \
        LLVMBuilderRef *builder UNUSED, LLVMTypeRef *param_types UNUSED,  \
        LLVMValueRef start UNUSED, LLVMBasicBlockRef *entry UNUSED,       \
        LLVMBuilderRef *taken_builder UNUSED,                             \
        LLVMBuilderRef *untaken_builder UNUSED, uint64_t mem_base UNUSED, \
        rv_insn_t *ir UNUSED)                                             \
    {                                                                     \
        code;                                                             \
    }

#include "t2_rv32_template.c"
#undef RVT2OP

static const void *dispatch_table[] = {
/* RV32 instructions */
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = t2_##inst,
    RV_INSN_LIST
#undef _
/* Macro operation fusion instructions */
#define _(inst) [rv_insn_##inst] = t2_##inst,
        FUSE_INSN_LIST
#undef _
};

FORCE_INLINE bool insn_is_unconditional_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_ecall:
    case rv_insn_ebreak:
    case rv_insn_jalr:
    case rv_insn_mret:
    case rv_insn_fuse5:
    case rv_insn_fuse6:
#if RV32_HAS(EXT_C)
    case rv_insn_cjalr:
    case rv_insn_cjr:
    case rv_insn_cebreak:
#endif
        return true;
    }
    return false;
}

typedef void (*t2_codegen_block_func_t)(LLVMBuilderRef *builder UNUSED,
                                        LLVMTypeRef *param_types UNUSED,
                                        LLVMValueRef start UNUSED,
                                        LLVMBasicBlockRef *entry UNUSED,
                                        LLVMBuilderRef *taken_builder UNUSED,
                                        LLVMBuilderRef *untaken_builder UNUSED,
                                        uint64_t mem_base UNUSED,
                                        rv_insn_t *ir UNUSED);

static void trace_ebb(LLVMBuilderRef *builder,
                      LLVMTypeRef *param_types UNUSED,
                      LLVMValueRef start,
                      LLVMBasicBlockRef *entry,
                      uint64_t mem_base,
                      rv_insn_t *ir,
                      set_t *set,
                      struct LLVM_block_map *map)
{
    if (set_has(set, ir->pc))
        return;
    set_add(set, ir->pc);
    LLVM_block_map_insert(map, entry, ir->pc);
    LLVMBuilderRef tk, utk;

    while (1) {
        ((t2_codegen_block_func_t) dispatch_table[ir->opcode])(
            builder, param_types, start, entry, &tk, &utk, mem_base, ir);
        if (!ir->next)
            break;
        ir = ir->next;
    }
    
    if (!insn_is_unconditional_branch(ir->opcode)) {
        if (ir->branch_untaken) {
            if (set_has(set, ir->branch_untaken->pc))
                LLVMBuildBr(utk,
                            LLVM_block_map_search(map, ir->branch_untaken->pc));
            else {
                LLVMBasicBlockRef untaken_entry =
                    LLVMAppendBasicBlock(start,
                                         "untaken_"
                                         "entry");
                LLVMBuilderRef untaken_builder = LLVMCreateBuilder();
                LLVMPositionBuilderAtEnd(untaken_builder, untaken_entry);
                LLVMBuildBr(utk, untaken_entry);
                trace_ebb(&untaken_builder, param_types, start, &untaken_entry,
                          mem_base, ir->branch_untaken, set, map);
            }
        }
        if (ir->branch_taken) {
            if (set_has(set, ir->branch_taken->pc))
                LLVMBuildBr(tk,
                            LLVM_block_map_search(map, ir->branch_taken->pc));
            else {
                LLVMBasicBlockRef taken_entry = LLVMAppendBasicBlock(start,
                                                                     "taken_"
                                                                     "entry");
                LLVMBuilderRef taken_builder = LLVMCreateBuilder();
                LLVMPositionBuilderAtEnd(taken_builder, taken_entry);
                LLVMBuildBr(tk, taken_entry);
                trace_ebb(&taken_builder, param_types, start, &taken_entry,
                          mem_base, ir->branch_taken, set, map);
            }
        }
    }
}

void t2_compile(block_t *block, uint64_t mem_base)
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
    trace_ebb(&builder, param_types, start, &entry, mem_base, block->ir_head,
              &set, &map);
    /* Offload LLVM IR to LLVM backend */
    char *error = NULL, *triple = LLVMGetDefaultTargetTriple();
    LLVMExecutionEngineRef engine;
    LLVMTargetRef target;
    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    if (LLVMGetTargetFromTriple(triple, &target, &error) != 0) {
        fprintf(stderr,
                "failed to create "
                "Target\n");
    }
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, LLVMGetHostCPUName(), LLVMGetHostCPUFeatures(),
        LLVMCodeGenLevelNone, LLVMRelocDefault, LLVMCodeModelJITDefault);
    LLVMPassBuilderOptionsRef pb_option = LLVMCreatePassBuilderOptions();
    /* Run aggressive optimization level and some selected Passes */
    LLVMRunPasses(module,
                  "default<O3>,dce,early-cse<"
                  "memssa>,instcombine,memcpyopt",
                  tm, pb_option);

    if (LLVMCreateExecutionEngineForModule(&engine, module, &error) != 0) {
        fprintf(stderr,
                "failed to create "
                "execution engine\n");
        abort();
    }

    /* Return the function pointer of T2C generated machine code */
    block->func = (funcPtr_t) LLVMGetPointerToGlobal(engine, start);
    block->hot2 = true;
}