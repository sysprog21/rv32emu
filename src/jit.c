/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* This JIT implementation has undergone extensive modifications, heavily
 * relying on the ubpf_jit_[x86_64|arm64].[c|h] from ubpf. The original
 * ubpf_jit_[x86_64|arm64].[c|h] file served as the foundation and source of
 * inspiration for adapting and tailoring it specifically for this JIT
 * implementation. Therefore, credit and sincere thanks are extended to ubpf for
 * their invaluable work.
 *
 * Reference:
 *   https://github.com/iovisor/ubpf/blob/main/vm/ubpf_jit_x86_64.c
 *   https://github.com/iovisor/ubpf/blob/main/vm/ubpf_jit_arm64.c
 */

#if !RV32_HAS(JIT)
#error "Do not manage to build this file unless you enable JIT support."
#endif

#if !defined(__x86_64__) && !defined(__aarch64__)
#error "This implementation is dedicated to x64 and arm64."
#endif

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#if defined(__aarch64__)
#include <pthread.h>
#endif
#endif

#include "cache.h"
#include "decode.h"
#include "io.h"
#include "jit.h"
#include "log.h"
#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"

#if RV32_HAS(SYSTEM)
#include "system.h"
#endif

#define JIT_CLS_MASK 0x07
#define JIT_ALU_OP_MASK 0xf0
#define JIT_CLS_ALU 0x04
#define JIT_CLS_ALU64 0x07
#define JIT_SRC_IMM 0x00
#define JIT_SRC_REG 0x08
#define JIT_OP_MUL_IMM (JIT_CLS_ALU | JIT_SRC_IMM | 0x20)
#define JIT_OP_MUL_REG (JIT_CLS_ALU | JIT_SRC_REG | 0x20)
#define JIT_OP_DIV_IMM (JIT_CLS_ALU | JIT_SRC_IMM | 0x30)
#define JIT_OP_DIV_REG (JIT_CLS_ALU | JIT_SRC_REG | 0x30)
#define JIT_OP_MOD_IMM (JIT_CLS_ALU | JIT_SRC_IMM | 0x90)
#define JIT_OP_MOD_REG (JIT_CLS_ALU | JIT_SRC_REG | 0x90)

#define STACK_SIZE 512
#define MAX_JUMPS 1024
#define MAX_BLOCKS 8192
#define IN_JUMP_THRESHOLD 256
#if defined(__x86_64__)
/* indicate where the immediate value is in the emitted jump instruction */
#define JUMP_LOC_0 jump_loc_0 + 2
#if RV32_HAS(SYSTEM)
#define JUMP_LOC_1 jump_loc_1 + 1
#endif
/* Special values for target_pc in struct jump */
#define TARGET_PC_EXIT -1U
#define TARGET_PC_RETPOLINE -3U
enum x64_reg {
    RAX,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RIP = 5,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
};

#elif defined(__aarch64__)
/* indicate where the immediate value is in the emitted jump instruction */
#define JUMP_LOC_0 jump_loc_0
#if RV32_HAS(SYSTEM)
#define JUMP_LOC_1 jump_loc_1
#endif
/* Special values for target_pc in struct jump */
#define TARGET_PC_EXIT ~UINT32_C(0)
#define TARGET_PC_ENTER (~UINT32_C(0) & 0x0101)
/* This is guaranteed to be an illegal A64 instruction. */
#define BAD_OPCODE ~UINT32_C(0)

enum a64_reg {
    R0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    R16,
    R17,
    R18,
    R19,
    R20,
    R21,
    R22,
    R23,
    R24,
    R25,
    R26,
    R27,
    R28,
    R29,
    R30,
    SP,
    RZ = 31
};

typedef enum {
    /* AddSubOpcode */
    AS_ADD = 0,
    AS_SUB = 2,
    AS_SUBS = 3,
    /* LogicalOpcode */
    LOG_AND = 0x00000000U, /* 0000_0000_0000_0000_0000_0000_0000_0000 */
    LOG_ORR = 0x20000000U, /* 0010_0000_0000_0000_0000_0000_0000_0000 */
    LOG_ORN = 0x20200000U, /* 0010_0000_0010_0000_0000_0000_0000_0000 */
    LOG_EOR = 0x40000000U, /* 0100_0000_0000_0000_0000_0000_0000_0000 */
    /* LoadStoreOpcode */
    LS_STRB = 0x00000000U,   /* 0000_0000_0000_0000_0000_0000_0000_0000 */
    LS_LDRB = 0x00400000U,   /* 0000_0000_0100_0000_0000_0000_0000_0000 */
    LS_LDRSBW = 0x00c00000U, /* 0000_0000_1100_0000_0000_0000_0000_0000 */
    LS_STRH = 0x40000000U,   /* 0100_0000_0000_0000_0000_0000_0000_0000 */
    LS_LDRH = 0x40400000U,   /* 0100_0000_0100_0000_0000_0000_0000_0000 */
    LS_LDRSHW = 0x40c00000U, /* 0100_0000_1100_0000_0000_0000_0000_0000 */
    LS_STRW = 0x80000000U,   /* 1000_0000_0000_0000_0000_0000_0000_0000 */
    LS_LDRW = 0x80400000U,   /* 1000_0000_0100_0000_0000_0000_0000_0000 */
    LS_LDRSW = 0x80800000U,  /* 1000_0000_1000_0000_0000_0000_0000_0000 */
    LS_STRX = 0xc0000000U,   /* 1100_0000_0000_0000_0000_0000_0000_0000 */
    LS_LDRX = 0xc0400000U,   /* 1100_0000_0100_0000_0000_0000_0000_0000 */
    /* LoadStorePairOpcode */
    LSP_STPX = 0xa9000000U, /* 1010_1001_0000_0000_0000_0000_0000_0000 */
    LSP_LDPX = 0xa9400000U, /* 1010_1001_0100_0000_0000_0000_0000_0000 */
    /* UnconditionalBranchOpcode */
    BR_BR = 0xd61f0000U,  /* 1101_0110_0001_1111_0000_0000_0000_0000 */
    BR_BLR = 0xd63f0000U, /* 1101_0110_0011_1111_0000_0000_0000_0000 */
    BR_RET = 0xd65f0000U, /* 1101_0110_0101_1111_0000_0000_0000_0000 */
    /* UnconditionalBranchImmediateOpcode */
    UBR_B = 0x14000000U, /* 0001_0100_0000_0000_0000_0000_0000_0000 */
    /* ConditionalBranchImmediateOpcode */
    BR_Bcond = 0x54000000U,
    /* DP2Opcode */
    DP2_UDIV = 0x1ac00800U, /* 0001_1010_1100_0000_0000_1000_0000_0000 */
    DP2_LSLV = 0x1ac02000U, /* 0001_1010_1100_0000_0010_0000_0000_0000 */
    DP2_LSRV = 0x1ac02400U, /* 0001_1010_1100_0000_0010_0100_0000_0000 */
    DP2_ASRV = 0x1ac02800U, /* 0001_1010_1100_0000_0010_1000_0000_0000 */
    /* DP3Opcode */
    DP3_MADD = 0x1b000000U, /* 0001_1011_0000_0000_0000_0000_0000_0000 */
    DP3_MSUB = 0x1b008000U, /* 0001_1011_0000_0000_1000_0000_0000_0000 */
    /* MoveWideOpcode */
    MW_MOVN = 0x12800000U, /* 0001_0010_1000_0000_0000_0000_0000_0000 */
    MW_MOVZ = 0x52800000U, /* 0101_0010_1000_0000_0000_0000_0000_0000 */
    MW_MOVK = 0x72800000U, /* 0111_0010_1000_0000_0000_0000_0000_0000 */
} a64opcode_t;

enum condition {
    COND_EQ,
    COND_NE,
    COND_HS,
    COND_LO,
    COND_GE = 10,
    COND_LT = 11,
    COND_AL = 14,
};

enum {
    temp_imm_reg = R24, /* Temp register for immediate generation */
    temp_div_reg = R25, /* Temp register for division results */
};
#endif

enum operand_size {
    S8,
    S16,
    S32,
    S64,
};

#if defined(__x86_64__)
/* There are two common x86-64 calling conventions, discussed at:
 * https://en.wikipedia.org/wiki/X64_calling_conventions#x86-64_calling_conventions
 *
 * Please note: R12 is an exception and is *not* being used. Consequently, it
 * is omitted from the list of non-volatile registers for both platforms,
 * despite being non-volatile.
 */
#if defined(_WIN32)
static const int nonvolatile_reg[] = {RBP, RBX, RDI, RSI, R13, R14, R15};
static const int parameter_reg[] = {RCX, RDX, R8, R9};
static struct host_reg register_map[] = {
    {RAX, -1, 0, 0}, {R10, -1, 0, 0}, {RDX, -1, 0, 0}, {R8, -1, 0, 0},
    {R9, -1, 0, 0},  {R14, -1, 0, 0}, {R15, -1, 0, 0}, {RDI, -1, 0, 0},
    {RSI, -1, 0, 0}, {RBX, -1, 0, 0}, {RBP, -1, 0, 0},
};
static int temp_reg = RCX;
#else
static const int nonvolatile_reg[] = {RBP, RBX, R13, R14, R15};
static const int parameter_reg[] = {RDI, RSI, RDX, RCX, R8, R9};
static struct host_reg register_map[] = {
    {RAX, -1, 0, 0}, {RBX, -1, 0, 0}, {RDX, -1, 0, 0}, {R8, -1, 0, 0},
    {R9, -1, 0, 0},  {R10, -1, 0, 0}, {R11, -1, 0, 0}, {R13, -1, 0, 0},
    {R14, -1, 0, 0}, {R15, -1, 0, 0},
};
static int temp_reg = RCX;
#endif
#elif defined(__aarch64__)
/* callee_reg - this must be a multiple of two because of how we save the stack
 * later on.
 */
static const int callee_reg[] = {R19, R20, R21, R22, R23, R24, R25, R26};
/* parameter_reg (Caller saved registers) */
static const int parameter_reg[] = {R0, R1, R2, R3, R4};
static int temp_reg = R8;

/* Register assignments:
 * Arm64       Usage
 *   r0 - r4   Function parameters, caller-saved
 *   r6 - r8   Temp - used for storing calculated value during execution
 *   r19 - r23 Callee-saved registers
 *   r24       Temp - used for generating 32-bit immediates
 *   r25       Temp - used for modulous calculations
 */
static struct host_reg register_map[] = {
    {R5, -1, 0, 0},  {R6, -1, 0, 0},  {R7, -1, 0, 0},  {R9, -1, 0, 0},
    {R11, -1, 0, 0}, {R12, -1, 0, 0}, {R13, -1, 0, 0}, {R14, -1, 0, 0},
    {R15, -1, 0, 0}, {R16, -1, 0, 0}, {R17, -1, 0, 0}, {R18, -1, 0, 0},
    {R26, -1, 0, 0},
};
#endif

static const int n_host_regs =
    ARRAY_SIZE(register_map); /* the number of avavliable host register */

static inline void set_dirty(int reg_idx, bool is_dirty)
{
    for (int i = 0; i < n_host_regs; i++) {
        /* ignore nonvolatile and parameter registers */
        if (register_map[i].reg_idx != reg_idx)
            continue;

        register_map[i].dirty = is_dirty;
        return;
    }
}

static inline void offset_map_insert(struct jit_state *state, block_t *block)
{
    assert(state->n_blocks < MAX_BLOCKS);

    struct offset_map *map_entry = &state->offset_map[state->n_blocks++];
    map_entry->pc = block->pc_start;
    map_entry->offset = state->offset;
#if RV32_HAS(SYSTEM)
    map_entry->satp = block->satp;
#endif
}

#if !defined(__APPLE__)
#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *) (addr), (char *) (addr) + (size));
#endif

/* JIT debug helpers - enable with ENABLE_JIT_DEBUG=1 to detect issues early */
#ifndef ENABLE_JIT_DEBUG
#define ENABLE_JIT_DEBUG 0
#endif

#if ENABLE_JIT_DEBUG
static void jit_dump_regmap(const char *ctx)
{
    rv_log_debug("JIT RegMap [%s]:", ctx);
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx >= 0) {
            rv_log_debug("  Host R%d -> VM x%d (dirty=%d)",
                         register_map[i].reg_idx, register_map[i].vm_reg_idx,
                         register_map[i].dirty);
        }
    }
}

static void jit_check_regmap_conflict(int vm_reg,
                                      int host_reg,
                                      const char *insn)
{
    int found_idx = -1;
    /* Check if VM register is already mapped */
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx == vm_reg) {
            if (found_idx >= 0 && found_idx != i) {
                /* VM register mapped to multiple host registers */
                rv_log_error(
                    "JIT RegMap CONFLICT in %s: VM x%d mapped to "
                    "Host R%d (idx %d) and R%d (idx %d)",
                    insn, vm_reg, register_map[found_idx].reg_idx, found_idx,
                    register_map[i].reg_idx, i);
                jit_dump_regmap("CONFLICT");
                assert(false);
            }
            found_idx = i;
            /* Verify the found mapping is correct */
            if (register_map[i].reg_idx != host_reg) {
                rv_log_error(
                    "JIT RegMap CONFLICT in %s: VM x%d expected at "
                    "Host R%d but found at R%d",
                    insn, vm_reg, host_reg, register_map[i].reg_idx);
                jit_dump_regmap("CONFLICT");
                assert(false);
            }
        } else if (register_map[i].reg_idx == host_reg &&
                   register_map[i].vm_reg_idx >= 0) {
            /* Host register holds different VM register */
            rv_log_error(
                "JIT RegMap CONFLICT in %s: Host R%d already holds "
                "VM x%d, cannot map VM x%d",
                insn, host_reg, register_map[i].vm_reg_idx, vm_reg);
            jit_dump_regmap("CONFLICT");
            assert(false);
        }
    }
}

static void jit_verify_cache_coherency(struct jit_state *state, uint32_t pc)
    UNUSED;
static void jit_verify_cache_coherency(struct jit_state *state, uint32_t pc)
{
    /* On ARM64, verify instruction cache was properly invalidated */
#if defined(__aarch64__)
    if (state->offset > 0) {
        rv_log_debug("JIT: Cache coherency check at PC=0x%08x, offset=%u", pc,
                     state->offset);
    }
#endif
}
#else
#define jit_dump_regmap(ctx) \
    do {                     \
    } while (0)
#define jit_check_regmap_conflict(vm_reg, host_reg, insn) \
    do {                                                  \
    } while (0)
#define jit_verify_cache_coherency(state, pc) \
    do {                                      \
    } while (0)
#endif

static bool should_flush = false;
static void emit_bytes(struct jit_state *state, void *data, uint32_t len)
{
    if (unlikely((state->offset + len) > state->size)) {
        should_flush = true;
        return;
    }
    if (unlikely(state->n_blocks == MAX_BLOCKS)) {
        should_flush = true;
        return;
    }
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(false);
#endif
    memcpy(state->buf + state->offset, data, len);
    sys_icache_invalidate(state->buf + state->offset, len);
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(true);
#endif
    state->offset += len;
}

#if defined(__x86_64__)
static inline void emit1(struct jit_state *state, uint8_t x)
{
    emit_bytes(state, &x, sizeof(x));
}

static inline void emit4(struct jit_state *state, uint32_t x)
{
    emit_bytes(state, &x, sizeof(x));
}

static inline void emit8(struct jit_state *state, uint64_t x)
{
    emit_bytes(state, &x, sizeof(x));
}

static inline void emit_modrm(struct jit_state *state, int mod, int r, int m)
{
    assert(!(mod & ~0xc0));
    emit1(state, (mod & 0xc0) | ((r & 7) << 3) | (m & 7));
}

static inline void emit_modrm_reg2reg(struct jit_state *state, int r, int m)
{
    emit_modrm(state, 0xc0, r, m);
}

static inline void emit_modrm_and_displacement(struct jit_state *state,
                                               int r,
                                               int m,
                                               int32_t d)
{
    if (d == 0 && (m & 7) != RBP) {
        emit_modrm(state, 0x00, r, m);
    } else if ((int8_t) d == d) {
        emit_modrm(state, 0x40, r, m);
        emit1(state, d);
    } else {
        emit_modrm(state, 0x80, r, m);
        emit4(state, d);
    }
}

static inline void emit_rex(struct jit_state *state, int w, int r, int x, int b)
{
    assert(!(w & ~1));
    assert(!(r & ~1));
    assert(!(x & ~1));
    assert(!(b & ~1));
    emit1(state, 0x40 | (w << 3) | (r << 2) | (x << 1) | b);
}

/* Emit a REX prefix incorporating the top bit of both src and dst. This step is
 * skipped if no bits are set.
 */
static inline void emit_basic_rex(struct jit_state *state,
                                  int w,
                                  int src,
                                  int dst)
{
    if (w || (src & 8) || (dst & 8))
        emit_rex(state, w, !!(src & 8), 0, !!(dst & 8));
}

static inline void emit_push(struct jit_state *state, int r)
{
    if (r & 8)
        emit_basic_rex(state, 0, 0, r);
    emit1(state, 0x50 | (r & 7));
}

static inline void emit_pop(struct jit_state *state, int r)
{
    if (r & 8)
        emit_basic_rex(state, 0, 0, r);
    emit1(state, 0x58 | (r & 7));
}

static inline void emit_jump_target_address(struct jit_state *state,
                                            int32_t target_pc,
                                            uint32_t target_satp UNUSED)
{
    assert(state->n_jumps < MAX_JUMPS);

    struct jump *jump = &state->jumps[state->n_jumps++];
    jump->offset_loc = state->offset;
    jump->target_pc = target_pc;
#if RV32_HAS(SYSTEM)
    jump->target_satp = target_satp;
#endif
    emit4(state, 0);
}
#elif defined(__aarch64__)
static inline void emit_load_imm(struct jit_state *state,
                                 int dst,
                                 uint32_t imm);

static void emit_a64(struct jit_state *state, uint32_t insn)
{
    assert(insn != BAD_OPCODE);
    emit_bytes(state, &insn, 4);
}

/* Get the value of the size bit in most instruction encodings (bit 31). */
static inline uint32_t sz(bool is64)
{
    return (is64 ? UINT32_C(1) : UINT32_C(0)) << 31;
}

/* For details on Arm instructions, users can refer to
 * https://developer.arm.com/documentation/ddi0487/ha (Arm Architecture
 * Reference Manual for A-profile architecture).
 */

/* [ARM-A]: C4.1.64: Add/subtract (immediate).  */
static void emit_addsub_imm(struct jit_state *state,
                            bool is64,
                            a64opcode_t op,
                            int rd,
                            int rn,
                            uint32_t imm12)
{
    const uint32_t imm_op_base = 0x11000000;
    emit_a64(state, sz(is64) | (op << 29) | imm_op_base | (0 << 22) |
                        (imm12 << 10) | (rn << 5) | rd);
    set_dirty(rd, true);
}

/* [ARM-A]: C4.1.67: Logical (shifted register).  */
static void emit_logical_register(struct jit_state *state,
                                  bool is64,
                                  a64opcode_t op,
                                  int rd,
                                  int rn,
                                  int rm)
{
    emit_a64(state, sz(is64) | op | (1 << 27) | (1 << 25) | (rm << 16) |
                        (rn << 5) | rd);
    set_dirty(rd, true);
}

/* [ARM-A]: C4.1.67: Add/subtract (shifted register).  */
static inline void emit_addsub_register(struct jit_state *state,
                                        bool is64,
                                        a64opcode_t op,
                                        int rd,
                                        int rn,
                                        int rm)
{
    const uint32_t reg_op_base = 0x0b000000;
    emit_a64(state,
             sz(is64) | (op << 29) | reg_op_base | (rm << 16) | (rn << 5) | rd);
    set_dirty(rd, true);
}

/* [ARM-A]: C4.1.64: Move wide (Immediate).  */
static inline void emit_movewide_imm(struct jit_state *state,
                                     bool is64,
                                     int rd,
                                     uint64_t imm)
{
    /* Emit a MOVZ or MOVN followed by a sequence of MOVKs to generate the
     * 64-bit constant in imm. See whether the 0x0000 or 0xffff pattern is more
     * common in the immediate.  This ensures we produce the fewest number of
     * immediates.
     */
    unsigned count0000 = is64 ? 0 : 2;
    unsigned countffff = 0;
    for (unsigned i = 0; i < (is64 ? 64 : 32); i += 16) {
        uint64_t block = (imm >> i) & 0xffff;
        if (block == 0xffff) {
            ++countffff;
        } else if (block == 0) {
            ++count0000;
        }
    }

    /* Iterate over 16-bit elements of imm, outputting an appropriate move
     * instruction.
     */
    bool invert = (count0000 < countffff);
    a64opcode_t op = invert ? MW_MOVN : MW_MOVZ;
    uint64_t skip_pattern = invert ? 0xffff : 0;
    for (unsigned i = 0; i < (is64 ? 4 : 2); ++i) {
        uint64_t imm16 = (imm >> (i * 16)) & 0xffff;
        if (imm16 != skip_pattern) {
            if (invert) {
                imm16 = ~imm16;
                imm16 &= 0xffff;
            }
            emit_a64(state, sz(is64) | op | (i << 21) | (imm16 << 5) | rd);
            op = MW_MOVK;
            invert = false;
        }
    }

    /* Tidy up for the case imm = 0 or imm == -1.  */
    if (op != MW_MOVK)
        emit_a64(state, sz(is64) | op | (0 << 21) | (0 << 5) | rd);

    set_dirty(rd, true);
}

/* [ARM-A]: C4.1.66: Load/store register (unscaled immediate).  */
static void emit_loadstore_imm(struct jit_state *state,
                               a64opcode_t op,
                               int rt,
                               int rn,
                               int16_t imm9)
{
    const uint32_t imm_op_base = 0x38000000U;
    assert(imm9 >= -256 && imm9 < 256);
    imm9 &= 0x1ff;
    emit_a64(state, imm_op_base | op | (imm9 << 12) | (rn << 5) | rt);
}

/* [ARM-A]: C4.1.66: Load/store register pair (offset).  */
static void emit_loadstorepair_imm(struct jit_state *state,
                                   a64opcode_t op,
                                   int rt,
                                   int rt2,
                                   int rn,
                                   int32_t imm7)
{
    int32_t imm_div = ((op == LSP_STPX) || (op == LSP_LDPX)) ? 8 : 4;
    assert(imm7 % imm_div == 0);
    imm7 /= imm_div;
    emit_a64(state, op | (imm7 << 15) | (rt2 << 10) | (rn << 5) | rt);
}

/* [ARM-A]: C4.1.65: Unconditional branch (register).  */
static void emit_uncond_branch_reg(struct jit_state *state,
                                   a64opcode_t op,
                                   int rn)
{
    emit_a64(state, op | (rn << 5));
}

/* [ARM-A]: C4.1.67: Data-processing (2 source).  */
static void emit_dataproc_2source(struct jit_state *state,
                                  bool is64,
                                  a64opcode_t op,
                                  int rd,
                                  int rn,
                                  int rm)
{
    emit_a64(state, sz(is64) | op | (rm << 16) | (rn << 5) | rd);
    set_dirty(rd, true);
}


#if RV32_HAS(EXT_M)
/* [ARM-A]: C4.1.67: Data-processing (3 source).  */
static void emit_dataproc_3source(struct jit_state *state,
                                  bool is64,
                                  a64opcode_t op,
                                  int rd,
                                  int rn,
                                  int rm,
                                  int ra)
{
    emit_a64(state, sz(is64) | op | (rm << 16) | (ra << 10) | (rn << 5) | rd);
    set_dirty(rd, true);
}
#endif

static void update_branch_imm(struct jit_state *state,
                              uint32_t offset,
                              int32_t imm)
{
    assert((imm & 3) == 0);
    uint32_t insn;
    imm >>= 2;
    rv_log_debug("JIT: Patching branch at offset=%u, imm=%d", offset, imm * 4);
    /* Read instruction while in execute mode (MAP_JIT requirement) */
    memcpy(&insn, state->buf + offset, sizeof(uint32_t));
    if ((insn & 0xfe000000U) == 0x54000000U /* Conditional branch immediate. */
        || (insn & 0x7e000000U) ==
               0x34000000U) { /* Compare and branch immediate. */
        assert((imm >> 19) == INT64_C(-1) || (imm >> 19) == 0);
        insn &= ~(0x7ffffU << 5); /* Clear old offset bits */
        insn |= (imm & 0x7ffff) << 5;
    } else if ((insn & 0x7c000000U) == 0x14000000U) {
        /* Unconditional branch immediate.  */
        assert((imm >> 26) == INT64_C(-1) || (imm >> 26) == 0);
        insn &= ~0x03ffffffU; /* Clear old offset bits */
        insn |= (imm & 0x03ffffffU) << 0;
    } else {
        assert(false);
        insn = BAD_OPCODE;
    }
#if defined(__APPLE__) && defined(__aarch64__)
    /* Switch to write mode only for writing */
    pthread_jit_write_protect_np(false);
#endif
    memcpy(state->buf + offset, &insn, sizeof(uint32_t));
    sys_icache_invalidate(state->buf + offset, sizeof(uint32_t));
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(true);
#endif
}
#endif

static inline void emit_jump_target_offset(struct jit_state *state,
                                           uint32_t jump_loc_0,
                                           uint32_t jump_state_offset)
{
    assert(state->n_jumps < MAX_JUMPS);

    struct jump *jump = &state->jumps[state->n_jumps++];
    jump->offset_loc = jump_loc_0;
    jump->target_offset = jump_state_offset;
}

static inline void emit_alu32(struct jit_state *state, int op, int src, int dst)
{
#if defined(__x86_64__)
    /* The REX prefix and ModRM byte are emitted.
     * The MR encoding is utilized when a choice is available. The 'src' is
     * often used as an opcode extension.
     */
    if (src & 8 || dst & 8)
        emit_basic_rex(state, 0, src, dst);
    emit1(state, op);
    emit_modrm_reg2reg(state, src, dst);

    set_dirty(dst, true);
#elif defined(__aarch64__)
    switch (op) {
    case 1: /* ADD */
        emit_addsub_register(state, false, AS_ADD, dst, dst, src);
        break;
    case 0x29: /* SUB */
        emit_addsub_register(state, false, AS_SUB, dst, dst, src);
        break;
    case 0x31: /* XOR */
        emit_logical_register(state, false, LOG_EOR, dst, dst, src);
        break;
    case 9: /* OR */
        emit_logical_register(state, false, LOG_ORR, dst, dst, src);
        break;
    case 0x21: /* AND */
        emit_logical_register(state, false, LOG_AND, dst, dst, src);
        break;
    case 0xd3:
        if (src == 4) /* SLL */
            emit_dataproc_2source(state, false, DP2_LSLV, dst, dst, temp_reg);
        else if (src == 5) /* SRL */
            emit_dataproc_2source(state, false, DP2_LSRV, dst, dst, temp_reg);
        else if (src == 7) /* SRA */
            emit_dataproc_2source(state, false, DP2_ASRV, dst, dst, temp_reg);
        break;
    default:
        __UNREACHABLE;
        break;
    }
#endif
}

static inline void emit_alu32_imm32(struct jit_state *state,
                                    int op UNUSED,
                                    int src,
                                    int dst,
                                    int32_t imm)
{
#if defined(__x86_64__)
    /* REX prefix, ModRM byte, and 32-bit immediate */
    emit_alu32(state, op, src, dst);
    emit4(state, imm);
#elif defined(__aarch64__)
    switch (src) {
    case 0:
        emit_load_imm(state, R10, imm);
        emit_addsub_register(state, false, AS_ADD, dst, dst, R10);
        break;
    case 1:
        emit_load_imm(state, R10, imm);
        emit_logical_register(state, false, LOG_ORR, dst, dst, R10);
        break;
    case 4:
        emit_load_imm(state, R10, imm);
        emit_logical_register(state, false, LOG_AND, dst, dst, R10);
        break;
    case 6:
        emit_load_imm(state, R10, imm);
        emit_logical_register(state, false, LOG_EOR, dst, dst, R10);
        break;
    default:
        __UNREACHABLE;
        break;
    }
#endif
}

static inline void emit_alu32_imm8(struct jit_state *state,
                                   int op UNUSED,
                                   int src,
                                   int dst,
                                   int8_t imm)
{
#if defined(__x86_64__)
    /* REX prefix, ModRM byte, and 8-bit immediate */
    emit_alu32(state, op, src, dst);
    emit1(state, imm);
#elif defined(__aarch64__)
    switch (src) {
    case 4:
        emit_load_imm(state, R10, imm);
        emit_dataproc_2source(state, false, DP2_LSLV, dst, dst, R10);
        break;
    case 5:
        emit_load_imm(state, R10, imm);
        emit_dataproc_2source(state, false, DP2_LSRV, dst, dst, R10);
        break;
    case 7:
        emit_load_imm(state, R10, imm);
        emit_dataproc_2source(state, false, DP2_ASRV, dst, dst, R10);
        break;
    default:
        __UNREACHABLE;
        break;
    }
#endif
}

static inline void emit_alu64(struct jit_state *state, int op, int src, int dst)
{
#if defined(__x86_64__)
    /* The REX.W prefix and ModRM byte are emitted.
     * The MR encoding is used when there is a choice. 'src' is often used as
     * an opcode extension.
     */
    emit_basic_rex(state, 1, src, dst);
    emit1(state, op);
    emit_modrm_reg2reg(state, src, dst);

    set_dirty(dst, true);
#elif defined(__aarch64__)
    if (op == 0x01)
        emit_addsub_register(state, true, AS_ADD, dst, dst, src);
#endif
}

#if RV32_HAS(EXT_M) || defined(__aarch64__)
static inline void emit_alu64_imm8(struct jit_state *state,
                                   int op,
                                   int src UNUSED,
                                   int dst,
                                   int8_t imm)
{
#if defined(__x86_64__)
    /* REX.W prefix, ModRM byte, and 8-bit immediate */
    emit_alu64(state, op, src, dst);
    emit1(state, imm);
#elif defined(__aarch64__)
    if (op == 0xc1) {
        emit_load_imm(state, R10, imm);
        emit_dataproc_2source(state, true, DP2_LSRV, dst, dst, R10);
    } else if (src == 0) {
        emit_load_imm(state, R10, imm);
        emit_addsub_register(state, true, AS_ADD, dst, dst, R10);
    }
#endif
}
#endif

/* Register to register mov */
static inline void emit_mov(struct jit_state *state, int src, int dst)
{
#if defined(__x86_64__)
    emit_alu64(state, 0x89, src, dst);
#elif defined(__aarch64__)
    emit_load_imm(state, R10, 0);
    emit_addsub_register(state, false, AS_ADD, dst, src, R10);
#endif
}


#if defined(__x86_64__)
/* REX.W prefix, ModRM byte, and 32-bit immediate */
static inline void emit_alu64_imm32(struct jit_state *state,
                                    int op,
                                    int src,
                                    int dst,
                                    int32_t imm)
{
    emit_alu64(state, op, src, dst);
    emit4(state, imm);
}
#endif

static inline void emit_cmp_imm32(struct jit_state *state, int dst, int32_t imm)
{
#if defined(__x86_64__)
    emit_alu32_imm32(state, 0x81, 7, dst, imm);
#elif defined(__aarch64__)
    emit_load_imm(state, R10, imm);
    emit_addsub_register(state, false, AS_SUBS, RZ, dst, R10);
#endif
}

static inline void emit_cmp32(struct jit_state *state, int src, int dst)
{
#if defined(__x86_64__)
    emit_alu32(state, 0x39, src, dst);
#elif defined(__aarch64__)
    emit_addsub_register(state, false, AS_SUBS, RZ, dst, src);
#endif
}

static inline void emit_jcc_offset(struct jit_state *state, int code)
{
#if defined(__x86_64__)
    /* unconditional jump instruction does not have 0x0f prefix */
    if (code != 0xe9)
        emit1(state, 0x0f);
    emit1(state, code);
    emit4(state, 0);
#elif defined(__aarch64__)
    switch (code) {
    case 0x84: /* BEQ */
        code = COND_EQ;
        break;
    case 0x85: /* BNE */
        code = COND_NE;
        break;
    case 0x8c: /* BLT */
        code = COND_LT;
        break;
    case 0x8d: /* BGE */
        code = COND_GE;
        break;
    case 0x82: /* BLTU */
        code = COND_LO;
        break;
    case 0x83: /* BGEU */
        code = COND_HS;
        break;
    case 0xe9: /* AL */
        code = COND_AL;
        break;
    default:
        assert(NULL);
        __UNREACHABLE;
    }
    emit_a64(state, BR_Bcond | (0 << 5) | code);
#endif
}

static inline void emit_load_imm(struct jit_state *state,
                                 int dst,
                                 uint32_t imm);

/* Load [src + offset] into dst.
 *
 * If the offset is non-zero, it restores the vm register to the host register
 * from the stack. Otherwise, it is a `read` pseudo instruction that loading
 * the [src] into destination register.
 */
static inline void emit_load(struct jit_state *state,
                             enum operand_size size,
                             int src,
                             int dst,
                             int32_t offset)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].reg_idx != dst)
            continue;
        if (register_map[i].vm_reg_idx != 0)
            continue;

        /* if dst is x0, load 0x0 into host register */
        emit_load_imm(state, dst, 0x0);
        set_dirty(dst, true);
        return;
    }

#if defined(__x86_64__)
    if (src & 8 || dst & 8)
        emit_basic_rex(state, 0, dst, src);
    if (size == S8 || size == S16) {
        /* movzx */
        emit1(state, 0x0f);
        emit1(state, size == S8 ? 0xb6 : 0xb7);
    } else if (size == S32) {
        /* mov */
        emit1(state, 0x8b);
    } else {
        assert(NULL);
        __UNREACHABLE;
    }

    emit_modrm_and_displacement(state, dst, src, offset);
#elif defined(__aarch64__)
    switch (size) {
    case S8:
        emit_loadstore_imm(state, LS_LDRB, dst, src, offset);
        break;
    case S16:
        emit_loadstore_imm(state, LS_LDRH, dst, src, offset);
        break;
    case S32:
        emit_loadstore_imm(state, LS_LDRW, dst, src, offset);
        break;
    case S64:
        emit_loadstore_imm(state, LS_LDRX, dst, src, offset);
        break;
    default:
        assert(NULL);
        __UNREACHABLE;
    }
#endif

    set_dirty(dst, !offset);
}

static inline void emit_load_sext(struct jit_state *state,
                                  enum operand_size size,
                                  int src,
                                  int dst,
                                  int32_t offset)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].reg_idx != dst)
            continue;
        if (register_map[i].vm_reg_idx != 0)
            continue;

        /* if dst is x0, load 0x0 into host register */
        emit_load_imm(state, dst, 0x0);
        set_dirty(dst, true);
        return;
    }

#if defined(__x86_64__)
    if (size == S8 || size == S16) {
        if (src & 8 || dst & 8)
            emit_basic_rex(state, 0, dst, src);
        /* movsx */
        emit1(state, 0x0f);
        emit1(state, size == S8 ? 0xbe : 0xbf);
    } else if (size == S32) {
        emit_basic_rex(state, 1, dst, src);
        emit1(state, 0x63);
    }

    emit_modrm_and_displacement(state, dst, src, offset);
#elif defined(__aarch64__)
    switch (size) {
    case S8:
        emit_loadstore_imm(state, LS_LDRSBW, dst, src, offset);
        break;
    case S16:
        emit_loadstore_imm(state, LS_LDRSHW, dst, src, offset);
        break;
    case S32:
        emit_loadstore_imm(state, LS_LDRSW, dst, src, offset);
        break;
    default:
        __UNREACHABLE;
        break;
    }
#endif

    set_dirty(dst, !offset);
}

/* Load 32-bit immediate into register (zero-extend) */
static inline void emit_load_imm(struct jit_state *state, int dst, uint32_t imm)
{
#if defined(__x86_64__)
    if (dst & 8)
        emit_basic_rex(state, 0, 0, dst);
    emit1(state, 0xb8 | (dst & 7));
    emit4(state, imm);

    set_dirty(dst, true);
#elif defined(__aarch64__)
    emit_movewide_imm(state, true, dst, imm);
#endif
}

/* Load sign-extended immediate into register */
static inline void emit_load_imm_sext(struct jit_state *state,
                                      int dst,
                                      int64_t imm)
{
#if defined(__x86_64__)
    if ((int32_t) imm == imm)
        emit_alu64_imm32(state, 0xc7, 0, dst, imm);
    else {
        /* movabs $imm, dst */
        emit_basic_rex(state, 1, 0, dst);
        emit1(state, 0xb8 | (dst & 7));
        emit8(state, imm);
    }

    set_dirty(dst, true);
#elif defined(__aarch64__)
    if ((int32_t) imm == imm)
        emit_movewide_imm(state, false, dst, imm);
    else
        emit_movewide_imm(state, true, dst, imm);
#endif
}

static inline bool jit_store_x0(struct jit_state *state,
                                enum operand_size size,
                                int src,
                                int dst,
                                int32_t offset)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].reg_idx != src)
            continue;
        if (register_map[i].vm_reg_idx != 0)
            continue;

#if defined(__x86_64__)
        /* if src is x0, write 0x0 into destination */
        if (size == S16)
            emit1(state, 0x66); /* 16-bit override */
        if (dst & 8)
            emit_rex(state, 0, 0, 0, !!(dst & 8));
        emit1(state, size == S8 ? 0xc6 : 0xc7);
        emit1(state, 0x80 | (dst & 0x7));
        emit4(state, offset);
        switch (size) {
        case S8:
            emit1(state, 0x0);
            break;
        case S16:
            emit1(state, 0x0);
            emit1(state, 0x0);
            break;
        case S32:
            emit4(state, 0x0);
            break;
        default:
            assert(NULL);
            __UNREACHABLE;
        }
#elif defined(__aarch64__)
        switch (size) {
        case S8:
            emit_loadstore_imm(state, LS_STRB, RZ, dst, offset);
            break;
        case S16:
            emit_loadstore_imm(state, LS_STRH, RZ, dst, offset);
            break;
        case S32:
            emit_loadstore_imm(state, LS_STRW, RZ, dst, offset);
            break;
        default:
            assert(NULL);
            __UNREACHABLE;
        }
#endif
        set_dirty(src, false);
        return true;
    }
    return false;
}

/* Store register src to [dst + offset].
 *
 * If the offset is non-zero, it stores the host register back to the stack
 * which mapped to the vm register file. Otherwise, it is a `write` pseudo
 * instruction that writing the content of `src` into [dst].
 */
static inline void emit_store(struct jit_state *state,
                              enum operand_size size,
                              int src,
                              int dst,
                              int32_t offset)
{
    if (jit_store_x0(state, size, src, dst, offset))
        return;

#if defined(__x86_64__)
    if (size == S16)
        emit1(state, 0x66); /* 16-bit override */
    if (src & 8 || dst & 8 || size == S8)
        emit_rex(state, 0, !!(src & 8), 0, !!(dst & 8));
    emit1(state, size == S8 ? 0x88 : 0x89);
    emit_modrm_and_displacement(state, src, dst, offset);
#elif defined(__aarch64__)
    switch (size) {
    case S8:
        emit_loadstore_imm(state, LS_STRB, src, dst, offset);
        break;
    case S16:
        emit_loadstore_imm(state, LS_STRH, src, dst, offset);
        break;
    case S32:
        emit_loadstore_imm(state, LS_STRW, src, dst, offset);
        break;
    case S64:
        emit_loadstore_imm(state, LS_STRX, src, dst, offset);
        break;
    default:
        assert(NULL);
        __UNREACHABLE;
    }
#endif

    if (offset)
        set_dirty(src, false);
}

static inline void emit_jmp(struct jit_state *state,
                            uint32_t target_pc,
                            uint32_t target_satp UNUSED)
{
#if defined(__x86_64__)
    emit1(state, 0xe9);
    emit_jump_target_address(state, target_pc, target_satp);
#elif defined(__aarch64__)
    assert(state->n_jumps < MAX_JUMPS);

    struct jump *jump = &state->jumps[state->n_jumps++];
    jump->offset_loc = state->offset;
    jump->target_pc = target_pc;
    emit_a64(state, UBR_B);
#if RV32_HAS(SYSTEM)
    jump->target_satp = target_satp;
#endif
#endif
}

static inline void save_reg(struct jit_state *, int);
static inline void unmap_vm_reg(int);

static inline void emit_call(struct jit_state *state, intptr_t target)
{
#if defined(__x86_64__)
    emit_load_imm_sext(state, RAX, target);
    /* callq *%rax */
    emit1(state, 0xff);
    /* ModR/M byte: b11010000b = xd0, rax is register 0 */
    emit1(state, 0xd0);
#elif defined(__aarch64__)
    uint32_t stack_movement = align_up(8, 16);
    emit_addsub_imm(state, true, AS_SUB, SP, SP, stack_movement);
    emit_loadstore_imm(state, LS_STRX, R30, SP, 0);

    emit_movewide_imm(state, true, temp_imm_reg, target);
    emit_uncond_branch_reg(state, BR_BLR, temp_imm_reg);

    save_reg(state, 0); /* R5 */
    unmap_vm_reg(0);    /* R5 */
    emit_logical_register(state, true, LOG_ORR, R5, RZ, R0);

    emit_loadstore_imm(state, LS_LDRX, R30, SP, 0);
    emit_addsub_imm(state, true, AS_ADD, SP, SP, stack_movement);
#endif
}

static inline void emit_exit(struct jit_state *state)
{
#if defined(__x86_64__)
    emit1(state, 0xe9);
    emit_jump_target_offset(state, state->offset, state->exit_loc);
    emit4(state, 0);
#elif defined(__aarch64__)
    emit_jmp(state, TARGET_PC_EXIT, 0);
#endif
}

#if RV32_HAS(EXT_M)
#if defined(__x86_64__)
static inline void emit_conditional_move(struct jit_state *state,
                                         int src,
                                         int dst)
{
    emit1(state, 0x48);
    emit1(state, 0x0f);
    emit1(state, 0x44);
    emit_modrm_reg2reg(state, dst, src);
}
#elif defined(__aarch64__)
static inline void emit_conditional_move(struct jit_state *state,
                                         int rd,
                                         int rn,
                                         int rm,
                                         int cond)
{
    emit_a64(state, 0x1a800000 | (rm << 16) | (cond << 12) | (rn << 5) | rd);
    set_dirty(rd, true);
}

static void divmod(struct jit_state *state,
                   uint8_t opcode,
                   int rd,
                   int rn,
                   int rm,
                   bool sign)
{
    bool mod = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_MOD_IMM & JIT_ALU_OP_MASK);
    bool is64 = (opcode & JIT_CLS_MASK) == JIT_CLS_ALU64;
    int div_dest = mod ? temp_div_reg : rd;

    if (sign)
        emit_cmp_imm32(state, rd, 0x80000000); /* overflow checking */

    emit_dataproc_2source(state, is64, DP2_UDIV, div_dest, rn, rm);
    if (mod)
        emit_dataproc_3source(state, is64, DP3_MSUB, rd, rm, div_dest, rn);

    if (sign) {
        /* handle overflow */
        uint32_t jump_loc_0 = state->offset;
        emit_jcc_offset(state, 0x85);
        emit_cmp_imm32(state, rm, -1);
        if (mod)
            emit_load_imm(state, R10, 0);
        else
            emit_load_imm(state, R10, 0x80000000);
        emit_conditional_move(state, rd, R10, rd, COND_EQ);
        emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    }
    if (!mod) {
        /* handle dividing zero */
        emit_cmp_imm32(state, rm, 0);
        emit_load_imm(state, temp_reg, -1);
        emit_conditional_move(state, rd, temp_reg, rd, COND_EQ);
    }
}
#endif

static void muldivmod(struct jit_state *state,
                      uint8_t opcode,
                      int src,
                      int dst,
                      bool sign)
{
#if defined(__x86_64__)
    bool mul = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_MUL_IMM & JIT_ALU_OP_MASK);
    bool div = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_DIV_IMM & JIT_ALU_OP_MASK);
    bool mod = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_MOD_IMM & JIT_ALU_OP_MASK);
    bool is64 = (opcode & JIT_CLS_MASK) == JIT_CLS_ALU64;

    /* Record the mapping status before the registers are used for other
     * purposes, and restore the status after popping the registers.
     */
    int d1 = register_map[0].dirty, d2 = register_map[2].dirty;
    int r1 = register_map[0].vm_reg_idx, r2 = register_map[2].vm_reg_idx;

    if (dst != RAX) {
        unmap_vm_reg(0); /* RAX */
        emit_push(state, RAX);
    }

    if (dst != RDX) {
        unmap_vm_reg(2); /* RDX */
        emit_push(state, RDX);
    }

    /*  Load the divisor into RCX */
    emit_mov(state, src, RCX);

    /* Load the dividend into RAX */
    emit_mov(state, dst, RAX);

    /* The JIT employs two different semantics for division and modulus
     * operations. In the case of division, if the divisor is zero, the result
     * is set to -1. For modulus operations, if the divisor is zero, the
     * result becomes the dividend. To manage this, we first set the divisor to
     * 1 if it is initially zero. Then, we adjust the result accordingly: for
     * division, we set it to -1 if the original divisor was zero; for
     * modulus, we set it to the dividend under the same condition.
     */

    if (div || mod) {
        if (sign) {
            emit_load_imm_sext(state, RDX, -1);
            /* compare divisor with -1 for overflow checking */
            emit_cmp32(state, RDX, RCX);
            /* Save the result of the comparision */
            emit1(state, 0x9c); /* pushfq */
        }
        if (mod || (div && sign))
            emit_push(state, RAX); /* Save dividend */

        emit_alu32(state, 0x85, RCX, RCX);
        /* Save the result of the test */
        emit1(state, 0x9c); /* pushfq */

        /* Set the divisor to 1 if it is zero */
        emit_load_imm(state, RDX, 1);
        emit_conditional_move(state, RDX, RCX);
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
            emit_load_imm_sext(state, RCX, -1);

            /* Store 0 in RAX if the divisor was zero. */
            /* Use conditional move to avoid a branch. */
            emit_conditional_move(state, RCX, RAX);
            if (sign) {
                emit_pop(state, RCX);
                /* handle DIV overflow */
                emit1(state, 0x9d); /* popfq */
                uint32_t jump_loc_0 = state->offset;
                emit_jcc_offset(state, 0x85);
                emit_cmp_imm32(state, RCX, 0x80000000);
                emit_conditional_move(state, RCX, RAX);
                emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            }
        } else {
            /* Restore dividend to RCX */
            emit_pop(state, RCX);
            /* Store the dividend in RAX if the divisor was zero. */
            /* Use conditional move to avoid a branch. */
            emit_conditional_move(state, RCX, RDX);
            if (sign) {
                /* handle REM overflow */
                emit1(state, 0x9d); /* popfq */
                uint32_t jump_loc_0 = state->offset;
                emit_jcc_offset(state, 0x85);
                emit_cmp_imm32(state, RCX, 0x80000000);
                emit_load_imm(state, RCX, 0);
                emit_conditional_move(state, RCX, RDX);
                emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
            }
        }
    }

    if (dst != RDX) {
        if (mod)
            emit_mov(state, RDX, dst);
        emit_pop(state, RDX);
        register_map[2].vm_reg_idx = r2;
        register_map[2].dirty = d2;
    }
    if (dst != RAX) {
        if (div || mul)
            emit_mov(state, RAX, dst);
        emit_pop(state, RAX);
        register_map[0].vm_reg_idx = r1;
        register_map[0].dirty = d1;
    }
#elif defined(__aarch64__)
    switch (opcode) {
    case 0x28:
        emit_dataproc_3source(state, false, DP3_MADD, dst, dst, src, RZ);
        break;
    case 0x2f:
        emit_dataproc_3source(state, true, DP3_MADD, dst, dst, src, RZ);
        break;
    case 0x38:
        divmod(state, JIT_OP_DIV_REG, dst, dst, src, sign);
        break;
    case 0x98:
        divmod(state, JIT_OP_MOD_REG, dst, dst, src, sign);
        break;
    default:
        __UNREACHABLE;
        break;
    }
#endif
}
#endif /* RV32_HAS(EXT_M) */

#if RV32_HAS(SYSTEM)
uint32_t jit_mmio_read_wrapper(riscv_t *rv, uint32_t addr)
{
    MMIO_READ();
    __UNREACHABLE;
}

void jit_mmu_handler(riscv_t *rv, uint32_t vreg_idx)
{
    assert(vreg_idx < 32);

    uint32_t addr;

    if (rv->jit_mmu.type == rv_insn_lb || rv->jit_mmu.type == rv_insn_lh ||
        rv->jit_mmu.type == rv_insn_lbu || rv->jit_mmu.type == rv_insn_lhu ||
        rv->jit_mmu.type == rv_insn_lw)
        addr = rv->io.mem_translate(rv, rv->jit_mmu.vaddr, R);
    else
        addr = rv->io.mem_translate(rv, rv->jit_mmu.vaddr, W);

    if (addr == rv->jit_mmu.vaddr || addr < PRIV(rv)->mem->mem_size) {
        rv->jit_mmu.is_mmio = 0;
        rv->jit_mmu.paddr = addr;
        return;
    }

    uint32_t val;
    rv->jit_mmu.is_mmio = 1;

    switch (rv->jit_mmu.type) {
    case rv_insn_sb:
        val = rv->X[vreg_idx] & 0xff;
        MMIO_WRITE();
        break;
    case rv_insn_sh:
        val = rv->X[vreg_idx] & 0xffff;
        MMIO_WRITE();
        break;
    case rv_insn_sw:
        val = rv->X[vreg_idx];
        MMIO_WRITE();
        break;
    case rv_insn_lb:
        rv->X[vreg_idx] = (int8_t) jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lh:
        rv->X[vreg_idx] = (int16_t) jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lw:
        rv->X[vreg_idx] = jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lbu:
        rv->X[vreg_idx] = (uint8_t) jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lhu:
        rv->X[vreg_idx] = (uint16_t) jit_mmio_read_wrapper(rv, addr);
        break;
    default:
        assert(NULL);
        __UNREACHABLE;
    }
}

void emit_jit_mmu_handler(struct jit_state *state, uint8_t vreg_idx)
{
    assert(vreg_idx < 32);

#if defined(__x86_64__)
    /* push $rdi */
    emit1(state, 0xff);
    emit_modrm(state, 0x3 << 6, 0x6, parameter_reg[0]);

    /* mov $vreg_idx, %rsi */
    emit1(state, 0xbe);
    emit4(state, vreg_idx);

    /* call jit_mmu_handler */
    emit_load_imm_sext(state, temp_reg, (uintptr_t) &jit_mmu_handler);
    emit1(state, 0xff);
    emit_modrm(state, 0x3 << 6, 0x2, temp_reg);

    /* pop rv to $rdi */
    emit1(state, 0x8f);
    emit_modrm(state, 0x3 << 6, 0x0, parameter_reg[0]);
#elif defined(__aarch64__)
    uint32_t insn;

    /* push rv into stack */
    insn = (0xf81f0fe << 4) | R0;
    emit_a64(state, insn);

    /* move vreg_idx into R1 */
    emit_movewide_imm(state, false, R1, vreg_idx);

    /* load &jit_mmu_handler */
    emit_movewide_imm(state, true, temp_reg, (uintptr_t) &jit_mmu_handler);
    /* blr jit_mmu_handler */
    insn = (0xd63f << 16) | (temp_reg << 5);
    emit_a64(state, insn);

    /* pop from stack */
    insn = (0xf84107e << 4) | R0;
    emit_a64(state, insn);
#endif
}
#endif

static void prepare_translate(struct jit_state *state)
{
#if defined(__x86_64__)
    /* Save platform non-volatile registers */
    for (uint32_t i = 0; i < ARRAY_SIZE(nonvolatile_reg); i++)
        emit_push(state, nonvolatile_reg[i]);

    /* Assuming that the stack is 16-byte aligned just before the call
     * instruction that brought us to this code, we need to restore 16-byte
     * alignment upon starting execution of the JIT'd code. STACK_SIZE is
     * guaranteed to be divisible by 16. However, if an even number of
     * registers were pushed onto the stack during state saving (see above),
     * an additional 8 bytes must be added to regain 16-byte alignment.
     */
    if (!(ARRAY_SIZE(nonvolatile_reg) % 2))
        emit_alu64_imm32(state, 0x81, 5, RSP, 0x8);

    /* Set JIT R10 (the way to access the frame in JIT) to match RSP. */
    emit_mov(state, RSP, RBP);

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

    /* Deallocate stack space by restoring RSP from JIT R10. */
    emit_mov(state, RBP, RSP);

    if (!(ARRAY_SIZE(nonvolatile_reg) % 2))
        emit_alu64_imm32(state, 0x81, 0, RSP, 0x8);

    /* Restore platform non-volatile registers */
    for (uint32_t i = 0; i < ARRAY_SIZE(nonvolatile_reg); i++)
        emit_pop(state, nonvolatile_reg[ARRAY_SIZE(nonvolatile_reg) - i - 1]);

    /* Return */
    emit1(state, 0xc3);
#elif defined(__aarch64__)
    uint32_t register_space = ARRAY_SIZE(callee_reg) * 8 + 2 * 8;
    state->stack_size = align_up(STACK_SIZE + register_space, 16);
    emit_addsub_imm(state, true, AS_SUB, SP, SP, state->stack_size);

    /* Set up frame */
    emit_loadstorepair_imm(state, LSP_STPX, R29, R30, SP, 0);
    /* In ARM64 calling convention, R29 is the frame pointer. */
    emit_addsub_imm(state, true, AS_ADD, R29, SP, 0);

    /* Save callee saved registers */
    for (size_t i = 0; i < ARRAY_SIZE(callee_reg); i += 2) {
        emit_loadstorepair_imm(state, LSP_STPX, callee_reg[i],
                               callee_reg[i + 1], SP, (i + 2) * 8);
    }

    emit_uncond_branch_reg(state, BR_BR, R1);
    /* Epilogue */
    state->exit_loc = state->offset;

    /* Restore callee-saved registers).  */
    for (size_t i = 0; i < ARRAY_SIZE(callee_reg); i += 2) {
        emit_loadstorepair_imm(state, LSP_LDPX, callee_reg[i],
                               callee_reg[i + 1], SP, (i + 2) * 8);
    }
    emit_loadstorepair_imm(state, LSP_LDPX, R29, R30, SP, 0);
    emit_addsub_imm(state, true, AS_ADD, SP, SP, state->stack_size);
    emit_uncond_branch_reg(state, BR_RET, R30);
#endif
    state->org_size = state->offset;
}

static int liveness[N_RV_REGS];
/* The priority queue of vm registers. The one which has farthest liveness is
 * first.
 */
static uint8_t candidate_queue[N_RV_REGS];
static int vm_reg[3]; /* enum x64_reg/a64_reg */

static void reset_reg()
{
    for (int i = 0; i < n_host_regs; i++) {
        register_map[i].vm_reg_idx = -1;
        register_map[i].dirty = false;
        register_map[i].alive = false;
    }
}

/* Save host register if it is dirty. */
static inline void save_reg(struct jit_state *state, int idx)
{
    assert(idx > -1 && idx < n_host_regs);

    if (!register_map[idx].dirty)
        return;

    emit_store(state, S32, register_map[idx].reg_idx, parameter_reg[0],
               offsetof(riscv_t, X) + 4 * register_map[idx].vm_reg_idx);
    register_map[idx].dirty = 0;
}

static void store_back(struct jit_state *state)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx == -1)
            continue;
        save_reg(state, i);
    }
}

static inline void liveness_reset()
{
    memset(liveness, 0xff, sizeof(liveness));
}

static inline void candidate_queue_init()
{
    for (int i = 0; i < N_RV_REGS; i++) {
        candidate_queue[i] = i;
    }
}

static int liveness_cmp(const void *l, const void *r)
{
    return liveness[*(uint8_t *) l] - liveness[*(uint8_t *) r];
}

/* TODO: this function could be generated by "tools/gen-jit-template.py" */
static inline void liveness_calc(block_t *block)
{
    uint32_t idx;
    rv_insn_t *ir;

    /* follow the order of operator in "src/rc32_template.c" */
    for (idx = 0, ir = block->ir_head; idx < block->n_insn;
         idx++, ir = ir->next) {
        switch (ir->opcode) {
        case rv_insn_nop:
        case rv_insn_lui:
        case rv_insn_auipc:
        case rv_insn_jal:
            break;
        case rv_insn_jalr:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_beq:
        case rv_insn_bne:
        case rv_insn_blt:
        case rv_insn_bge:
        case rv_insn_bltu:
        case rv_insn_bgeu:
            liveness[ir->rs1] = idx;
            liveness[ir->rs2] = idx;
            break;
        case rv_insn_lb:
        case rv_insn_lh:
        case rv_insn_lw:
        case rv_insn_lbu:
        case rv_insn_lhu:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_sb:
        case rv_insn_sh:
        case rv_insn_sw:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_addi:
        case rv_insn_slti:
        case rv_insn_sltiu:
        case rv_insn_xori:
        case rv_insn_ori:
        case rv_insn_andi:
        case rv_insn_slli:
        case rv_insn_srli:
        case rv_insn_srai:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_add:
        case rv_insn_sub:
        case rv_insn_sll:
        case rv_insn_slt:
        case rv_insn_sltu:
        case rv_insn_xor:
        case rv_insn_srl:
        case rv_insn_sra:
        case rv_insn_or:
        case rv_insn_and:
            liveness[ir->rs1] = idx;
            liveness[ir->rs2] = idx;
            break;
        case rv_insn_ecall:
        case rv_insn_ebreak:
            break;
#if RV32_HAS(EXT_M)
        case rv_insn_mul:
        case rv_insn_mulh:
        case rv_insn_mulhsu:
        case rv_insn_mulhu:
        case rv_insn_div:
        case rv_insn_divu:
        case rv_insn_rem:
        case rv_insn_remu:
            liveness[ir->rs1] = idx;
            liveness[ir->rs2] = idx;
            break;
#endif
#if RV32_HAS(EXT_C)
        case rv_insn_caddi4spn:
            liveness[rv_reg_sp] = idx;
            break;
        case rv_insn_clw:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_csw:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_cnop:
            break;
        case rv_insn_caddi:
            liveness[ir->rd] = idx;
            break;
        case rv_insn_cjal:
        case rv_insn_cli:
        case rv_insn_clui:
            break;
        case rv_insn_caddi16sp:
            liveness[ir->rd] = idx;
            break;
        case rv_insn_csrli:
        case rv_insn_csrai:
        case rv_insn_candi:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_csub:
        case rv_insn_cxor:
        case rv_insn_cor:
        case rv_insn_cand:
            liveness[ir->rs1] = idx;
            liveness[ir->rs2] = idx;
            break;
        case rv_insn_cj:
            break;
        case rv_insn_cbeqz:
        case rv_insn_cbnez:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_cslli:
            liveness[ir->rd] = idx;
            break;
        case rv_insn_clwsp:
            liveness[rv_reg_sp] = idx;
            break;
        case rv_insn_cjr:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_cmv:
            liveness[ir->rs2] = idx;
            break;
        case rv_insn_cebreak:
            break;
        case rv_insn_cjalr:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_cadd:
            liveness[ir->rs1] = idx;
            liveness[ir->rs2] = idx;
            break;
        case rv_insn_cswsp:
            liveness[rv_reg_sp] = idx;
            liveness[ir->rs2] = idx;
            break;
#endif
        case rv_insn_fuse1:
            for (int i = 0; i < ir->imm2; i++) {
                liveness[ir->fuse[i].rd] = idx;
            }
            break;
        case rv_insn_fuse2:
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_fuse3:
            for (int i = 0; i < ir->imm2; i++) {
                liveness[ir->fuse[i].rs1] = idx;
                liveness[ir->fuse[i].rs2] = idx;
            }
            break;
        case rv_insn_fuse4:
        case rv_insn_fuse5:
            for (int i = 0; i < ir->imm2; i++) {
                liveness[ir->fuse[i].rs1] = idx;
            }
            break;
        default:
            __UNREACHABLE;
        }
    }

    candidate_queue_init();
    qsort(candidate_queue, N_RV_REGS, sizeof(uint8_t), liveness_cmp);
}

static inline void regs_refresh(int idx)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx == -1)
            continue;
        if (liveness[register_map[i].vm_reg_idx] < idx)
            register_map[i].alive = false;
    }
}

/* return the index in the register_map */
static inline int reg_pick(int reserved)
{
    /* pick an available register */
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].reg_idx == reserved)
            continue;
        if (!register_map[i].alive)
            return i;
    }

    /* If registers are exhausted, pick the one which has farthest liveness. */
    int idx = -1;
    for (int i = 0; i < N_RV_REGS; i++) {
        uint8_t candidate = candidate_queue[i];
        for (int j = 0; j < n_host_regs; j++) {
            if (register_map[j].reg_idx == reserved)
                continue;
            if (register_map[j].vm_reg_idx == candidate) {
                idx = j;
                goto end_pick_reg;
            }
        }
    }
    __UNREACHABLE;

end_pick_reg:
    assert(idx > -1 && idx < n_host_regs);
    return idx;
}

/* Unmap the vm register to the host register. */
static inline void unmap_vm_reg(int idx)
{
    /* check dirty before unmap */
    assert(idx > -1 && idx < n_host_regs);
    register_map[idx].vm_reg_idx = -1;
}

static inline void set_vm_reg(int idx, int vm_reg_idx)
{
    assert(idx > -1 && idx < n_host_regs);
    register_map[idx].vm_reg_idx = vm_reg_idx;
    register_map[idx].alive = true;
}

/* Map the vm register to a host register. If the host register file is
 * exhausted, pick a register and swap it out.
 */
static inline int map_vm_reg(struct jit_state *state, int vm_reg_idx)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx)
            continue;
        return register_map[i].reg_idx;
    }

    int idx = reg_pick(-1);
    int target_reg = register_map[idx].reg_idx;
    save_reg(state, idx);
    unmap_vm_reg(idx);
    set_vm_reg(idx, vm_reg_idx);
    jit_check_regmap_conflict(vm_reg_idx, target_reg, "map_vm_reg");
    return target_reg;
}

static int ra_load(struct jit_state *state, int vm_reg_idx)
{
    int origin = -1;
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx)
            continue;
        origin = register_map[i].reg_idx;
    }

    int target_reg = map_vm_reg(state, vm_reg_idx);

    if (origin != target_reg)
        emit_load(state, S32, parameter_reg[0], target_reg,
                  offsetof(riscv_t, X) + 4 * vm_reg_idx);
    return target_reg;
}

/* Prevent the host register collision while the first vm register has already
 * been mapped and the second one is going to be mapped to the same host
 * register and invoke swapping.
 */
static inline int map_vm_reg_reserved(struct jit_state *state,
                                      int vm_reg_idx,
                                      int reserved_reg_idx)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx)
            continue;
        return register_map[i].reg_idx;
    }

    int idx, target_reg;
    do {
        idx = reg_pick(reserved_reg_idx);
        target_reg = register_map[idx].reg_idx;
    } while (target_reg == reserved_reg_idx);

    save_reg(state, idx);
    unmap_vm_reg(idx);
    set_vm_reg(idx, vm_reg_idx);
    jit_check_regmap_conflict(vm_reg_idx, target_reg, "map_vm_reg_reserved");
    /* Additional check: ensure we didn't allocate the reserved register */
    if (target_reg == reserved_reg_idx) {
        rv_log_error(
            "JIT RegMap ERROR: map_vm_reg_reserved allocated reserved "
            "register R%d for VM x%d",
            reserved_reg_idx, vm_reg_idx);
        assert(false);
    }
    return target_reg;
}

static void ra_load2(struct jit_state *state, int vm_reg_idx1, int vm_reg_idx2)
{
    int origin1 = -1, origin2 = -1;
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx1)
            continue;
        origin1 = register_map[i].reg_idx;
    }
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx2)
            continue;
        origin2 = register_map[i].reg_idx;
    }

    if (vm_reg_idx1 == vm_reg_idx2) {
        vm_reg[0] = vm_reg[1] = map_vm_reg(state, vm_reg_idx1);
    } else {
        vm_reg[0] = map_vm_reg(state, vm_reg_idx1);
        vm_reg[1] = map_vm_reg_reserved(state, vm_reg_idx2, vm_reg[0]);
        assert(vm_reg[0] != vm_reg[1]);
    }

    if (origin1 != vm_reg[0])
        emit_load(state, S32, parameter_reg[0], vm_reg[0],
                  offsetof(riscv_t, X) + 4 * vm_reg_idx1);
    if (origin2 != vm_reg[1])
        emit_load(state, S32, parameter_reg[0], vm_reg[1],
                  offsetof(riscv_t, X) + 4 * vm_reg_idx2);
}

#if RV32_HAS(EXT_M)
static void ra_load2_sext(struct jit_state *state,
                          int vm_reg_idx1,
                          int vm_reg_idx2,
                          bool sext1,
                          bool sext2)
{
    int origin1 = -1, origin2 = -1;
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx1)
            continue;
        origin1 = register_map[i].reg_idx;
    }
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx2)
            continue;
        origin2 = register_map[i].reg_idx;
    }

    if (vm_reg_idx1 == vm_reg_idx2) {
        vm_reg[0] = vm_reg[1] = map_vm_reg(state, vm_reg_idx1);
    } else {
        vm_reg[0] = map_vm_reg(state, vm_reg_idx1);
        vm_reg[1] = map_vm_reg_reserved(state, vm_reg_idx2, vm_reg[1]);
        assert(vm_reg[0] != vm_reg[1]);
    }

    if (origin1 != vm_reg[0]) {
        if (sext1)
            emit_load_sext(state, S32, parameter_reg[0], vm_reg[0],
                           offsetof(riscv_t, X) + 4 * vm_reg_idx1);
        else
            emit_load(state, S32, parameter_reg[0], vm_reg[0],
                      offsetof(riscv_t, X) + 4 * vm_reg_idx1);
    }
    if (origin2 != vm_reg[1]) {
        if (sext2)
            emit_load_sext(state, S32, parameter_reg[0], vm_reg[1],
                           offsetof(riscv_t, X) + 4 * vm_reg_idx2);
        else
            emit_load(state, S32, parameter_reg[0], vm_reg[1],
                      offsetof(riscv_t, X) + 4 * vm_reg_idx2);
    }
}
#endif

void parse_branch_history_table(struct jit_state *state,
                                riscv_t *rv UNUSED,
                                rv_insn_t *ir)
{
    int max_idx = 0;
    branch_history_table_t *bt = ir->branch_table;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (!bt->times[i])
            break;
        if (bt->times[max_idx] < bt->times[i])
            max_idx = i;
    }
    if (bt->PC[max_idx] && bt->times[max_idx] >= IN_JUMP_THRESHOLD) {
        IIF(RV32_HAS(SYSTEM))(if (bt->satp[max_idx] == rv->csr_satp), )
        {
            save_reg(state, 0);
            unmap_vm_reg(0);
            emit_load_imm(state, register_map[0].reg_idx, bt->PC[max_idx]);
            emit_cmp32(state, temp_reg, register_map[0].reg_idx);
            uint32_t jump_loc_0 = state->offset;
            emit_jcc_offset(state, 0x85);
#if RV32_HAS(SYSTEM)
            emit_jmp(state, bt->PC[max_idx], bt->satp[max_idx]);
#else
            emit_jmp(state, bt->PC[max_idx], 0);
#endif
            emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
        }
    }
}

void emit_jit_inc_timer(struct jit_state *state)
{
#if defined(__x86_64__)
    /* Increment rv->timer. *rv pointer is stored in RDI register */
    /* INC RDI, [rv + offsetof(riscv_t, timer)] */
    emit_rex(state, 1, 0, 0, 0);
    emit1(state, 0xff);
    emit1(state, 0x87);
    emit4(state, offsetof(riscv_t, timer));
#elif defined(__aarch64__)
    emit_load(state, S64, parameter_reg[0], temp_reg, offsetof(riscv_t, timer));
    emit_alu64_imm8(state, 0, 0, temp_reg, 1);
    emit_store(state, S64, temp_reg, parameter_reg[0],
               offsetof(riscv_t, timer));
#endif
}

#define GEN(inst, code)                                                       \
    static void do_##inst(struct jit_state *state UNUSED, riscv_t *rv UNUSED, \
                          rv_insn_t *ir UNUSED)                               \
    {                                                                         \
        emit_jit_inc_timer(state);                                            \
        code;                                                                 \
    }
#include "rv32_jit.c"
#undef GEN

static void do_fuse1(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        vm_reg[0] = map_vm_reg(state, fuse[i].rd);
        emit_load_imm(state, vm_reg[0], fuse[i].imm);
    }
}

static void do_fuse2(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
    emit_mov(state, vm_reg[0], temp_reg);
    vm_reg[1] = ra_load(state, ir->rs1);
    vm_reg[2] = map_vm_reg(state, ir->rs2);
    emit_mov(state, vm_reg[1], vm_reg[2]);
    emit_alu32(state, 0x01, temp_reg, vm_reg[2]);
}

static void do_fuse3(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = PRIV(rv)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        vm_reg[0] = ra_load(state, fuse[i].rs1);
        emit_load_imm_sext(state, temp_reg,
                           (intptr_t) (m->mem_base + fuse[i].imm));
        emit_alu64(state, 0x01, vm_reg[0], temp_reg);
        vm_reg[1] = ra_load(state, fuse[i].rs2);
        emit_store(state, S32, vm_reg[1], temp_reg, 0);
    }
}

static void do_fuse4(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = PRIV(rv)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        vm_reg[0] = ra_load(state, fuse[i].rs1);
        emit_load_imm_sext(state, temp_reg,
                           (intptr_t) (m->mem_base + fuse[i].imm));
        emit_alu64(state, 0x01, vm_reg[0], temp_reg);
        vm_reg[1] = map_vm_reg(state, fuse[i].rd);
        emit_load(state, S32, temp_reg, vm_reg[1], 0);
    }
}

static void do_fuse5(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            vm_reg[0] = ra_load(state, fuse[i].rs1);
            vm_reg[1] = map_vm_reg(state, fuse[i].rd);
            if (vm_reg[0] != vm_reg[1])
                emit_mov(state, vm_reg[0], vm_reg[1]);
            emit_alu32_imm8(state, 0xc1, 4, vm_reg[1], fuse[i].imm & 0x1f);
            break;
        case rv_insn_srli:
            vm_reg[0] = ra_load(state, fuse[i].rs1);
            vm_reg[1] = map_vm_reg(state, fuse[i].rd);
            if (vm_reg[0] != vm_reg[1])
                emit_mov(state, vm_reg[0], vm_reg[1]);
            emit_alu32_imm8(state, 0xc1, 5, vm_reg[1], fuse[i].imm & 0x1f);
            break;
        case rv_insn_srai:
            vm_reg[0] = ra_load(state, fuse[i].rs1);
            vm_reg[1] = map_vm_reg(state, fuse[i].rd);
            if (vm_reg[0] != vm_reg[1])
                emit_mov(state, vm_reg[0], vm_reg[1]);
            emit_alu32_imm8(state, 0xc1, 7, vm_reg[1], fuse[i].imm & 0x1f);
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

void clear_hot(block_t *block)
{
    block->hot = false;
}

static void code_cache_flush(struct jit_state *state, riscv_t *rv)
{
    rv_log_info("JIT: Flushing code cache (n_blocks=%d, n_jumps=%d, offset=%u)",
                state->n_blocks, state->n_jumps, state->offset);
    should_flush = false;
    state->offset = state->org_size;
    state->n_blocks = 0;
    state->n_jumps = 0; /* Reset jump count when flushing */
    set_reset(&state->set);
    clear_cache_hot(rv->block_cache, (clear_func_t) clear_hot);
#if RV32_HAS(T2C)
    jit_cache_clear(rv->jit_cache);
#endif
    return;
}

typedef void (*codegen_block_func_t)(struct jit_state *,
                                     riscv_t *,
                                     rv_insn_t *);

static void translate(struct jit_state *state, riscv_t *rv, block_t *block)
{
    uint32_t idx;
    rv_insn_t *ir, *next;
    reset_reg();
    liveness_reset();
    liveness_calc(block);
    for (idx = 0, ir = block->ir_head; idx < block->n_insn && !should_flush;
         idx++, ir = next) {
        next = ir->next;
        regs_refresh(idx);
        ((codegen_block_func_t) dispatch_table[ir->opcode])(state, rv, ir);
    }
}

static void resolve_jumps(struct jit_state *state)
{
    rv_log_debug("JIT: Resolving %d jumps", state->n_jumps);
    for (int i = 0; i < state->n_jumps; i++) {
        struct jump jump = state->jumps[i];
        int target_loc;
        if (jump.target_offset != 0)
            target_loc = jump.target_offset;
        else if (jump.target_pc == TARGET_PC_EXIT)
            target_loc = state->exit_loc;
#if defined(__x86_64__)
        else if (jump.target_pc == TARGET_PC_RETPOLINE)
            target_loc = state->retpoline_loc;
#elif defined(__aarch64__)
        else if (jump.target_pc == TARGET_PC_ENTER)
            target_loc = state->entry_loc;
#endif
        else {
            target_loc = jump.offset_loc + sizeof(uint32_t);
            for (int i = 0; i < state->n_blocks; i++) {
                if (jump.target_pc == state->offset_map[i].pc) {
                    IIF(RV32_HAS(SYSTEM))
                    (if (jump.target_satp == state->offset_map[i].satp), )
                    {
                        target_loc = state->offset_map[i].offset;
                        rv_log_debug(
                            "JIT: Jump %d resolved to block pc=0x%08x, "
                            "offset=%d",
                            i, jump.target_pc, target_loc);
                        break;
                    }
                }
            }
        }
#if defined(__x86_64__)
        /* Assumes jump offset is at end of instruction */
        uint32_t rel = target_loc - (jump.offset_loc + sizeof(uint32_t));

        uint8_t *offset_ptr = &state->buf[jump.offset_loc];
        memcpy(offset_ptr, &rel, sizeof(uint32_t));
        sys_icache_invalidate(offset_ptr, sizeof(uint32_t));
#elif defined(__aarch64__)
        int32_t rel = target_loc - jump.offset_loc;
        update_branch_imm(state, jump.offset_loc, rel);
#endif
    }
}

static void translate_chained_block(struct jit_state *state,
                                    riscv_t *rv,
                                    block_t *block)
{
    if (set_has(&state->set, RV_HASH_KEY(block)))
        return;

    if (state->n_blocks == MAX_BLOCKS)
        return;

    assert(set_add(&state->set, RV_HASH_KEY(block)));
    offset_map_insert(state, block);
    translate(state, rv, block);
    if (unlikely(should_flush))
        return;
    rv_insn_t *ir = block->ir_tail;
    if (ir->branch_untaken && !set_has(&state->set, ir->branch_untaken->pc)) {
        block_t *block1 =
            cache_get(rv->block_cache, ir->branch_untaken->pc, false);
        if (block1->translatable) {
            IIF(RV32_HAS(SYSTEM))
            (if (block1->satp == rv->csr_satp), )
                translate_chained_block(state, rv, block1);
        }
    }
    if (ir->branch_taken && !set_has(&state->set, ir->branch_taken->pc)) {
        block_t *block1 =
            cache_get(rv->block_cache, ir->branch_taken->pc, false);
        if (block1->translatable) {
            IIF(RV32_HAS(SYSTEM))
            (if (block1->satp == rv->csr_satp), )
                translate_chained_block(state, rv, block1);
        }
    }

    branch_history_table_t *bt = ir->branch_table;
    if (bt) {
        int max_idx = 0;
        for (int i = 0; i < HISTORY_SIZE; i++) {
            if (!bt->times[i])
                break;
            if (bt->times[max_idx] < bt->times[i])
                max_idx = i;
        }
        if (bt->PC[max_idx] && bt->times[max_idx] >= IN_JUMP_THRESHOLD &&
            !set_has(&state->set, bt->PC[max_idx])) {
            IIF(RV32_HAS(SYSTEM))(if (bt->satp[max_idx] == rv->csr_satp), )
            {
                block_t *block1 =
                    cache_get(rv->block_cache, bt->PC[max_idx], false);
                if (block1 && block1->translatable) {
                    IIF(RV32_HAS(SYSTEM))
                    (if (block1->satp == rv->csr_satp), )
                        translate_chained_block(state, rv, block1);
                }
            }
        }
    }
}

void jit_translate(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    if (set_has(&state->set, RV_HASH_KEY(block))) {
        for (int i = 0; i < state->n_blocks; i++) {
            if (block->pc_start == state->offset_map[i].pc
#if RV32_HAS(SYSTEM)
                && block->satp == state->offset_map[i].satp
#endif
            ) {
                block->offset = state->offset_map[i].offset;
                block->hot = true;
                rv_log_debug("JIT: Cache hit for block pc=0x%08x, offset=%u",
                             block->pc_start, block->offset);
                return;
            }
        }
        assert(NULL);
        __UNREACHABLE;
    }
    rv_log_debug("JIT: Starting translation for block pc=0x%08x",
                 block->pc_start);
restart:
    memset(state->jumps, 0, MAX_JUMPS * sizeof(struct jump));
    state->n_jumps = 0;
    block->offset = state->offset;
    translate_chained_block(state, rv, block);
    if (unlikely(should_flush)) {
        /* Mark block as not translated since translation was incomplete */
        block->hot = false;
        /* Don't reset offset - it will be set correctly on restart */
        rv_log_debug("JIT: Translation triggered flush for block pc=0x%08x",
                     block->pc_start);
        code_cache_flush(state, rv);
        goto restart;
    }
    resolve_jumps(state);
    block->hot = true;
    rv_log_debug(
        "JIT: Translation completed for block pc=0x%08x, offset=%u, size=%u",
        block->pc_start, block->offset, state->offset - block->offset);
}

struct jit_state *jit_state_init(size_t size)
{
    struct jit_state *state = malloc(sizeof(struct jit_state));
    if (!state)
        return NULL;
    assert(state);

    state->offset = 0;
    state->size = size;
#if defined(TSAN_ENABLED)
    /* ThreadSanitizer compatibility: Allocate JIT code buffer at a fixed
     * address above the main memory region to avoid conflicts.
     */
#if defined(__x86_64__)
    /* x86_64 memory layout:
     * Main memory: 0x7d0000000000 - 0x7d0100000000 (4GB for FULL4G)
     * JIT buffer:  0x7d1000000000 + size
     *
     * This keeps both allocations in TSAN's app range (0x7cf000000000 -
     * 0x7ffffffff000) and prevents overlap with main memory or TSAN shadow.
     */
    void *jit_addr = (void *) 0x7d1000000000UL;
    state->buf = mmap(jit_addr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED
#if defined(__APPLE__)
                          | MAP_JIT
#endif
                      ,
                      -1, 0);
#elif defined(__aarch64__)
    /* ARM64 memory layout (macOS/Apple Silicon):
     * Main memory: 0x150000000000 - 0x150100000000 (4GB for FULL4G)
     * JIT buffer:  0x151000000000 + size
     *
     * Apple Silicon requires MAP_JIT for executable memory. The fixed
     * address is chosen to avoid TSAN's shadow memory and typical process
     * allocations. Requires ASLR disabled via: setarch $(uname -m) -R
     */
    void *jit_addr = (void *) 0x151000000000UL;
    state->buf = mmap(jit_addr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED
#if defined(__APPLE__)
                          | MAP_JIT
#endif
                      ,
                      -1, 0);
#else
#error "TSAN is only supported on x86_64 and aarch64"
#endif
    if (state->buf == MAP_FAILED) {
        free(state);
        return NULL;
    }
#else
    /* Standard allocation without TSAN */
    state->buf = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS
#if defined(__APPLE__)
                          | MAP_JIT
#endif
                      ,
                      -1, 0);
    if (state->buf == MAP_FAILED) {
        free(state);
        return NULL;
    }
#endif
    state->n_blocks = 0;
    set_reset(&state->set);
    reset_reg();
    prepare_translate(state);

    state->offset_map = calloc(MAX_BLOCKS, sizeof(struct offset_map));
    if (!state->offset_map) {
        munmap(state->buf, state->size);
        free(state);
        return NULL;
    }

    state->jumps = calloc(MAX_JUMPS, sizeof(struct jump));
    if (!state->jumps) {
        free(state->offset_map);
        munmap(state->buf, state->size);
        free(state);
        return NULL;
    }

    return state;
}

void jit_state_exit(struct jit_state *state)
{
    munmap(state->buf, state->size);
    free(state->offset_map);
    free(state->jumps);
    free(state);
}
