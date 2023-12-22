/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* This JIT implementation has undergone extensive modifications, heavily
 * relying on the ubpf_jit_x86_64.[ch] from ubpf. The original
 * ubpf_jit_x86_64.[ch] file served as the foundation and source of inspiration
 * for adapting and tailoring it specifically for this JIT implementation.
 * Therefore, credit and sincere thanks are extended to ubpf for their
 * invaluable work.
 *
 * Reference:
 *   https://github.com/iovisor/ubpf/blob/main/vm/ubpf_jit_x86_64.c
 */

#if !defined(__x86_64__)
#error "This implementation is dedicated to x86-64."
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cache.h"
#include "decode.h"
#include "io.h"
#include "jit_x64.h"
#include "riscv.h"
#include "utils.h"

enum VM_REG {
    VM_REG_0 = 0,
    VM_REG_1,
    VM_REG_2,
    VM_REG_3,
    VM_REG_4,
    VM_REG_5,
    VM_REG_6,
    VM_REG_7,
    VM_REG_8,
    VM_REG_9,
    VM_REG_10,
    N_VM_REGS,
};

#define X64_CLS_MASK 0x07
#define X64_ALU_OP_MASK 0xf0
#define X64_CLS_ALU 0x04
#define X64_CLS_ALU64 0x07
#define X64_SRC_IMM 0x00
#define X64_SRC_REG 0x08
#define X64_OP_MUL_IMM (X64_CLS_ALU | X64_SRC_IMM | 0x20)
#define X64_OP_MUL_REG (X64_CLS_ALU | X64_SRC_REG | 0x20)
#define X64_OP_DIV_IMM (X64_CLS_ALU | X64_SRC_IMM | 0x30)
#define X64_OP_DIV_REG (X64_CLS_ALU | X64_SRC_REG | 0x30)
#define X64_OP_MOD_IMM (X64_CLS_ALU | X64_SRC_IMM | 0x90)
#define X64_OP_MOD_REG (X64_CLS_ALU | X64_SRC_REG | 0x90)

#define STACK_SIZE 512
#define MAX_INSNS 1024

#if RV32_HAS(EXT_M)
static void muldivmod(struct jit_state *state,
                      uint8_t opcode,
                      int src,
                      int dst,
                      int32_t imm)
{
    bool mul = (opcode & X64_ALU_OP_MASK) == (X64_OP_MUL_IMM & X64_ALU_OP_MASK);
    bool div = (opcode & X64_ALU_OP_MASK) == (X64_OP_DIV_IMM & X64_ALU_OP_MASK);
    bool mod = (opcode & X64_ALU_OP_MASK) == (X64_OP_MOD_IMM & X64_ALU_OP_MASK);
    bool is64 = (opcode & X64_CLS_MASK) == X64_CLS_ALU64;
    bool reg = (opcode & X64_SRC_REG) == X64_SRC_REG;

    /* Short circuit for imm == 0 */
    if (!reg && imm == 0) {
        assert(NULL);
        if (div || mul) {
            /* For division and multiplication, set result to zero. */
            emit_alu32(state, 0x31, dst, dst);
        } else {
            /* For modulo, set result to dividend. */
            emit_mov(state, dst, dst);
        }
        return;
    }

    if (dst != RAX)
        emit_push(state, RAX);

    if (dst != RDX)
        emit_push(state, RDX);

    /*  Load the divisor into RCX */
    if (imm)
        emit_load_imm(state, RCX, imm);
    else
        emit_mov(state, src, RCX);

    /* Load the dividend into RAX */
    emit_mov(state, dst, RAX);

    /* The JIT employs two different semantics for division and modulus
     * operations. In the case of division, if the divisor is zero, the result
     * is set to zero. For modulus operations, if the divisor is zero, the
     * result becomes the dividend. To manage this, we first set the divisor to
     * 1 if it is initially zero. Then, we adjust the result accordingly: for
     * division, we set it to zero if the original divisor was zero; for
     * modulus, we set it to the dividend under the same condition.
     */

    if (div || mod) {
        /* Check if divisor is zero */
        if (is64)
            emit_alu64(state, 0x85, RCX, RCX);
        else
            emit_alu32(state, 0x85, RCX, RCX);

        /* Save the dividend for the modulo case */
        if (mod)
            emit_push(state, RAX); /* Save dividend */

        /* Save the result of the test */
        emit1(state, 0x9c); /* pushfq */

        /* Set the divisor to 1 if it is zero */
        emit_load_imm(state, RDX, 1);
        emit1(state, 0x48);
        emit1(state, 0x0f);
        emit1(state, 0x44);
        emit1(state, 0xca); /* cmove rcx, rdx */

        /* xor %edx,%edx */
        emit_alu32(state, 0x31, RDX, RDX);
    }

    if (is64)
        emit_rex(state, 1, 0, 0, 0);

    /* Multiply or divide */
    emit_alu32(state, 0xf7, mul ? 4 : 6, RCX);

    /* The division operation stores the remainder in RDX and the quotient
     * in RAX.
     */
    if (div || mod) {
        /* Restore the result of the test */
        emit1(state, 0x9d); /* popfq */

        /* If zero flag is set, then the divisor was zero. */

        if (div) {
            /* Set the dividend to zero if the divisor was zero. */
            emit_load_imm(state, RCX, 0);

            /* Store 0 in RAX if the divisor was zero. */
            /* Use conditional move to avoid a branch. */
            emit1(state, 0x48);
            emit1(state, 0x0f);
            emit1(state, 0x44);
            emit1(state, 0xc1); /* cmove rax, rcx */
        } else {
            /* Restore dividend to RCX */
            emit_pop(state, RCX);

            /* Store the dividend in RAX if the divisor was zero. */
            /* Use conditional move to avoid a branch. */
            emit1(state, 0x48);
            emit1(state, 0x0f);
            emit1(state, 0x44);
            emit1(state, 0xd1); /* cmove rdx, rcx */
        }
    }

    if (dst != RDX) {
        if (mod)
            emit_mov(state, RDX, dst);
        emit_pop(state, RDX);
    }
    if (dst != RAX) {
        if (div || mul)
            emit_mov(state, RAX, dst);
        emit_pop(state, RAX);
    }
}
#endif

#define REGISTER_MAP_SIZE 11

/* There are two common x86-64 calling conventions, discussed at:
 * https://en.wikipedia.org/wiki/X64_calling_conventions#x86-64_calling_conventions
 *
 * Please note: R12 is an exception and is *not* being used. Consequently, it
 * is omitted from the list of non-volatile registers for both platforms,
 * despite being non-volatile.
 */
#if defined(_WIN32)
static int nonvolatile_reg[] = {RBP, RBX, RDI, RSI, R13, R14, R15};
static int parameter_reg[] = {RCX, RDX, R8, R9};
#define RCX_ALT R10
static int register_map[REGISTER_MAP_SIZE] = {
    RAX, R10, RDX, R8, R9, R14, R15, RDI, RSI, RBX, RBP,
};
#else
#define RCX_ALT R9
static const int nonvolatile_reg[] = {RBP, RBX, R13, R14, R15};
static const int parameter_reg[] = {RDI, RSI, RDX, RCX, R8, R9};
static const int register_map[REGISTER_MAP_SIZE] = {
    RAX, RDI, RSI, RDX, R9, R8, RBX, R13, R14, R15, RBP,
};
#endif

/* Return the x86 register for the given JIT register */
static int map_register(int r)
{
    assert(r < N_VM_REGS);
    return register_map[r % N_VM_REGS];
}

#define SET_SIZE_BITS 10
#define SET_SIZE (1 << SET_SIZE_BITS)
#define SET_SLOTS_SIZE 32
HASH_FUNC_IMPL(set_hash, SET_SIZE_BITS, 1 << SET_SIZE_BITS);

/* The set consists of SET_SIZE buckets, with each bucket containing
 * SET_SLOTS_SIZE slots.
 */
typedef struct {
    uint32_t table[SET_SIZE][SET_SLOTS_SIZE];
} set_t;

/**
 * set_reset - clear a set
 * @set: a pointer points to target set
 */
static inline void set_reset(set_t *set)
{
    memset(set, 0, sizeof(set_t));
}

/**
 * set_add - insert a new element into the set
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_add(set_t *set, uint32_t key)
{
    const uint32_t index = set_hash(key);
    uint8_t count = 0;
    while (set->table[index][count]) {
        if (set->table[index][count++] == key)
            return false;
    }

    set->table[index][count] = key;
    return true;
}

/**
 * set_has - check whether the element exist in the set or not
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_has(set_t *set, uint32_t key)
{
    const uint32_t index = set_hash(key);
    for (uint8_t count = 0; set->table[index][count]; count++) {
        if (set->table[index][count] == key)
            return true;
    }
    return false;
}

#define UPDATE_PC(pc)                             \
    emit_load_imm(state, RAX, (pc));              \
    emit_store(state, S32, RAX, parameter_reg[0], \
               offsetof(struct riscv_internal, PC));

static void prepare_translate(struct jit_state *state)
{
    /* Save platform non-volatile registers */
    for (uint32_t i = 0; i < ARRAYS_SIZE(nonvolatile_reg); i++)
        emit_push(state, nonvolatile_reg[i]);

    /* Assuming that the stack is 16-byte aligned just before the call
     * instruction that brought us to this code, we need to restore 16-byte
     * alignment upon starting execution of the JIT'd code. STACK_SIZE is
     * guaranteed to be divisible by 16. However, if an even number of
     * registers were pushed onto the stack during state saving (see above),
     * an additional 8 bytes must be added to regain 16-byte alignment.
     */
    if (!(ARRAYS_SIZE(nonvolatile_reg) % 2))
        emit_alu64_imm32(state, 0x81, 5, RSP, 0x8);

    /* Set JIT R10 (the way to access the frame in JIT) to match RSP. */
    emit_mov(state, RSP, map_register(VM_REG_10));

    /* Allocate stack space */
    emit_alu64_imm32(state, 0x81, 5, RSP, STACK_SIZE);

#if defined(_WIN32)
    /* Windows x64 ABI requires home register space. */
    /* Allocate home register space - 4 registers */
    emit_alu64_imm32(state, 0x81, 5, RSP, 4 * sizeof(uint64_t));
#endif

    /* Jump to the entry point, which is stored in the second parameter. */
    emit1(state, 0xff);
    emit1(state, 0xe6);

    /* Epilogue */
    state->exit_loc = state->offset;

    /* Move register 0 into rax */
    if (map_register(VM_REG_0) != RAX)
        emit_mov(state, map_register(VM_REG_0), RAX);

    /* Deallocate stack space by restoring RSP from JIT R10. */
    emit_mov(state, map_register(VM_REG_10), RSP);

    if (!(ARRAYS_SIZE(nonvolatile_reg) % 2))
        emit_alu64_imm32(state, 0x81, 0, RSP, 0x8);

    /* Restore platform non-volatile registers */
    for (uint32_t i = 0; i < ARRAYS_SIZE(nonvolatile_reg); i++)
        emit_pop(state, nonvolatile_reg[ARRAYS_SIZE(nonvolatile_reg) - i - 1]);

    /* Return */
    emit1(state, 0xc3);
}

#define X64(inst, code)                                                       \
    static void do_##inst(struct jit_state *state UNUSED, riscv_t *rv UNUSED, \
                          rv_insn_t *ir UNUSED)                               \
    {                                                                         \
        code;                                                                 \
    }
#include "rv32_jit_template.c"
#undef X64

static void do_fuse1(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        emit_load_imm(state, RAX, fuse[i].imm);
        emit_store(state, S32, RAX, parameter_reg[0],
                   offsetof(struct riscv_internal, X) + 4 * fuse[i].rd);
    }
}

static void do_fuse2(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    emit_load_imm(state, RAX, ir->imm);
    emit_store(state, S32, RAX, parameter_reg[0],
               offsetof(struct riscv_internal, X) + 4 * ir->rd);
    emit_load(state, S32, parameter_reg[0], RBX,
              offsetof(struct riscv_internal, X) + 4 * ir->rs1);
    emit_alu32(state, 0x01, RBX, RAX);
    emit_store(state, S32, RAX, parameter_reg[0],
               offsetof(struct riscv_internal, X) + 4 * ir->rs2);
}

static void do_fuse3(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = ((state_t *) rv->userdata)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        emit_load(state, S32, parameter_reg[0], RAX,
                  offsetof(struct riscv_internal, X) + 4 * fuse[i].rs1);
        emit_load_imm(state, RBX, (intptr_t) (m->mem_base + fuse[i].imm));
        emit_alu64(state, 0x01, RBX, RAX);
        emit_load(state, S32, parameter_reg[0], RBX,
                  offsetof(struct riscv_internal, X) + 4 * fuse[i].rs2);
        emit_store(state, S32, RBX, RAX, 0);
    }
}

static void do_fuse4(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = ((state_t *) rv->userdata)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        emit_load(state, S32, parameter_reg[0], RAX,
                  offsetof(struct riscv_internal, X) + 4 * fuse[i].rs1);
        emit_load_imm(state, RBX, (intptr_t) (m->mem_base + fuse[i].imm));
        emit_alu64(state, 0x01, RBX, RAX);
        emit_load(state, S32, RAX, RBX, 0);
        emit_store(state, S32, RBX, parameter_reg[0],
                   offsetof(struct riscv_internal, X) + 4 * fuse[i].rd);
    }
}

static void do_fuse5(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    emit_load_imm(state, RAX, ir->pc + 4);
    emit_store(state, S32, RAX, parameter_reg[0],
               offsetof(struct riscv_internal, PC));
    emit_call(state, (intptr_t) rv->io.on_memset);
    emit_exit(&(*state));
}

static void do_fuse6(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    emit_load_imm(state, RAX, ir->pc + 4);
    emit_store(state, S32, RAX, parameter_reg[0],
               offsetof(struct riscv_internal, PC));
    emit_call(state, (intptr_t) rv->io.on_memcpy);
    emit_exit(&(*state));
}

static void do_fuse7(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            emit_load(state, S32, parameter_reg[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * fuse[i].rs1);
            emit_alu32_imm8(state, 0xc1, 4, RAX, fuse[i].imm & 0x1f);
            emit_store(state, S32, RAX, parameter_reg[0],
                       offsetof(struct riscv_internal, X) + 4 * fuse[i].rd);
            break;
        case rv_insn_srli:
            emit_load(state, S32, parameter_reg[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * fuse[i].rs1);
            emit_alu32_imm8(state, 0xc1, 5, RAX, fuse[i].imm & 0x1f);
            emit_store(state, S32, RAX, parameter_reg[0],
                       offsetof(struct riscv_internal, X) + 4 * fuse[i].rd);
            break;
        case rv_insn_srai:
            emit_load(state, S32, parameter_reg[0], RAX,
                      offsetof(struct riscv_internal, X) + 4 * fuse[i].rs1);
            emit_alu32_imm8(state, 0xc1, 7, RAX, fuse[i].imm & 0x1f);
            emit_store(state, S32, RAX, parameter_reg[0],
                       offsetof(struct riscv_internal, X) + 4 * fuse[i].rd);
            break;
        default:
            __UNREACHABLE;
            break;
        }
    }
}

/* clang-format off */
static const void *dispatch_table[] = {
    /* RV32 instructions */
#define _(inst, can_branch, insn_len, translatable, reg_mask) [rv_insn_##inst] = do_##inst,
    RV_INSN_LIST
#undef _
    /* Macro operation fusion instructions */
#define _(inst) [rv_insn_##inst] = do_##inst,
    FUSE_INSN_LIST
#undef _
};
/* clang-format on */

typedef void (*codegen_block_func_t)(struct jit_state *,
                                     riscv_t *,
                                     rv_insn_t *);

static void translate(struct jit_state *state, riscv_t *rv, block_t *block)
{
    uint32_t idx;
    rv_insn_t *ir, *next;
    for (idx = 0, ir = block->ir_head; idx < block->n_insn; idx++, ir = next) {
        next = ir->next;
        ((codegen_block_func_t) dispatch_table[ir->opcode])(state, rv, ir);
    }
}

static void resolve_jumps(struct jit_state *state)
{
    for (int i = 0; i < state->num_jumps; i++) {
        struct jump jump = state->jumps[i];
        int target_loc;
        if (jump.target_offset != 0)
            target_loc = jump.target_offset;
        else if (jump.target_pc == TARGET_PC_EXIT)
            target_loc = state->exit_loc;
        else if (jump.target_pc == TARGET_PC_RETPOLINE)
            target_loc = state->retpoline_loc;
        else {
            target_loc = jump.offset_loc + sizeof(uint32_t);
            for (int i = 0; i < state->num_insn; i++) {
                if (jump.target_pc == state->offset_map[i].PC) {
                    target_loc = state->offset_map[i].offset;
                    break;
                }
            }
        }
        /* Assumes jump offset is at end of instruction */
        uint32_t rel = target_loc - (jump.offset_loc + sizeof(uint32_t));

        uint8_t *offset_ptr = &state->buf[jump.offset_loc];
        memcpy(offset_ptr, &rel, sizeof(uint32_t));
    }
}

static void translate_chained_block(struct jit_state *state,
                                    riscv_t *rv,
                                    block_t *block,
                                    set_t *set)
{
    if (set_has(set, block->pc_start))
        return;

    set_add(set, block->pc_start);
    offset_map_insert(state, block->pc_start);
    translate(state, rv, block);
    rv_insn_t *ir = block->ir_tail;
    if (ir->branch_untaken && !set_has(set, ir->pc + 4)) {
        block_t *block1 = cache_get(rv->block_cache, ir->pc + 4);
        if (block1 && block1->translatable)
            translate_chained_block(state, rv, block1, set);
    }
    if (ir->branch_taken && !set_has(set, ir->pc + ir->imm)) {
        block_t *block1 = cache_get(rv->block_cache, ir->pc + ir->imm);
        if (block1 && block1->translatable)
            translate_chained_block(state, rv, block1, set);
    }
}

uint32_t translate_x64(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    memset(state->offset_map, 0, MAX_INSNS * sizeof(struct offset_map));
    memset(state->jumps, 0, MAX_INSNS * sizeof(struct jump));
    state->num_insn = 0;
    state->num_jumps = 0;
    uint32_t entry_loc = state->offset;
    set_t set;
    set_reset(&set);
    translate_chained_block(&(*state), rv, block, &set);

    if (state->offset == state->size) {
        printf("Target buffer too small\n");
        goto out;
    }
    resolve_jumps(&(*state));
out:
    return entry_loc;
}

struct jit_state *init_state(size_t size)
{
    struct jit_state *state = malloc(sizeof(struct jit_state));
    state->offset = 0;
    state->size = size;
    state->buf = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS
#if defined(__APPLE__)
                          | MAP_JIT
#endif
                      ,
                      -1, 0);
    assert(state->buf != MAP_FAILED);
    prepare_translate(state);
    state->offset_map = calloc(MAX_INSNS, sizeof(struct offset_map));
    state->jumps = calloc(MAX_INSNS, sizeof(struct jump));
    return state;
}

void destroy_state(struct jit_state *state)
{
    munmap(state->buf, state->size);
    free(state->offset_map);
    free(state->jumps);
    free(state);
}
