/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Host-side Wasm backend checks. Run with make check-jit-wasm. The backend is
 * included directly so the checks can inspect its private state.
 */
#include <assert.h>
#include <emscripten/heap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Return to the fixture after every block unless a check enables chaining. */
static uint32_t cycle_slice;
#define WASM_CYCLE_SLICE cycle_slice
#include "jit_wasm.c"

static block_t *active;

/* The unit fixture has one cached block. Exercise the real backend's cache
 * reset callback without constructing an emulator and a guest address space.
 */
void clear_cache_hot(const struct cache *cache, clear_func_t clear)
{
    (void) cache;
    clear(active);
}

static int helper_calls;
static bool helper_exit;
static bool memory_helper(riscv_t *rv,
                          const rv_insn_t *ir,
                          uint64_t cycle,
                          uint32_t pc)
{
    assert(!ir->next && !ir->branch_taken && !ir->branch_untaken);
    assert(rv->X[5] == 42); /* preceding generated writes are visible */
    rv->X[6] = rv->X[5] + 1;
    rv->csr_cycle = cycle + 1;
    rv->PC = helper_exit ? 0x9000 : pc + 4;
    helper_calls++;
    return true;
}

#if !RV32_HAS(EXT_C)
/* Stands in for an interpreter jump that traps before linking. */
static int jump_helper_calls;
static bool jump_helper(riscv_t *rv,
                        const rv_insn_t *ir,
                        uint64_t cycle,
                        uint32_t pc)
{
    (void) ir;
    (void) pc;
    assert(rv->X[5] == 0x2001); /* the link register is still unwritten */
    rv->csr_cycle = cycle;
    rv->PC = 0x9000;
    jump_helper_calls++;
    return true;
}
#endif

/* Stands in for an interpreter call that halts the hart, as an exit system call
 * does, and then continues to the next record.
 */
static bool halt_helper(riscv_t *rv,
                        const rv_insn_t *ir,
                        uint64_t cycle,
                        uint32_t pc)
{
    (void) ir;
    rv->halt = true;
    rv->csr_cycle = cycle + 1;
    rv->PC = pc + 4;
    return true;
}

/* Stands in for an interpreter chain that paused after leaving its block. */
static const rv_insn_t paused_insn;
static bool paused_helper(riscv_t *rv,
                          const rv_insn_t *ir,
                          uint64_t cycle,
                          uint32_t pc)
{
    (void) ir;
    rv->csr_cycle = cycle + 1;
    rv->PC = pc + 4; /* coincides with the next record */
    rv->next_insn = &paused_insn;
    return true;
}

/* Link n records at 4-byte steps from pc into one block, as the decoder does.
 */
static block_t make_block(rv_insn_t *irs, unsigned n, uint32_t pc)
{
    for (unsigned i = 0; i < n; i++) {
        irs[i].pc = pc + 4 * i;
        irs[i].next = i + 1 < n ? &irs[i + 1] : NULL;
    }
    return (block_t) {.ir_head = irs,
                      .ir_tail = &irs[n - 1],
                      .pc_start = pc,
                      .pc_end = pc + 4 * n,
                      .translatable = true};
}

static void run(riscv_t *rv, rv_insn_t *irs, unsigned n)
{
    block_t block = make_block(irs, n, 0x1000);
    active = &block;
    assert(jit_translate(rv, &block));
    assert(block.hot && !((struct jit_state *) rv->jit_state)->disabled);
    static bool grown;
    if (!grown) {
        assert(emscripten_resize_heap(emscripten_get_heap_size() + 65536));
        grown = true;
    }
    jit_wasm_run(rv, &block);
    jit_wasm_retire_block(rv, &block);
    active = NULL;
}

/* An independent model of each register operation, written from the ISA rather
 * than from the lowering. a is the first source (rd for compressed forms that
 * read it, sp for the SP forms) and b the second. Compressed forms share the
 * case of their base instruction.
 */
#if RV32_HAS(EXT_C)
#define C_CASE(op) case rv_insn_##op:
#else
#define C_CASE(op)
#endif
static uint32_t reference(unsigned op, uint32_t a, uint32_t b, int32_t imm)
{
    const uint32_t u = (uint32_t) imm;
    switch (op) {
    case rv_insn_lui:
        C_CASE(cli) C_CASE(clui) return u;
    case rv_insn_addi:
        C_CASE(caddi) C_CASE(caddi16sp) C_CASE(caddi4spn) return a + u;
    case rv_insn_xori:
        return a ^ u;
    case rv_insn_ori:
        return a | u;
    case rv_insn_andi:
        C_CASE(candi)
        return a & u;
    case rv_insn_slti:
        return (int32_t) a < imm;
    case rv_insn_sltiu:
        return a < u;
    case rv_insn_slli:
        C_CASE(cslli)
        return a << imm;
    case rv_insn_srli:
        C_CASE(csrli)
        return a >> imm;
    case rv_insn_srai:
        C_CASE(csrai)
        return (uint32_t) ((int32_t) a >> imm);
    case rv_insn_add:
        C_CASE(cadd)
        return a + b;
    case rv_insn_sub:
        C_CASE(csub)
        return a - b;
    case rv_insn_xor:
        C_CASE(cxor)
        return a ^ b;
    case rv_insn_or:
        C_CASE(cor)
        return a | b;
    case rv_insn_and:
        C_CASE(cand)
        return a & b;
    case rv_insn_slt:
        return (int32_t) a < (int32_t) b;
    case rv_insn_sltu:
        return a < b;
    case rv_insn_sll:
        return a << (b & 31);
    case rv_insn_srl:
        return a >> (b & 31);
    case rv_insn_sra:
        return (uint32_t) ((int32_t) a >> (b & 31));
#if RV32_HAS(EXT_M)
    case rv_insn_mul:
        return a * b;
    case rv_insn_mulh:
        return (uint32_t) (((int64_t) (int32_t) a * (int32_t) b) >> 32);
    case rv_insn_mulhsu:
        return (uint32_t) (((int64_t) (int32_t) a * (int64_t) b) >> 32);
    case rv_insn_mulhu:
        return (uint32_t) (((uint64_t) a * b) >> 32);
#endif
#if RV32_HAS(EXT_C)
    case rv_insn_cmv:
        return b;
#endif
    default:
        abort();
    }
}

/* Fill in the registers as the decoder does for op, with a in the first source;
 * return the destination.
 */
static unsigned layout(rv_insn_t *ir, riscv_t *rv, uint32_t a, uint32_t b)
{
    ir->rd = 10;
    ir->rs1 = 11;
    ir->rs2 = 12;
    rv->X[11] = a;
    rv->X[12] = b;
    switch (ir->opcode) {
#if RV32_HAS(EXT_C)
    case rv_insn_caddi:
    case rv_insn_cslli:
    case rv_insn_csrli:
    case rv_insn_csrai:
    case rv_insn_candi:
    case rv_insn_cadd:
    case rv_insn_csub:
    case rv_insn_cxor:
    case rv_insn_cor:
    case rv_insn_cand:
        ir->rd = ir->rs1; /* the destination is also the first source */
        ir->shamt = ir->imm;
        break;
    case rv_insn_caddi4spn:
        ir->rs1 = rv_reg_sp;
        rv->X[rv_reg_sp] = a;
        break;
    case rv_insn_caddi16sp:
        ir->rd = ir->rs1 = rv_reg_sp;
        rv->X[rv_reg_sp] = a;
        break;
#endif
    default:
        break;
    }
    return ir->rd;
}

static bool is_shift(unsigned op)
{
    switch (op) {
    case rv_insn_slli:
    case rv_insn_srli:
    case rv_insn_srai:
#if RV32_HAS(EXT_C)
    case rv_insn_cslli:
    case rv_insn_csrli:
    case rv_insn_csrai:
#endif
        return true;
    default:
        return false;
    }
}

static void check_register_ops(riscv_t *rv)
{
    static const unsigned ops[] = {
        rv_insn_lui,       rv_insn_addi,      rv_insn_xori,   rv_insn_ori,
        rv_insn_andi,      rv_insn_slti,      rv_insn_sltiu,  rv_insn_slli,
        rv_insn_srli,      rv_insn_srai,      rv_insn_add,    rv_insn_sub,
        rv_insn_xor,       rv_insn_or,        rv_insn_and,    rv_insn_slt,
        rv_insn_sltu,      rv_insn_sll,       rv_insn_srl,    rv_insn_sra,
#if RV32_HAS(EXT_M)
        rv_insn_mul,       rv_insn_mulh,      rv_insn_mulhsu, rv_insn_mulhu,
#endif
#if RV32_HAS(EXT_C)
        rv_insn_cli,       rv_insn_clui,      rv_insn_cmv,    rv_insn_caddi,
        rv_insn_caddi16sp, rv_insn_caddi4spn, rv_insn_cslli,  rv_insn_csrli,
        rv_insn_csrai,     rv_insn_candi,     rv_insn_cadd,   rv_insn_csub,
        rv_insn_cxor,      rv_insn_cor,       rv_insn_cand,
#endif
    };
    static const uint32_t values[][2] = {
        {0, 0},
        {1, 31},
        {0x80000000, UINT32_MAX},
        {0x7fffffff, 1},
        {0xdeadbeef, 0x12345678},
        {UINT32_MAX, 33},
    };
    static const int32_t imms[] = {-1, 0x7ff, -2048, 0x12345000};
    static const int32_t shamts[] = {0, 1, 31};
    unsigned checked = 0;
    for (unsigned o = 0; o < ARRAY_SIZE(ops); o++) {
        const bool shift = is_shift(ops[o]);
        const int32_t *imm = shift ? shamts : imms;
        const unsigned n_imm = shift ? ARRAY_SIZE(shamts) : ARRAY_SIZE(imms);
        for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
            for (unsigned i = 0; i < n_imm; i++) {
                rv_insn_t ir = {.opcode = ops[o], .imm = imm[i]};
                uint32_t a = values[v][0], b = values[v][1];
                unsigned rd = layout(&ir, rv, a, b);
                rv->csr_cycle = 0;
                run(rv, &ir, 1);
                if (rv->X[rd] != reference(ops[o], a, b, imm[i]) ||
                    rv->csr_cycle != 1) {
                    fprintf(stderr, "op %u a=%08x b=%08x imm=%d: %08x\n",
                            ops[o], a, b, imm[i], rv->X[rd]);
                    abort();
                }
                checked++;
            }
        }
    }
    /* AUIPC adds to its own PC, the first record's in run(). */
    rv_insn_t auipc = {.opcode = rv_insn_auipc, .rd = 10, .imm = 0x12345000};
    run(rv, &auipc, 1);
    assert(rv->X[10] == 0x12346000);
    assert(checked > 500);
}

/* Each conditional branch, taken and not, and the compressed jumps. */
static void check_control_flow(riscv_t *rv)
{
    static const struct {
        unsigned op;
        uint32_t a, b;
        bool taken;
    } branches[] = {
        {rv_insn_beq, 5, 5, true},
        {rv_insn_beq, 5, 6, false},
        {rv_insn_bne, 5, 6, true},
        {rv_insn_bne, 5, 5, false},
        {rv_insn_blt, UINT32_MAX, 0, true},
        {rv_insn_blt, 0, UINT32_MAX, false},
        {rv_insn_bge, 0, UINT32_MAX, true},
        {rv_insn_bge, UINT32_MAX, 0, false},
        {rv_insn_bge, 7, 7, true},
        {rv_insn_bltu, 0, UINT32_MAX, true},
        {rv_insn_bltu, UINT32_MAX, 0, false},
        {rv_insn_bgeu, UINT32_MAX, 0, true},
        {rv_insn_bgeu, 0, UINT32_MAX, false},
#if RV32_HAS(EXT_C)
        {rv_insn_cbeqz, 0, 0, true},
        {rv_insn_cbeqz, 1, 0, false},
        {rv_insn_cbnez, 1, 0, true},
        {rv_insn_cbnez, 0, 0, false},
#endif
    };
    for (unsigned i = 0; i < ARRAY_SIZE(branches); i++) {
        rv_insn_t ir = {
            .opcode = branches[i].op, .rs1 = 11, .rs2 = 12, .imm = 0x40};
        unsigned length = 4;
#if RV32_HAS(EXT_C)
        if (ir.opcode == rv_insn_cbeqz || ir.opcode == rv_insn_cbnez) {
            ir.rs2 = 0;
            length = 2;
        }
#endif
        rv->X[11] = branches[i].a;
        rv->X[12] = branches[i].b;
        run(rv, &ir, 1);
        assert(rv->PC == (branches[i].taken ? 0x1040 : 0x1000 + length));
    }
#if RV32_HAS(EXT_C)
    rv_insn_t jump = {.opcode = rv_insn_cj, .imm = -0x20};
    run(rv, &jump, 1);
    assert(rv->PC == 0xfe0);
    jump = (rv_insn_t) {.opcode = rv_insn_cjal, .rd = rv_reg_ra, .imm = 0x80};
    run(rv, &jump, 1);
    assert(rv->PC == 0x1080 && rv->X[rv_reg_ra] == 0x1002);
    /* C.JR and C.JALR clear the low bit; C.JALR links after reading rs1. */
    rv->X[rv_reg_ra] = 0x3001;
    jump = (rv_insn_t) {.opcode = rv_insn_cjr, .rs1 = rv_reg_ra};
    run(rv, &jump, 1);
    assert(rv->PC == 0x3000);
    jump = (rv_insn_t) {
        .opcode = rv_insn_cjalr, .rd = rv_reg_ra, .rs1 = rv_reg_ra};
    run(rv, &jump, 1);
    assert(rv->PC == 0x3000 && rv->X[rv_reg_ra] == 0x1002);
#endif
    /* fuse12 is ADDI + BNE against x0. */
    rv_insn_t countdown = {
        .opcode = rv_insn_fuse12, .rd = 10, .rs1 = 10, .imm = -1, .imm2 = 0x40};
    rv->X[10] = 2;
    rv->csr_cycle = 0;
    run(rv, &countdown, 1);
    assert(rv->X[10] == 1 && rv->PC == 0x1044 && rv->csr_cycle == 2);
    run(rv, &countdown, 1);
    assert(rv->X[10] == 0 && rv->PC == 0x1008);
#if !RV32_HAS(EXT_C)
    /* Without C, a misaligned fused branch target is left to the interpreter,
     * as for a plain branch.
     */
    const int jumps = jump_helper_calls;
    countdown.imm2 = 0x42;
    countdown.impl = jump_helper;
    rv->X[5] = 0x2001;
    run(rv, &countdown, 1);
    assert(jump_helper_calls == jumps + 1 && rv->PC == 0x9000);
#endif

    /* fuse1 (LUIs), fuse7 (ADDIs) and fuse8 (LUI + ADDI) count each member. */
    opcode_fuse_t luis[] = {{.rd = 10, .imm = 0x1000},
                            {.rd = 11, .imm = -4096}};
    rv_insn_t fused = {.opcode = rv_insn_fuse1, .fuse = luis, .imm2 = 2};
    rv->csr_cycle = 0;
    run(rv, &fused, 1);
    assert(rv->X[10] == 0x1000 && rv->X[11] == 0xfffff000);
    assert(rv->csr_cycle == 2);
    opcode_fuse_t addis[] = {{.rd = 10, .rs1 = 10, .imm = 5},
                             {.rd = 12, .rs1 = 10, .imm = -1}};
    fused = (rv_insn_t) {.opcode = rv_insn_fuse7, .fuse = addis, .imm2 = 2};
    run(rv, &fused, 1);
    assert(rv->X[10] == 0x1005 && rv->X[12] == 0x1004);
    fused = (rv_insn_t) {
        .opcode = rv_insn_fuse8, .rd = 13, .imm = 0x12345000, .imm2 = 0x678};
    rv->csr_cycle = 0;
    run(rv, &fused, 1);
    assert(rv->X[13] == 0x12345678 && rv->csr_cycle == 2);
}

#if !RV32_HAS(SYSTEM) || RV32_HAS(SYSTEM_MMIO)
/* 64 bytes of zeroed guest RAM. Translate only while it is attached: code
 * embeds the RAM address.
 */
struct test_ram {
    uint8_t bytes[64];
    memory_t mem;
    vm_attr_t attr;
};

static void attach_ram(riscv_t *rv, struct test_ram *ram)
{
    memset(ram, 0, sizeof(*ram));
    ram->mem =
        (memory_t) {.mem_base = ram->bytes, .mem_size = sizeof(ram->bytes)};
    ram->attr.mem = &ram->mem;
    rv->data = &ram->attr;
#if !RV32_HAS(SYSTEM)
    rv->ram_base = ram->bytes;
#endif
}

static void detach_ram(riscv_t *rv)
{
    rv->data = NULL;
#if !RV32_HAS(SYSTEM)
    rv->ram_base = NULL;
#endif
}

static int group_calls;
static uint32_t group_base;

/* Stands in for the interpreter: the generated code must not have run any
 * member of the group before falling back to it.
 */
static bool group_helper(riscv_t *rv,
                         const rv_insn_t *ir,
                         uint64_t cycle,
                         uint32_t pc)
{
    assert(rv->X[11] == group_base && rv->X[12] == 0x11111111);
    rv->csr_cycle = cycle + (ir->opcode == rv_insn_fuse11 ? 2 : ir->imm2);
    rv->PC = pc + 4 * (ir->opcode == rv_insn_fuse11 ? 2 : ir->imm2);
    group_calls++;
    return true;
}
#endif

#if !RV32_HAS(SYSTEM)
static void check_user_memory(riscv_t *rv)
{
    struct test_ram ram;
    attach_ram(rv, &ram);
    rv->X[8] = 4;
    rv->X[9] = 0x8000ff81;
    rv->csr_cycle = 0;
    /* Signed and unsigned loads; the last overwrites its own base. */
    rv_insn_t widths[] = {
        {.opcode = rv_insn_sw, .rs1 = 8, .rs2 = 9},
        {.opcode = rv_insn_lb, .rd = 10, .rs1 = 8},
        {.opcode = rv_insn_lhu, .rd = 11, .rs1 = 8},
        {.opcode = rv_insn_lh, .rd = 12, .rs1 = 8},
        {.opcode = rv_insn_lw, .rd = 8, .rs1 = 8},
    };
    run(rv, widths, 5);
    assert(rv->X[8] == 0x8000ff81 && rv->X[10] == 0xffffff81);
    assert(rv->X[11] == 0xff81 && rv->X[12] == 0xffffff81);
    assert(rv->csr_cycle == 5);
    helper_exit = false;
    rv_insn_t memory[] = {
        {.opcode = rv_insn_addi, .rd = 5, .rs1 = 0, .imm = 42},
        {.opcode = rv_insn_lw, .rd = 6, .rs1 = 8, .impl = memory_helper},
        {.opcode = rv_insn_addi, .rd = 7, .rs1 = 6, .imm = 1},
    };
    rv->X[8] = 63; /* crosses RAM end; no host out-of-bounds access */
    rv->X[7] = 123;
    rv->csr_cycle = 0;
    run(rv, memory, 3);
    assert(helper_calls == 3 && rv->X[7] == 123 && rv->csr_cycle == 2);

    /* A fast loop that later leaves RAM must report all completed iterations
     * plus its final slow instruction, even with cycles cached in a local.
     */
    rv->X[5] = 42;
    rv->X[8] = 4;
    rv->X[9] = 68;
    rv->csr_cycle = 0;
    rv_insn_t boundary[] = {
        {.opcode = rv_insn_sw, .rs1 = 8, .rs2 = 5, .impl = memory_helper},
        {.opcode = rv_insn_addi, .rd = 8, .rs1 = 8, .imm = 4},
        {.opcode = rv_insn_bne, .rs1 = 8, .rs2 = 9, .imm = -8},
    };
    run(rv, boundary, 3);
    assert(rv->X[8] == 64 && rv->csr_cycle == 46 && helper_calls == 4);

    /* Remaining widths, and the compressed and SP-relative forms. */
    memset(ram.bytes, 0, sizeof(ram.bytes));
    rv->X[8] = 8;
    rv->X[9] = 0x80c0ffee;
    rv_insn_t access[] = {
        {.opcode = rv_insn_sb, .rs1 = 8, .rs2 = 9, .imm = 1},
        {.opcode = rv_insn_sh, .rs1 = 8, .rs2 = 9, .imm = 2},
        {.opcode = rv_insn_lbu, .rd = 10, .rs1 = 8, .imm = 1},
        {.opcode = rv_insn_lw, .rd = 11, .rs1 = 8},
    };
    run(rv, access, 4);
    assert(rv->X[10] == 0xee && rv->X[11] == 0xffeeee00);
#if RV32_HAS(EXT_C)
    rv->X[rv_reg_sp] = 16;
    rv_insn_t compressed[] = {
        {.opcode = rv_insn_cswsp, .rs1 = rv_reg_sp, .rs2 = 9, .imm = 4},
        {.opcode = rv_insn_clwsp, .rd = 12, .rs1 = rv_reg_sp, .imm = 4},
        {.opcode = rv_insn_csw, .rs1 = 8, .rs2 = 12, .imm = 16},
        {.opcode = rv_insn_clw, .rd = 13, .rs1 = 8, .imm = 16},
    };
    rv->csr_cycle = 0;
    run(rv, compressed, 4);
    assert(rv->X[12] == 0x80c0ffee && rv->X[13] == 0x80c0ffee);
    assert(rv->csr_cycle == 4);
#endif

    /* fuse3 stores and fuse4 loads, including halfword groups, run inline and
     * count each member.
     */
    group_base = 32;
    rv->X[11] = group_base;
    rv->X[12] = 0x11111111;
    rv->X[13] = 0x22222222;
    opcode_fuse_t stores[] = {
        {.opcode = rv_insn_sw, .rs1 = 11, .rs2 = 12, .imm = 0},
        {.opcode = rv_insn_sw, .rs1 = 11, .rs2 = 13, .imm = 4},
    };
    rv_insn_t group = {.opcode = rv_insn_fuse3,
                       .fuse = stores,
                       .imm2 = 2,
                       .impl = group_helper};
    rv->csr_cycle = 0;
    run(rv, &group, 1);
    uint32_t word;
    memcpy(&word, ram.bytes + 36, 4);
    assert(word == 0x22222222 && rv->csr_cycle == 2 && !group_calls);
    /* Mixed widths, and a load into x0 that is performed but dropped. */
    opcode_fuse_t loads[] = {
        {.opcode = rv_insn_lw, .rd = 14, .rs1 = 11, .imm = 4},
        {.opcode = rv_insn_lhu, .rd = 15, .rs1 = 11, .imm = 2},
        {.opcode = rv_insn_lw, .rd = 0, .rs1 = 11, .imm = 0},
    };
    group = (rv_insn_t) {.opcode = rv_insn_fuse4,
                         .fuse = loads,
                         .imm2 = 3,
                         .impl = group_helper};
    rv->csr_cycle = 0;
    run(rv, &group, 1);
    assert(rv->X[14] == 0x22222222 && rv->X[15] == 0x1111);
    assert(rv->X[0] == 0 && rv->csr_cycle == 3 && !group_calls);

    /* One member outside RAM: the whole group replays through the helper, which
     * checks that no member ran first.
     */
    stores[1].imm = 30; /* 62..65 crosses the end of RAM */
    group = (rv_insn_t) {.opcode = rv_insn_fuse3,
                         .fuse = stores,
                         .imm2 = 2,
                         .impl = group_helper};
    memset(ram.bytes + 32, 0, 4);
    rv->csr_cycle = 0;
    run(rv, &group, 1);
    memcpy(&word, ram.bytes + 32, 4);
    assert(group_calls == 1 && !word && rv->csr_cycle == 2);
    /* A member loading a later member's base cannot be guarded up front. */
    loads[0].rd = 11;
    group = (rv_insn_t) {.opcode = rv_insn_fuse4,
                         .fuse = loads,
                         .imm2 = 3,
                         .impl = group_helper};
    run(rv, &group, 1);
    assert(group_calls == 2);
    /* Misaligned members need the interpreter's trap or emulation. */
    loads[0] =
        (opcode_fuse_t) {.opcode = rv_insn_lw, .rd = 14, .rs1 = 11, .imm = 1};
    run(rv, &group, 1);
    assert(group_calls == 3);

    /* fuse11 (LW + ADDI) and fuse13 (SW + ADDI) advance the base. */
    rv->X[11] = group_base;
    rv_insn_t post = {.opcode = rv_insn_fuse13,
                      .rs1 = 11,
                      .rs2 = 13,
                      .imm = 8,
                      .imm2 = 4,
                      .impl = group_helper};
    rv->csr_cycle = 0;
    run(rv, &post, 1);
    memcpy(&word, ram.bytes + 40, 4);
    assert(word == 0x22222222 && rv->X[11] == 36 && rv->csr_cycle == 2);
    post = (rv_insn_t) {.opcode = rv_insn_fuse11,
                        .rd = 14,
                        .rs1 = 11,
                        .imm = 4,
                        .imm2 = -4,
                        .impl = group_helper};
    rv->X[14] = 0;
    run(rv, &post, 1);
    assert(rv->X[14] == 0x22222222 && rv->X[11] == 32);
    group_base = rv->X[11] = 60; /* 64..67 is outside RAM */
    run(rv, &post, 1);
    assert(group_calls == 4 && rv->X[11] == 60);
    detach_ram(rv);
}
#endif

#if RV32_HAS(SYSTEM_MMIO)
/* Single and grouped accesses through the dTLB guard. */
static void check_system_memory(riscv_t *rv)
{
    struct test_ram ram;
    attach_ram(rv, &ram);
    rv->X[5] = 42;
    uint32_t value = 0x87654321;
    memcpy(ram.bytes + 4, &value, 4);
    const uint32_t va = 0x40001004;
    tlb_entry_t *mapping = &rv->dtlb[(va >> 12) & TLB_MASK];
    *mapping = (tlb_entry_t) {
        .vpn = va >> 12, .ppn = 0, .valid = 1, .dirty = 1, .perm = 6};
    rv_insn_t load = {
        .opcode = rv_insn_lw, .rd = 6, .rs1 = 8, .impl = memory_helper};
    rv->X[8] = va;
    int before = helper_calls;
    rv->csr_cycle = 0;
    run(rv, &load, 1);
    assert(rv->X[6] == value && helper_calls == before && rv->csr_cycle == 1);
    /* Every gating condition must take the original instruction's slow path. */
    helper_exit = false;
    for (unsigned condition = 0; condition < 6; condition++) {
        *mapping = (tlb_entry_t) {
            .vpn = va >> 12, .ppn = 0, .valid = 1, .dirty = 1, .perm = 6};
        rv->X[8] = va;
        load.opcode = rv_insn_lw;
        if (condition == 0)
            mapping->valid = 0;
        if (condition == 1)
            mapping->vpn++;
        if (condition == 2)
            mapping->perm = 0;
        if (condition == 3)
            mapping->ppn = 0x10000000; /* MMIO/outside RAM */
        if (condition == 4)
            rv->X[8]++;
        if (condition == 5) {
            load.opcode = rv_insn_sw;
            load.rs2 = 5;
            mapping->dirty = 0;
        }
        run(rv, &load, 1);
        assert(helper_calls == before + (int) condition + 1);
    }

    /* Grouped accesses use the same guard. */
    const uint32_t group_va = 0x40002020;
    mapping = &rv->dtlb[(group_va >> 12) & TLB_MASK];
    *mapping = (tlb_entry_t) {
        .vpn = group_va >> 12, .ppn = 0, .valid = 1, .dirty = 1, .perm = 6};
    group_base = rv->X[11] = group_va;
    rv->X[12] = 0x11111111;
    opcode_fuse_t stores[] = {
        {.opcode = rv_insn_sw, .rs1 = 11, .rs2 = 12, .imm = 0},
        {.opcode = rv_insn_sw, .rs1 = 11, .rs2 = 12, .imm = 4},
    };
    rv_insn_t group = {.opcode = rv_insn_fuse3,
                       .fuse = stores,
                       .imm2 = 2,
                       .impl = group_helper};
    run(rv, &group, 1);
    uint32_t word;
    memcpy(&word, ram.bytes + 0x24, 4);
    assert(word == 0x11111111 && !group_calls);
    mapping->dirty = 0; /* a clean page needs the interpreter's PTE update */
    memset(ram.bytes, 0, sizeof(ram.bytes));
    run(rv, &group, 1);
    memcpy(&word, ram.bytes + 0x20, 4);
    assert(group_calls == 1 && !word);
    group_calls = 0;
    detach_ram(rv);
}
#endif

int main(void)
{
    riscv_t *rv = calloc(1, sizeof(*rv));
    assert(rv);
    rv->jit_state = jit_state_init(4 * 1024 * 1024, 0);
    assert(rv->jit_state);

    /* Source/destination aliasing, wrapping arithmetic, signed comparisons,
     * zero-register writes, shift masking, and precise 64-bit cycle carries.
     */
    rv->X[5] = 0x80000001;
    rv->X[6] = 33;
    rv->csr_cycle = UINT32_MAX - 1;
    rv_insn_t alu[] = {
        {.opcode = rv_insn_sra, .rd = 5, .rs1 = 5, .rs2 = 6},
        {.opcode = rv_insn_addi, .rd = 5, .rs1 = 5, .imm = -1},
        {.opcode = rv_insn_slti, .rd = 7, .rs1 = 5, .imm = 0},
        {.opcode = rv_insn_addi, .rd = 0, .rs1 = 5, .imm = 1},
    };
    run(rv, alu, 4);
    assert(rv->X[5] == 0xbfffffff && rv->X[7] == 1 && rv->X[0] == 0);
    assert(rv->PC == 0x1010 && rv->csr_cycle == UINT64_C(0x100000002));

#if RV32_HAS(EXT_M)
    const uint32_t operands[][2] = {
        {0x80000000, UINT32_MAX}, {0x80000000, 0}, {7, 0},
        {0xfffffff9, 3},          {0x80000000, 7}, {UINT32_MAX, UINT32_MAX},
    };
    const unsigned operations[] = {rv_insn_div, rv_insn_divu, rv_insn_rem,
                                   rv_insn_remu};
    for (unsigned i = 0; i < ARRAY_SIZE(operands); i++) {
        uint32_t a = operands[i][0], b = operands[i][1];
        uint32_t expected[] = {rv_div((int32_t) a, b), b ? a / b : UINT32_MAX,
                               rv_rem((int32_t) a, b), b ? a % b : a};
        for (unsigned j = 0; j < 4; j++) {
            rv->X[5] = a;
            rv->X[6] = b;
            rv_insn_t div = {
                .opcode = operations[j], .rd = 5, .rs1 = 5, .rs2 = 6};
            run(rv, &div, 1);
            assert(rv->X[5] == expected[j]);
        }
    }
#endif

    rv->X[5] = 100;
    rv->csr_cycle = 0;
    rv_insn_t loop[] = {
        {.opcode = rv_insn_addi, .rd = 5, .rs1 = 5, .imm = -1},
        {.opcode = rv_insn_bne, .rs1 = 5, .rs2 = 0, .imm = -4},
    };
    run(rv, loop, 2);
    assert(rv->X[5] == 0 && rv->PC == 0x1008 && rv->csr_cycle == 200);
    rv->X[5] = 1000;
    rv->csr_cycle = 0;
    run(rv, loop, 2);
    assert(rv->X[5] == 744 && rv->PC == 0x1000 && rv->csr_cycle == 512);

    /* Two compiled blocks must follow hot edges without the LFU fixture.
     * Retiring the first block then forbids re-entry, even if its old IR is
     * overwritten as it would be when the decoder pool reuses it.
     */
    rv_insn_t edge_a[] = {
        {.opcode = rv_insn_addi, .rd = 5, .rs1 = 5, .imm = 1},
        {.opcode = rv_insn_jal, .imm = 0xfc},
    };
    rv_insn_t edge_b[] = {
        {.opcode = rv_insn_addi, .rd = 6, .rs1 = 6, .imm = 3},
        {.opcode = rv_insn_jal, .imm = -0x104},
    };
    block_t first = make_block(edge_a, 2, 0x1000);
    block_t second = make_block(edge_b, 2, 0x1100);
    active = &first;
    assert(jit_translate(rv, &first));
    active = &second;
    assert(jit_translate(rv, &second));
    cycle_slice = 128; /* 64 two-cycle blocks */
    rv->X[5] = rv->X[6] = 0;
    rv->csr_cycle = 0;
    jit_wasm_run(rv, &first);
    assert(rv->X[5] == 32 && rv->X[6] == 96 && rv->csr_cycle == 128);
    unsigned retired =
        ((struct jit_state *) rv->jit_state)->maps[first.offset - 1].entry;
    jit_wasm_retire_block(rv, &first);
    assert(EM_ASM_INT({ return wasmTable.get($0) == null; }, retired));
    memset(edge_a, 0xa5, sizeof(edge_a));
    jit_wasm_run(rv, &second);
    assert(rv->X[5] == 32 && rv->X[6] == 99 && rv->csr_cycle == 130);
    jit_wasm_retire_block(rv, &second);

    /* A helper that halts the hart ends the chain, even though it falls through
     * to a compiled successor.
     */
    rv_insn_t halting[] = {
        {.opcode = rv_insn_lw, .rd = 6, .impl = halt_helper},
        {.opcode = rv_insn_jal, .imm = 0x100},
    };
    rv_insn_t after_halt = {
        .opcode = rv_insn_addi, .rd = 7, .rs1 = 7, .imm = 1};
    block_t halt_block = make_block(halting, 2, 0x1200);
    block_t next_block = make_block(&after_halt, 1, 0x1304);
    active = &halt_block;
    assert(jit_translate(rv, &halt_block));
    active = &next_block;
    assert(jit_translate(rv, &next_block));
    rv->X[7] = 0;
    rv->csr_cycle = 0;
    jit_wasm_run(rv, &next_block); /* publish its lookup entry */
    assert(rv->X[7] == 1);
    jit_wasm_run(rv, &halt_block);
    assert(rv->halt && rv->X[7] == 1 && rv->PC == 0x1304);
    rv->halt = false;
    jit_wasm_retire_block(rv, &next_block);
    jit_wasm_retire_block(rv, &halt_block);
    cycle_slice = 0;
    active = NULL;

    /* JALR must read its source before writing an aliased link register. */
    rv->X[5] = 0x2001;
    rv_insn_t jump = {.opcode = rv_insn_jalr, .rd = 5, .rs1 = 5, .imm = 3};
    run(rv, &jump, 1);
    assert(rv->X[5] == 0x1004 && rv->PC == 0x2004);
#if !RV32_HAS(EXT_C)
    /* Without C, only a misaligned target reaches the interpreter, and it must
     * see the state before the jump.
     */
    rv->X[5] = 0x2001;
    jump.imm = 1;
    jump.impl = jump_helper;
    run(rv, &jump, 1);
    assert(jump_helper_calls == 1 && rv->X[5] == 0x2001 && rv->PC == 0x9000);
#endif

    /* A helper whose interpreter chain paused must end the generated code, even
     * when its PC matches the next record, so rv_step resumes once.
     */
    rv_insn_t paused[] = {
        {.opcode = rv_insn_lw, .rd = 6, .impl = paused_helper},
        {.opcode = rv_insn_addi, .rd = 7, .rs1 = 0, .imm = 1},
    };
    rv->X[7] = 123;
    rv->csr_cycle = 0;
    run(rv, paused, 2);
    assert(rv->X[7] == 123 && rv->next_insn == &paused_insn);
    assert(rv->PC == 0x1004 && rv->csr_cycle == 1);
    rv->next_insn = NULL;

    /* Fused shifts still count each original instruction and use its own
     * immediate. SRAI's encoded high bits must not affect the shift amount.
     */
    opcode_fuse_t shifts[] = {
        {.opcode = rv_insn_slli, .rd = 5, .rs1 = 5, .imm = 1},
        {.opcode = rv_insn_srai, .rd = 6, .rs1 = 5, .imm = 0x401},
    };
    rv_insn_t fused = {.opcode = rv_insn_fuse5, .fuse = shifts, .imm2 = 2};
    rv->X[5] = 0x40000000;
    rv->csr_cycle = 0;
    run(rv, &fused, 1);
    assert(rv->X[5] == 0x80000000 && rv->X[6] == 0xc0000000);
    assert(rv->csr_cycle == 2);

    rv_insn_t memory[] = {
        {.opcode = rv_insn_addi, .rd = 5, .rs1 = 0, .imm = 42},
        {.opcode = rv_insn_lw, .rd = 6, .impl = memory_helper},
        {.opcode = rv_insn_addi, .rd = 7, .rs1 = 6, .imm = 1},
    };
    rv->csr_cycle = 0;
    run(rv, memory, 3);
    assert(helper_calls == 1 && rv->X[7] == 44 && rv->csr_cycle == 3);
    helper_exit = true;
    rv->X[7] = 123;
    rv->csr_cycle = 0;
    run(rv, memory, 3);
    assert(helper_calls == 2 && rv->X[7] == 123 && rv->PC == 0x9000);
    assert(rv->csr_cycle == 2);


    check_register_ops(rv);
    check_control_flow(rv);
#if !RV32_HAS(SYSTEM)
    check_user_memory(rv);
#endif
#if RV32_HAS(SYSTEM_MMIO)
    check_system_memory(rv);
#endif

#if RV32_HAS(SYSTEM)
    rv_insn_t probe = {.opcode = rv_insn_nop};
    block_t probe_block = make_block(&probe, 1, 0x1000);
    active = &probe_block;
    assert(jit_translate(rv, &probe_block));
    assert(jit_page_has_code(rv->jit_state, 0x1004, 0));
    jit_retire_space(rv->jit_state, 0);
    assert(!jit_page_has_code(rv->jit_state, 0x1004, 0));
    jit_wasm_retire_block(rv, &probe_block);

    /* Modules on one page share a bucket; retiring any of them, in any order,
     * keeps the others findable.
     */
    rv_insn_t chained[3] = {{.opcode = rv_insn_nop},
                            {.opcode = rv_insn_nop},
                            {.opcode = rv_insn_nop}};
    block_t chain[3];
    for (int i = 0; i < 3; i++) {
        chain[i] = make_block(&chained[i], 1, 0x3000 + 0x100 * i);
        active = &chain[i];
        assert(jit_translate(rv, &chain[i]));
    }
    jit_wasm_retire_block(rv, &chain[1]); /* middle of the chain */
    assert(jit_page_has_code(rv->jit_state, 0x3ffc, 0));
    jit_wasm_retire_block(rv, &chain[2]); /* its head */
    assert(jit_page_has_code(rv->jit_state, 0x3000, 0));
    jit_wasm_retire_block(rv, &chain[0]);
    assert(!jit_page_has_code(rv->jit_state, 0x3000, 0));
    /* Retiring the address space empties a bucket holding several modules. */
    for (int i = 0; i < 3; i++) {
        active = &chain[i];
        assert(jit_translate(rv, &chain[i]));
    }
    jit_retire_space(rv->jit_state, 0);
    assert(!jit_page_has_code(rv->jit_state, 0x3000, 0));
    for (int i = 0; i < 3; i++)
        assert(!chain[i].hot);
    active = NULL;
    active = NULL;
#endif
    jit_state_exit(rv->jit_state);
    rv->jit_state = jit_state_init(512, 0);
    assert(rv->jit_state);
    rv_insn_t small = {.opcode = rv_insn_addi, .rd = 5, .rs1 = 0, .imm = 42};
    /* Retired modules return their map slot and code bytes. */
    for (int i = 0; i < 20; i++)
        run(rv, &small, 1);
    struct jit_state *state = rv->jit_state;
    assert(state->n_blocks == 1 && !state->offset);
    /* Live modules exhaust the code budget, which resets the whole cache. */
    block_t live[20];
    for (int i = 0; i < 20; i++) {
        live[i] = make_block(&small, 1, 0x1000);
        active = &live[i];
        assert(jit_translate(rv, &live[i]));
    }
    assert(state->n_blocks < 20);
    block_t held = make_block(&small, 1, 0x1000);
    active = &held;
    assert(jit_translate(rv, &held));
    unsigned entry = state->maps[held.offset - 1].entry;
    jit_state_exit(state);
    active = NULL;
    assert(EM_ASM_INT({ return wasmTable.get($0) == null; }, entry));

    /* A browser policy denying runtime compilation keeps the VM usable. Once a
     * module was installed, a failure rejects only its own block.
     */
    rv->jit_state = jit_state_init(4096, 0);
    block_t installed = make_block(&small, 1, 0x1000);
    active = &installed;
    assert(jit_translate(rv, &installed));
    EM_ASM({
        Module.savedWasmModule = WebAssembly.Module;
        WebAssembly.Module = function()
        {
            throw new Error('blocked');
        };
    });
    block_t denied = make_block(&small, 1, 0x1000);
    active = &denied;
    assert(!jit_translate(rv, &denied));
    assert(!denied.hot && !denied.translatable);
    assert(!((struct jit_state *) rv->jit_state)->disabled);
    jit_state_exit(rv->jit_state);
    rv->jit_state = jit_state_init(4096, 0);
    denied = make_block(&small, 1, 0x1000);
    assert(!jit_translate(rv, &denied));
    assert(!denied.hot && ((struct jit_state *) rv->jit_state)->disabled);
    EM_ASM({ WebAssembly.Module = Module.savedWasmModule; });
    jit_state_exit(rv->jit_state);
    free(rv);
    puts("Wasm T1C backend OK");
    return 0;
}
