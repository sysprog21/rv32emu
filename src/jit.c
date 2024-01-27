/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* This JIT implementation has undergone extensive modifications, heavily
 * relying on the ubpf_jit_[x86_64|arm64].[ch] from ubpf. The original
 * ubpf_jit_[x86_64|arm64].[ch] file served as the foundation and source of
 * inspiration for adapting and tailoring it specifically for this JIT
 * implementation. Therefore, credit and sincere thanks are extended to ubpf for
 * their invaluable work.
 *
 * Reference:
 *   https://github.com/iovisor/ubpf/blob/main/vm/ubpf_jit_x86_64.c
 *   https://github.com/iovisor/ubpf/blob/main/vm/ubpf_jit_arm64.c
 */

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
#include "riscv.h"
#include "utils.h"

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
#define MAX_INSNS 1024
#if defined(__x86_64__)
#define JUMP_LOC jump_loc + 2
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
#define JUMP_LOC jump_loc
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
    LOG_AND = 0x00000000U,  // 0000_0000_0000_0000_0000_0000_0000_0000
    LOG_ORR = 0x20000000U,  // 0010_0000_0000_0000_0000_0000_0000_0000
    LOG_ORN = 0x20200000U,  // 0010_0000_0010_0000_0000_0000_0000_0000
    LOG_EOR = 0x40000000U,  // 0100_0000_0000_0000_0000_0000_0000_0000
    /* LoadStoreOpcode */
    LS_STRB = 0x00000000U,    // 0000_0000_0000_0000_0000_0000_0000_0000
    LS_LDRB = 0x00400000U,    // 0000_0000_0100_0000_0000_0000_0000_0000
    LS_LDRSBW = 0x00c00000U,  // 0000_0000_1100_0000_0000_0000_0000_0000
    LS_STRH = 0x40000000U,    // 0100_0000_0000_0000_0000_0000_0000_0000
    LS_LDRH = 0x40400000U,    // 0100_0000_0100_0000_0000_0000_0000_0000
    LS_LDRSHW = 0x40c00000U,  // 0100_0000_1100_0000_0000_0000_0000_0000
    LS_STRW = 0x80000000U,    // 1000_0000_0000_0000_0000_0000_0000_0000
    LS_LDRW = 0x80400000U,    // 1000_0000_0100_0000_0000_0000_0000_0000
    LS_STRX = 0xc0000000U,    // 1100_0000_0000_0000_0000_0000_0000_0000
    LS_LDRX = 0xc0400000U,    // 1100_0000_0100_0000_0000_0000_0000_0000
    /* LoadStorePairOpcode */
    LSP_STPX = 0xa9000000U,  // 1010_1001_0000_0000_0000_0000_0000_0000
    LSP_LDPX = 0xa9400000U,  // 1010_1001_0100_0000_0000_0000_0000_0000
    /* UnconditionalBranchOpcode */
    BR_BR = 0xd61f0000U,   // 1101_0110_0001_1111_0000_0000_0000_0000
    BR_BLR = 0xd63f0000U,  // 1101_0110_0011_1111_0000_0000_0000_0000
    BR_RET = 0xd65f0000U,  // 1101_0110_0101_1111_0000_0000_0000_0000
    /* UnconditionalBranchImmediateOpcode */
    UBR_B = 0x14000000U,  // 0001_0100_0000_0000_0000_0000_0000_0000
    /* ConditionalBranchImmediateOpcode */
    BR_Bcond = 0x54000000U,
    /* DP2Opcode */
    DP2_UDIV = 0x1ac00800U,  // 0001_1010_1100_0000_0000_1000_0000_0000
    DP2_LSLV = 0x1ac02000U,  // 0001_1010_1100_0000_0010_0000_0000_0000
    DP2_LSRV = 0x1ac02400U,  // 0001_1010_1100_0000_0010_0100_0000_0000
    DP2_ASRV = 0x1ac02800U,  // 0001_1010_1100_0000_0010_1000_0000_0000
    /* DP3Opcode */
    DP3_MADD = 0x1b000000U,  // 0001_1011_0000_0000_0000_0000_0000_0000
    DP3_MSUB = 0x1b008000U,  // 0001_1011_0000_0000_1000_0000_0000_0000
    /* MoveWideOpcode */
    MW_MOVN = 0x12800000U,  // 0001_0010_1000_0000_0000_0000_0000_0000
    MW_MOVZ = 0x52800000U,  // 0101_0010_1000_0000_0000_0000_0000_0000
    MW_MOVK = 0x72800000U,  // 0111_0010_1000_0000_0000_0000_0000_0000
} a64opcode_t;

enum condition {
    COND_EQ,
    COND_NE,
    COND_HS,
    COND_LO,
    COND_GE = 10,
    COND_LT = 11,
};

enum {
    temp_imm_reg = R24, /* Temp register for immediate generation */
    temp_div_reg = R25, /* Temp register for division results */
};
#endif

enum vm_reg {
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

enum operand_size {
    S8,
    S16,
    S32,
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
#define RCX_ALT R10
static const int register_map[] = {
    RAX, R10, RDX, R8, R9, R14, R15, RDI, RSI, RBX, RBP,
};
#else
#define RCX_ALT R9
static const int nonvolatile_reg[] = {RBP, RBX, R13, R14, R15};
static const int parameter_reg[] = {RDI, RSI, RDX, RCX, R8, R9};
static const int temp_reg[] = {RAX, RBX, RCX};
static const int register_map[] = {
    RAX, RDI, RSI, RDX, R9, R8, RBX, R13, R14, R15, RBP,
};
#endif
#elif defined(__aarch64__)
/* callee_reg - this must be a multiple of two because of how we save the stack
 * later on. */
static const int callee_reg[] = {R19, R20, R21, R22, R23, R24, R25, R26};
/* parameter_reg (Caller saved registers) */
static const int parameter_reg[] = {R0, R1, R2, R3, R4};
static const int temp_reg[] = {R6, R7, R8};

/*  Register assignments:
 *  Arm64       Usage
 *   r0 - r4     Function parameters, caller-saved
 *   r6 - r8     Temp - used for storing calculated value during execution
 *   r19 - r23   Callee-saved registers
 *   r24         Temp - used for generating 32-bit immediates
 *   r25         Temp - used for modulous calculations
 */

static const int register_map[] = {
    R5,                      /* result */
    R0,  R1,  R2,  R3,  R4,  /* parameters */
    R19, R20, R21, R22, R23, /* callee-saved */
};
static inline void emit_load_imm(struct jit_state *state, int dst, int64_t imm);
#endif

/* Return the register for the given JIT register */
static int map_register(int r)
{
    assert(r < N_VM_REGS);
    return register_map[r % N_VM_REGS];
}

static inline void offset_map_insert(struct jit_state *state, int32_t target_pc)
{
    struct offset_map *map_entry = &state->offset_map[state->n_insn++];
    map_entry->pc = target_pc;
    map_entry->offset = state->offset;
}

#if !defined(__APPLE__)
#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *) (addr), (char *) (addr) + (size));
#endif

static void emit_bytes(struct jit_state *state, void *data, uint32_t len)
{
    assert(state->offset <= state->size - len);
    if (unlikely((state->offset + len) > state->size)) {
        state->offset = state->size;
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

static inline void emit2(struct jit_state *state, uint16_t x)
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
    emit_basic_rex(state, 0, 0, r);
    emit1(state, 0x50 | (r & 7));
}

static inline void emit_pop(struct jit_state *state, int r)
{
    emit_basic_rex(state, 0, 0, r);
    emit1(state, 0x58 | (r & 7));
}

static inline void emit_jump_target_address(struct jit_state *state,
                                            int32_t target_pc)
{
    struct jump *jump = &state->jumps[state->n_jumps++];
    jump->offset_loc = state->offset;
    jump->target_pc = target_pc;
    emit4(state, 0);
}
#elif defined(__aarch64__)
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
     * instruction.  */
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
    if (op != MW_MOVK) {
        emit_a64(state, sz(is64) | op | (0 << 21) | (0 << 5) | rd);
    }
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
}


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
}

static void update_branch_imm(struct jit_state *state,
                              uint32_t offset,
                              int32_t imm)
{
    assert((imm & 3) == 0);
    uint32_t insn;
    imm >>= 2;
    memcpy(&insn, state->buf + offset, sizeof(uint32_t));
    if ((insn & 0xfe000000U) == 0x54000000U /* Conditional branch immediate. */
        || (insn & 0x7e000000U) ==
               0x34000000U) { /* Compare and branch immediate.  */
        assert((imm >> 19) == INT64_C(-1) || (imm >> 19) == 0);
        insn |= (imm & 0x7ffff) << 5;
    } else if ((insn & 0x7c000000U) == 0x14000000U) {
        /* Unconditional branch immediate.  */
        assert((imm >> 26) == INT64_C(-1) || (imm >> 26) == 0);
        insn |= (imm & 0x03ffffffU) << 0;
    } else {
        assert(false);
        insn = BAD_OPCODE;
    }
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(false);
#endif
    memcpy(state->buf + offset, &insn, sizeof(uint32_t));
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(true);
#endif
}
#endif

static inline void emit_jump_target_offset(struct jit_state *state,
                                           uint32_t jump_loc,
                                           uint32_t jump_state_offset)
{
    struct jump *jump = &state->jumps[state->n_jumps++];
    jump->offset_loc = jump_loc;
    jump->target_offset = jump_state_offset;
}

static inline void emit_alu32(struct jit_state *state, int op, int src, int dst)
{
#if defined(__x86_64__)
    /* The REX prefix and ModRM byte are emitted.
     * The MR encoding is utilized when a choice is available. The 'src' is
     * often used as an opcode extension.
     */
    emit_basic_rex(state, 0, src, dst);
    emit1(state, op);
    emit_modrm_reg2reg(state, src, dst);
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
            emit_dataproc_2source(state, false, DP2_LSLV, dst, dst, R8);
        else if (src == 5) /* SRL */
            emit_dataproc_2source(state, false, DP2_LSRV, dst, dst, R8);
        else if (src == 7) /* SRA */
            emit_dataproc_2source(state, false, DP2_ASRV, dst, dst, R8);
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
        emit_logical_register(state, false, LOG_EOR, dst, src, R10);
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
#elif defined(__aarch64__)
    if (op == 0x01)
        emit_addsub_register(state, true, AS_ADD, dst, dst, src);
#endif
}

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
    }
#endif
}

#if defined(__x86_64__)
/* Register to register mov */
static inline void emit_mov(struct jit_state *state, int src, int dst)
{
    emit_alu64(state, 0x89, src, dst);
}

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
#elif defined(__aarch64__)
static void divmod(struct jit_state *state,
                   uint8_t opcode,
                   int rd,
                   int rn,
                   int rm)
{
    bool mod = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_MOD_IMM & JIT_ALU_OP_MASK);
    bool is64 = (opcode & JIT_CLS_MASK) == JIT_CLS_ALU64;
    int div_dest = mod ? temp_div_reg : rd;

    /* Do not need to treet divide by zero as special because the UDIV
     * instruction already returns 0 when dividing by zero.
     */
    emit_dataproc_2source(state, is64, DP2_UDIV, div_dest, rn, rm);
    if (mod) {
        emit_dataproc_3source(state, is64, DP3_MSUB, rd, rm, div_dest, rn);
    }
}
#endif

static inline void emit_cmp_imm32(struct jit_state *state, int dst, int32_t imm)
{
#if defined(__x86_64__)
    emit_alu64_imm32(state, 0x81, 7, dst, imm);
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
    default:
        __UNREACHABLE;
        break;
    }
    emit_a64(state, BR_Bcond | (0 << 5) | code);
#endif
}

/* Load [src + offset] into dst */
static inline void emit_load(struct jit_state *state,
                             enum operand_size size,
                             int src,
                             int dst,
                             int32_t offset)
{
#if defined(__x86_64__)
    if (size == S8 || size == S16) {
        /* movzx */
        emit1(state, 0x0f);
        emit1(state, size == S8 ? 0xb6 : 0xb7);
    } else if (size == S32) {
        /* mov */
        emit1(state, 0x8b);
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
    default:
        __UNREACHABLE;
        break;
    }
#endif
}

static inline void emit_load_sext(struct jit_state *state,
                                  enum operand_size size,
                                  int src,
                                  int dst,
                                  int32_t offset)
{
#if defined(__x86_64__)
    if (size == S8 || size == S16) {
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
    default:
        __UNREACHABLE;
        break;
    }
#endif
}

/* Load sign-extended immediate into register */
static inline void emit_load_imm(struct jit_state *state, int dst, int64_t imm)
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
#elif defined(__aarch64__)
    if ((int32_t) imm == imm)
        emit_movewide_imm(state, false, dst, imm);
    else
        emit_movewide_imm(state, true, dst, imm);
#endif
}

/* Store register src to [dst + offset] */
static inline void emit_store(struct jit_state *state,
                              enum operand_size size,
                              int src,
                              int dst,
                              int32_t offset)
{
#if defined(__x86_64__)
    if (size == S16)
        emit1(state, 0x66); /* 16-bit override */
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
    default:
        __UNREACHABLE;
        break;
    }
#endif
}

/* Store imm to [dst + offset] */
static inline void emit_store_imm32(struct jit_state *state,
                                    enum operand_size size,
                                    int dst,
                                    int32_t offset,
                                    int32_t imm)
{
#if defined(__x86_64__)
    if (size == S16)
        emit1(state, 0x66); /* 16-bit override */
    emit1(state, size == S8 ? 0xc6 : 0xc7);
    emit_modrm_and_displacement(state, 0, dst, offset);
    switch (size) {
    case S32:
        emit4(state, imm);
        break;
    case S16:
        emit2(state, imm);
        break;
    case S8:
        emit1(state, imm);
        break;
    default:
        __UNREACHABLE;
        break;
    }
#elif defined(__aarch64__)
    emit_load_imm(state, R10, imm);
    emit_store(state, size, R10, dst, offset);
#endif
}

static inline void emit_jmp(struct jit_state *state, uint32_t target_pc)
{
#if defined(__x86_64__)
    emit1(state, 0xe9);
    emit_jump_target_address(state, target_pc);
#elif defined(__aarch64__)
    struct jump *jump = &state->jumps[state->n_jumps++];
    jump->offset_loc = state->offset;
    jump->target_pc = target_pc;
    emit_a64(state, UBR_B);
#endif
}

static inline void emit_call(struct jit_state *state, intptr_t target)
{
#if defined(__x86_64__)
    emit_load_imm(state, RAX, target);
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

    int dest = map_register(0);
    if (dest != R0) {
        emit_logical_register(state, true, LOG_ORR, dest, RZ, R0);
    }

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
    emit_jmp(state, TARGET_PC_EXIT);
#endif
}

/* TODO: muldivmod is incomplete, it does not handle imm or overflow now */
#if RV32_HAS(EXT_M)
static void muldivmod(struct jit_state *state,
                      uint8_t opcode,
                      int src,
                      int dst,
                      int32_t imm UNUSED)
{
#if defined(__x86_64__)
    bool mul = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_MUL_IMM & JIT_ALU_OP_MASK);
    bool div = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_DIV_IMM & JIT_ALU_OP_MASK);
    bool mod = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_MOD_IMM & JIT_ALU_OP_MASK);
    bool is64 = (opcode & JIT_CLS_MASK) == JIT_CLS_ALU64;
    bool reg = (opcode & JIT_SRC_REG) == JIT_SRC_REG;

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
#elif defined(__aarch64__)
    switch (opcode) {
    case 0x28:
        emit_dataproc_3source(state, false, DP3_MADD, dst, dst, src, RZ);
        break;
    case 0x2f:
        emit_dataproc_3source(state, true, DP3_MADD, dst, dst, src, RZ);
        break;
    case 0x38:
        divmod(state, JIT_OP_DIV_REG, dst, dst, src);
        break;
    case 0x98:
        divmod(state, JIT_OP_MOD_REG, dst, dst, src);
        break;
    default:
        __UNREACHABLE;
        break;
    }
#endif
}
#endif /* RV32_HAS(EXT_M) */

static void prepare_translate(struct jit_state *state)
{
#if defined(__x86_64__)
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
#elif defined(__aarch64__)
    uint32_t register_space = ARRAYS_SIZE(callee_reg) * 8 + 2 * 8;
    state->stack_size = align_up(STACK_SIZE + register_space, 16);
    emit_addsub_imm(state, true, AS_SUB, SP, SP, state->stack_size);

    /* Set up frame */
    emit_loadstorepair_imm(state, LSP_STPX, R29, R30, SP, 0);
    /* In ARM64 calling convention, R29 is the frame pointer. */
    emit_addsub_imm(state, true, AS_ADD, R29, SP, 0);

    /* Save callee saved registers */
    for (size_t i = 0; i < ARRAYS_SIZE(callee_reg); i += 2) {
        emit_loadstorepair_imm(state, LSP_STPX, callee_reg[i],
                               callee_reg[i + 1], SP, (i + 2) * 8);
    }

    emit_uncond_branch_reg(state, BR_BR, R1);
    /* Epilogue */
    state->exit_loc = state->offset;

    /* Move register 0 into R0 */
    if (map_register(0) != R0) {
        emit_logical_register(state, true, LOG_ORR, R0, RZ, map_register(0));
    }

    /* Restore callee-saved registers).  */
    for (size_t i = 0; i < ARRAYS_SIZE(callee_reg); i += 2) {
        emit_loadstorepair_imm(state, LSP_LDPX, callee_reg[i],
                               callee_reg[i + 1], SP, (i + 2) * 8);
    }
    emit_loadstorepair_imm(state, LSP_LDPX, R29, R30, SP, 0);
    emit_addsub_imm(state, true, AS_ADD, SP, SP, state->stack_size);
    emit_uncond_branch_reg(state, BR_RET, R30);
#endif
}

#define GEN(inst, code)                                                       \
    static void do_##inst(struct jit_state *state UNUSED, riscv_t *rv UNUSED, \
                          rv_insn_t *ir UNUSED)                               \
    {                                                                         \
        code;                                                                 \
    }
#include "rv32_jit.c"
#undef GEN

static void do_fuse1(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        emit_load_imm(state, temp_reg[0], fuse[i].imm);
        emit_store(state, S32, temp_reg[0], parameter_reg[0],
                   offsetof(riscv_t, X) + 4 * fuse[i].rd);
    }
}

static void do_fuse2(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    emit_load_imm(state, temp_reg[0], ir->imm);
    emit_store(state, S32, temp_reg[0], parameter_reg[0],
               offsetof(riscv_t, X) + 4 * ir->rd);
    emit_load(state, S32, parameter_reg[0], temp_reg[1],
              offsetof(riscv_t, X) + 4 * ir->rs1);
    emit_alu32(state, 0x01, temp_reg[1], temp_reg[0]);
    emit_store(state, S32, temp_reg[0], parameter_reg[0],
               offsetof(riscv_t, X) + 4 * ir->rs2);
}

static void do_fuse3(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = ((state_t *) rv->userdata)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        emit_load(state, S32, parameter_reg[0], temp_reg[0],
                  offsetof(riscv_t, X) + 4 * fuse[i].rs1);
        emit_load_imm(state, temp_reg[1],
                      (intptr_t) (m->mem_base + fuse[i].imm));
        emit_alu64(state, 0x01, temp_reg[1], temp_reg[0]);
        emit_load(state, S32, parameter_reg[0], temp_reg[1],
                  offsetof(riscv_t, X) + 4 * fuse[i].rs2);
        emit_store(state, S32, temp_reg[1], temp_reg[0], 0);
    }
}

static void do_fuse4(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = ((state_t *) rv->userdata)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        emit_load(state, S32, parameter_reg[0], temp_reg[0],
                  offsetof(riscv_t, X) + 4 * fuse[i].rs1);
        emit_load_imm(state, temp_reg[1],
                      (intptr_t) (m->mem_base + fuse[i].imm));
        emit_alu64(state, 0x01, temp_reg[1], temp_reg[0]);
        emit_load(state, S32, temp_reg[0], temp_reg[1], 0);
        emit_store(state, S32, temp_reg[1], parameter_reg[0],
                   offsetof(riscv_t, X) + 4 * fuse[i].rd);
    }
}

static void do_fuse5(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    emit_load_imm(state, temp_reg[0], ir->pc + 4);
    emit_store(state, S32, temp_reg[0], parameter_reg[0],
               offsetof(riscv_t, PC));
    emit_call(state, (intptr_t) rv->io.on_memset);
    emit_exit(&(*state));
}

static void do_fuse6(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    emit_load_imm(state, temp_reg[0], ir->pc + 4);
    emit_store(state, S32, temp_reg[0], parameter_reg[0],
               offsetof(riscv_t, PC));
    emit_call(state, (intptr_t) rv->io.on_memcpy);
    emit_exit(&(*state));
}

static void do_fuse7(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            emit_load(state, S32, parameter_reg[0], temp_reg[0],
                      offsetof(riscv_t, X) + 4 * fuse[i].rs1);
            emit_alu32_imm8(state, 0xc1, 4, temp_reg[0], fuse[i].imm & 0x1f);
            emit_store(state, S32, temp_reg[0], parameter_reg[0],
                       offsetof(riscv_t, X) + 4 * fuse[i].rd);
            break;
        case rv_insn_srli:
            emit_load(state, S32, parameter_reg[0], temp_reg[0],
                      offsetof(riscv_t, X) + 4 * fuse[i].rs1);
            emit_alu32_imm8(state, 0xc1, 5, temp_reg[0], fuse[i].imm & 0x1f);
            emit_store(state, S32, temp_reg[0], parameter_reg[0],
                       offsetof(riscv_t, X) + 4 * fuse[i].rd);
            break;
        case rv_insn_srai:
            emit_load(state, S32, parameter_reg[0], temp_reg[0],
                      offsetof(riscv_t, X) + 4 * fuse[i].rs1);
            emit_alu32_imm8(state, 0xc1, 7, temp_reg[0], fuse[i].imm & 0x1f);
            emit_store(state, S32, temp_reg[0], parameter_reg[0],
                       offsetof(riscv_t, X) + 4 * fuse[i].rd);
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
            for (int i = 0; i < state->n_insn; i++) {
                if (jump.target_pc == state->offset_map[i].pc) {
                    target_loc = state->offset_map[i].offset;
                    break;
                }
            }
        }
#if defined(__x86_64__)
        /* Assumes jump offset is at end of instruction */
        uint32_t rel = target_loc - (jump.offset_loc + sizeof(uint32_t));

        uint8_t *offset_ptr = &state->buf[jump.offset_loc];
        memcpy(offset_ptr, &rel, sizeof(uint32_t));
#elif defined(__aarch64__)
        int32_t rel = target_loc - jump.offset_loc;
        update_branch_imm(state, jump.offset_loc, rel);
#endif
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
    if (ir->branch_untaken && !set_has(set, ir->branch_untaken->pc)) {
        block_t *block1 =
            cache_get(rv->block_cache, ir->branch_untaken->pc, false);
        if (block1->translatable)
            translate_chained_block(state, rv, block1, set);
    }
    if (ir->branch_taken && !set_has(set, ir->branch_taken->pc)) {
        block_t *block1 =
            cache_get(rv->block_cache, ir->branch_taken->pc, false);
        if (block1->translatable)
            translate_chained_block(state, rv, block1, set);
    }
}

uint32_t jit_translate(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    memset(state->offset_map, 0, MAX_INSNS * sizeof(struct offset_map));
    memset(state->jumps, 0, MAX_INSNS * sizeof(struct jump));
    state->n_insn = 0;
    state->n_jumps = 0;
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

struct jit_state *jit_state_init(size_t size)
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

void jit_state_exit(struct jit_state *state)
{
    munmap(state->buf, state->size);
    free(state->offset_map);
    free(state->jumps);
    free(state);
}
