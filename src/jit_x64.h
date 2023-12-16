/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "riscv_private.h"

enum X64_REG {
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

enum operand_size {
    S8,
    S16,
    S32,
};

struct jump {
    uint32_t offset_loc;
    uint32_t target_pc;
    uint32_t target_offset;
};

/* Special values for target_pc in struct jump */
#define TARGET_PC_EXIT -1U
#define TARGET_PC_RETPOLINE -3U

struct offset_map {
    uint32_t PC;
    uint32_t offset;
};

struct jit_state {
    uint8_t *buf;
    uint32_t offset;
    uint32_t size;
    uint32_t exit_loc;
    uint32_t retpoline_loc;
    struct offset_map *offset_map;
    int num_insn;
    struct jump *jumps;
    int num_jumps;
};

struct jit_state *init_state(size_t size);
void destroy_state(struct jit_state *state);
uint32_t translate_x64(riscv_t *rv, block_t *block);

static inline void offset_map_insert(struct jit_state *state, int32_t target_pc)
{
    struct offset_map *map_entry = &state->offset_map[state->num_insn++];
    map_entry->PC = target_pc;
    map_entry->offset = state->offset;
}

static inline void emit_bytes(struct jit_state *state, void *data, uint32_t len)
{
    assert(state->offset <= state->size - len);
    if ((state->offset + len) > state->size) {
        state->offset = state->size;
        return;
    }
    memcpy(state->buf + state->offset, data, len);
    state->offset += len;
}

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

static inline void emit_jump_target_address(struct jit_state *state,
                                            int32_t target_pc)
{
    struct jump *jump = &state->jumps[state->num_jumps++];
    jump->offset_loc = state->offset;
    jump->target_pc = target_pc;
    emit4(state, 0);
}

static inline void emit_jump_target_offset(struct jit_state *state,
                                           uint32_t jump_loc,
                                           uint32_t jump_state_offset)
{
    struct jump *jump = &state->jumps[state->num_jumps++];
    jump->offset_loc = jump_loc;
    jump->target_offset = jump_state_offset;
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
    } else if (d >= -128 && d <= 127) {
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

/* The REX prefix and ModRM byte are emitted.
 * The MR encoding is utilized when a choice is available. The 'src' is often
 * used as an opcode extension.
 */
static inline void emit_alu32(struct jit_state *state, int op, int src, int dst)
{
    emit_basic_rex(state, 0, src, dst);
    emit1(state, op);
    emit_modrm_reg2reg(state, src, dst);
}

/* REX prefix, ModRM byte, and 32-bit immediate */
static inline void emit_alu32_imm32(struct jit_state *state,
                                    int op,
                                    int src,
                                    int dst,
                                    int32_t imm)
{
    emit_alu32(state, op, src, dst);
    emit4(state, imm);
}

/* REX prefix, ModRM byte, and 8-bit immediate */
static inline void emit_alu32_imm8(struct jit_state *state,
                                   int op,
                                   int src,
                                   int dst,
                                   int8_t imm)
{
    emit_alu32(state, op, src, dst);
    emit1(state, imm);
}

/* The REX.W prefix and ModRM byte are emitted.
 * The MR encoding is used when there is a choice. 'src' is often used as
 * an opcode extension.
 */
static inline void emit_alu64(struct jit_state *state, int op, int src, int dst)
{
    emit_basic_rex(state, 1, src, dst);
    emit1(state, op);
    emit_modrm_reg2reg(state, src, dst);
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

/* REX.W prefix, ModRM byte, and 8-bit immediate */
static inline void emit_alu64_imm8(struct jit_state *state,
                                   int op,
                                   int src,
                                   int dst,
                                   int8_t imm)
{
    emit_alu64(state, op, src, dst);
    emit1(state, imm);
}

/* Register to register mov */
static inline void emit_mov(struct jit_state *state, int src, int dst)
{
    emit_alu64(state, 0x89, src, dst);
}

static inline void emit_cmp_imm32(struct jit_state *state, int dst, int32_t imm)
{
    emit_alu64_imm32(state, 0x81, 7, dst, imm);
}

static inline void emit_cmp32_imm32(struct jit_state *state,
                                    int dst,
                                    int32_t imm)
{
    emit_alu32_imm32(state, 0x81, 7, dst, imm);
}

static inline void emit_cmp(struct jit_state *state, int src, int dst)
{
    emit_alu64(state, 0x39, src, dst);
}

static inline void emit_cmp32(struct jit_state *state, int src, int dst)
{
    emit_alu32(state, 0x39, src, dst);
}

static inline void emit_jcc(struct jit_state *state,
                            int code,
                            int32_t target_pc)
{
    emit1(state, 0x0f);
    emit1(state, code);
    emit_jump_target_address(state, target_pc);
}

static inline void emit_jcc_offset(struct jit_state *state, int code)
{
    emit1(state, 0x0f);
    emit1(state, code);
    emit4(state, 0);
}

/* Load [src + offset] into dst */
static inline void emit_load(struct jit_state *state,
                             enum operand_size size,
                             int src,
                             int dst,
                             int32_t offset)
{
    if (size == S8 || size == S16) {
        /* movzx */
        emit1(state, 0x0f);
        emit1(state, size == S8 ? 0xb6 : 0xb7);
    } else if (size == S32) {
        /* mov */
        emit1(state, 0x8b);
    }

    emit_modrm_and_displacement(state, dst, src, offset);
}

static inline void emit_load_sext(struct jit_state *state,
                                  enum operand_size size,
                                  int src,
                                  int dst,
                                  int32_t offset)
{
    if (size == S8 || size == S16) {
        /* movsx */
        emit1(state, 0x0f);
        emit1(state, size == S8 ? 0xbe : 0xbf);
    } else if (size == S32) {
        emit_basic_rex(state, 1, dst, src);
        emit1(state, 0x63);
    }

    emit_modrm_and_displacement(state, dst, src, offset);
}

/* Load sign-extended immediate into register */
static inline void emit_load_imm(struct jit_state *state, int dst, int64_t imm)
{
    if (imm >= INT32_MIN && imm <= INT32_MAX) {
        emit_alu64_imm32(state, 0xc7, 0, dst, imm);
    } else {
        /* movabs $imm, dst */
        emit_basic_rex(state, 1, 0, dst);
        emit1(state, 0xb8 | (dst & 7));
        emit8(state, imm);
    }
}

/* Store register src to [dst + offset] */
static inline void emit_store(struct jit_state *state,
                              enum operand_size size,
                              int src,
                              int dst,
                              int32_t offset)
{
    if (size == S16)
        emit1(state, 0x66); /* 16-bit override */
    emit1(state, size == S8 ? 0x88 : 0x89);
    emit_modrm_and_displacement(state, src, dst, offset);
}

/* Store immediate to [dst + offset] */
static inline void emit_store_imm32(struct jit_state *state,
                                    enum operand_size size,
                                    int dst,
                                    int32_t offset,
                                    int32_t imm)
{
    if (size == S16)
        emit1(state, 0x66); /* 16-bit override */
    emit1(state, size == S8 ? 0xc6 : 0xc7);
    emit_modrm_and_displacement(state, 0, dst, offset);
    if (size == S32) {
        emit4(state, imm);
    } else if (size == S16) {
        emit2(state, imm);
    } else if (size == S8) {
        emit1(state, imm);
    }
}

static inline void emit_ret(struct jit_state *state)
{
    emit1(state, 0xc3);
}

static inline void emit_jmp(struct jit_state *state, uint32_t target_pc)
{
    emit1(state, 0xe9);
    emit_jump_target_address(state, target_pc);
}

static inline void emit_call(struct jit_state *state, intptr_t target)
{
    emit_load_imm(state, RAX, (intptr_t) target);
    /* callq *%rax */
    emit1(state, 0xff);
    /* ModR/M byte: b11010000b = xd0, rax is register 0 */
    emit1(state, 0xd0);
}

static inline void emit_exit(struct jit_state *state)
{
    emit1(state, 0xe9);
    emit_jump_target_offset(state, state->offset, state->exit_loc);
    emit4(state, 0);
}
