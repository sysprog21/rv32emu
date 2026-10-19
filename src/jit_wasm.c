/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* T1C backend for Emscripten builds, compiled in place of jit.c. It
 * instantiates small modules importing the emulator's linear memory and puts
 * their exports in the indirect function table. The byte emitter and T1C
 * lowering here are independent implementations.
 *
 * Registers live in Wasm locals between observable operations. Instructions
 * without a direct lowering use one detached interpreter record, preserving
 * memory callbacks, MMU faults, misalignment emulation and fused semantics. IR
 * remains owned by the block cache; generated functions are never reused by a
 * replacement block, even when it has the same PC.
 */

#include <assert.h>
#include <emscripten.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "decode.h"
#include "jit.h"
#include "riscv.h"
#include "riscv_private.h"
#if RV32_HAS(SYSTEM_MMIO)
#include "system.h"
#define WASM_TLB_SHIFT 4 /* log2 of the dTLB entry size */
_Static_assert(sizeof(tlb_entry_t) == 1 << WASM_TLB_SHIFT,
               "Wasm dTLB stride must match WASM_TLB_SHIFT");
_Static_assert(RV_PG_SHIFT == 12, "Wasm dTLB assumes 4 KiB pages");
#endif

#if !RV32_HAS(JIT) || !defined(__EMSCRIPTEN__)
#error "jit_wasm.c is the T1C backend of Emscripten JIT builds."
#endif
_Static_assert(sizeof(void *) == 4, "lookup entries hold wasm32 addresses");
#define WASM_LOOKUP_SHIFT 2 /* log2 of a lookup entry's size */
_Static_assert(sizeof(uintptr_t) == 1 << WASM_LOOKUP_SHIFT,
               "lookup stride must match WASM_LOOKUP_SHIFT");

#define WASM_MAX_BLOCKS 4096
_Static_assert(WASM_MAX_BLOCKS <= UINT16_MAX + 1, "free_maps holds uint16_t");
#define WASM_BODY_SIZE (256 * 1024)
#define WASM_LOOP_LIMIT 256    /* self-loop iterations per call */
#ifndef WASM_CYCLE_SLICE       /* the test fixture overrides it */
#define WASM_CYCLE_SLICE 65536 /* guest cycles chained per entry from C */
#endif
#ifdef JIT_WASM_PROFILE
#define WASM_PROFILE_ENABLED 1
#else
#define WASM_PROFILE_ENABLED 0
#endif

/* Function locals after the rv parameter (local 0): guest x1..x31 use their
 * register numbers, then scratch values, then one RAM offset per member of a
 * fused load or store group.
 */
#define WASM_ADDR 32    /* RAM offset of the current access */
#define WASM_NEXT_PC 33 /* PC after the block */
#define WASM_BUDGET 34  /* self-loop iterations */
#define WASM_CYCLE 35   /* i64 cycle counter of helper-free loops */
#define WASM_TLB 36     /* dTLB entry */
#define WASM_VPN 37     /* page number */
#define WASM_GROUP 38   /* first of FUSE_MAX_ENTRIES group offsets */

/* wasm_tail() runs after the loop and every access, so it reuses three of those
 * locals.
 */
#define WASM_TAIL_SATP WASM_ADDR
#define WASM_TAIL_MAP WASM_TLB
#define WASM_TAIL_FUNC WASM_VPN

/* Edge lookup: hash of the target PC and address space. wasm_tail() emits the
 * same computation, so the two must change together.
 */
#define WASM_LOOKUP_BITS 13
#define WASM_SLOT_SHIFT_LO 1
#define WASM_SLOT_SHIFT_HI 11
#define WASM_PAGE_BITS 10
#define WASM_SATP_MIX 0x9e3779b1U /* golden-ratio multiplier */

/* WebAssembly opcodes and types used by the emitter. */
enum {
    W_VOID = 0x40, /* empty block type */
    W_TYPE_I32 = 0x7f,
    W_TYPE_I64 = 0x7e,
    W_LOOP = 0x03,
    W_IF = 0x04,
    W_ELSE = 0x05,
    W_END = 0x0b,
    W_BR_IF = 0x0d,
    W_RETURN = 0x0f,
    W_CALL = 0x10,
    W_RETURN_CALL_INDIRECT = 0x13,
    W_DROP = 0x1a,
    W_SELECT = 0x1b,
    W_LOCAL_GET = 0x20,
    W_LOCAL_SET = 0x21,
    W_LOCAL_TEE = 0x22,
    W_I32_LOAD = 0x28,
    W_I64_LOAD = 0x29,
    W_I32_LOAD8_S = 0x2c,
    W_I32_LOAD8_U = 0x2d,
    W_I32_LOAD16_S = 0x2e,
    W_I32_LOAD16_U = 0x2f,
    W_I32_STORE = 0x36,
    W_I64_STORE = 0x37,
    W_I32_STORE8 = 0x3a,
    W_I32_STORE16 = 0x3b,
    W_I32_CONST = 0x41,
    W_I64_CONST = 0x42,
    W_I32_EQZ = 0x45,
    W_I32_EQ = 0x46,
    W_I32_NE = 0x47,
    W_I32_LT_S = 0x48,
    W_I32_LT_U = 0x49,
    W_I32_GT_U = 0x4b,
    W_I32_GE_S = 0x4e,
    W_I32_GE_U = 0x4f,
    W_I64_LT_U = 0x54,
    W_I32_ADD = 0x6a,
    W_I32_SUB = 0x6b,
    W_I32_MUL = 0x6c,
    W_I32_DIV_S = 0x6d,
    W_I32_DIV_U = 0x6e,
    W_I32_REM_S = 0x6f,
    W_I32_REM_U = 0x70,
    W_I32_AND = 0x71,
    W_I32_OR = 0x72,
    W_I32_XOR = 0x73,
    W_I32_SHL = 0x74,
    W_I32_SHR_S = 0x75,
    W_I32_SHR_U = 0x76,
    W_I64_ADD = 0x7c,
    W_I64_MUL = 0x7e,
    W_I64_SHR_U = 0x88,
    W_I32_WRAP_I64 = 0xa7,
    W_I64_EXTEND_I32_S = 0xac,
    W_I64_EXTEND_I32_U = 0xad,
};

/* clang-format off */
EM_JS(int, wasm_install, (const uint8_t *body, unsigned size, int helper,
                           uint32_t pc, uint32_t satp, int profile), {
    function leb(n) {
        const bytes = [];
        do {
            const b = n & 127;
            n >>>= 7;
            bytes.push(b | (n ? 128 : 0));
        } while (n);
        return bytes;
    }
    function name(s) {
        return [s.length, ...Array.from(s, c => c.charCodeAt(0))];
    }
    const bytes = [ 0, 97, 115, 109, 1, 0, 0, 0 ];
    function section(id, data) {
        bytes.push(id, ...leb(data.length), ...data);
    }
    /* (rv, ir) -> status; (rv) -> void. */
    section(1, [ 2, 96, 2, 127, 127, 1, 127, 96, 1, 127, 0 ]);
    const shared = typeof SharedArrayBuffer != 'undefined' &&
                   wasmMemory.buffer instanceof SharedArrayBuffer;
    section(2, [
        3, ...name('env'), ...name('memory'), 2, shared ? 3 : 1, 0,
        ...leb(65536), ...name('env'), ...name('step'), 0, 0,
        ...name('env'), ...name('table'), 1, 0x70, 0, 0
    ]);
    section(3, [ 1, 1 ]);
    section(7, [ 1, ...name('run'), 0, 1 ]);
    /* The code section header precedes the body, copied in below. */
    const body_size = leb(size);
    bytes.push(10, ...leb(1 + body_size.length + size), 1, ...body_size);
    const prefix = bytes.splice(0);
    const head = prefix.length;
    if (profile) {
        const label = 'rv32_' + (pc >>> 0).toString(16).padStart(8, '0') +
                      '_' + (satp >>> 0).toString(16);
        const names = [1, 1, ...name(label)];
        section(0, [...name('name'), 1, ...leb(names.length), ...names]);
    }
    try {
        /* Copy out of the heap: module compilation must also work with shared
         * RAM.
         */
        const out = new Uint8Array(head + size + bytes.length);
        out.set(prefix);
        out.set(HEAPU8.subarray(body, body + size), head);
        out.set(bytes, head + size);
        const module = new WebAssembly.Module(out);
        const instance = new WebAssembly.Instance(
            module,
            {env : {memory : wasmMemory, step : wasmTable.get(helper), table : wasmTable}});
        return addFunction(instance.exports.run, 'vi');
    } catch (e) {
        /* CSP and engines without runtime compilation keep using the
         * interpreter.
         */
        return 0;
    }
});

/* removeFunction also clears the table slot, so the instance and its compiled
 * code can be collected before the index is reused.
 */
EM_JS(void, wasm_remove, (int entry), { removeFunction(entry); });

/* clang-format on */

/* Counters are per VM and survive code-cache resets. Helper counts describe
 * calls, rather than retired instructions (one fused record can contain many).
 */
struct wasm_profile {
    uint64_t calls, cycles, blocks, bytes, loops, lookup_hits, lookup_misses;
    uint64_t helpers[rv_insn_fuse13 + 1];
    double translate_ms, module_ms;
};

static void wasm_profile_dump(const struct wasm_profile *profile)
{
    static const char *const names[] = {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = #inst,
        RV_INSN_LIST
#undef _
#define _(inst) [rv_insn_##inst] = #inst,
            FUSE_INSN_LIST
#undef _
    };
    fprintf(stderr,
            "WASM T1C profile: {\"calls\":%" PRIu64 ",\"cycles\":%" PRIu64
            ",\"blocks\":%" PRIu64 ",\"bytes\":%" PRIu64
            ",\"loop_blocks\":%" PRIu64 ",\"lookup_hits\":%" PRIu64
            ",\"lookup_misses\":%" PRIu64
            ",\"translate_ms\":%.3f,\"module_ms\":%.3f,\"helpers\":{",
            profile->calls, profile->cycles, profile->blocks, profile->bytes,
            profile->loops, profile->lookup_hits, profile->lookup_misses,
            profile->translate_ms, profile->module_ms);
    bool first = true;
    for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
        if (!profile->helpers[i])
            continue;
        fprintf(stderr, "%s\"%s\":%" PRIu64, first ? "" : ",", names[i],
                profile->helpers[i]);
        first = false;
    }
    fputs("}}\n", stderr);
}

/* One installed module. While its block is hot, block->offset holds the
 * one-based map index; edge lookups hold map addresses.
 */
struct wasm_map {
    uint32_t pc;
    uint32_t entry; /* function-table index; zero once retired */
    uint32_t size;  /* body bytes charged to the code budget */
    block_t *owner; /* NULL once retired */
#if RV32_HAS(SYSTEM)
    uint32_t satp;
    uint16_t next_in_page; /* one-based index of the next map in its bucket */
#endif
};

struct jit_state {
    struct wasm_map maps[WASM_MAX_BLOCKS];

    /* Translation buffer for one module body, allocated apart so that the bytes
     * no translation reaches are never touched.
     */
    uint8_t *scratch;
    uint32_t lookup[1 << WASM_LOOKUP_BITS]; /* struct wasm_map addresses */
#if RV32_HAS(SYSTEM)
    /* One-based index of the first live map per (address space, page) bucket,
     * so that a page flush visits only the maps that may match.
     */
    uint16_t pages[1 << WASM_PAGE_BITS];
#endif
    uint32_t size, offset; /* code budget and live body bytes */
    int n_blocks;          /* maps ever used since the last reset */
    int n_free;            /* retired maps ready for reuse, in free_maps */
    uint16_t free_maps[WASM_MAX_BLOCKS];
    bool disabled;
    bool installed;              /* some module was ever installed */
    struct wasm_profile profile; /* updated only under JIT_WASM_PROFILE */
};

/* A helper must not follow the interpreter's intra- or inter-block chains. Its
 * caller materializes registers and cycles before making this call. If the
 * interpreter still paused a chain, rv_step resumes it from next_insn, so the
 * generated code must leave rather than run the same records again.
 */
static int wasm_step(riscv_t *rv, const rv_insn_t *original)
{
    if (WASM_PROFILE_ENABLED) {
        struct jit_state *state = rv->jit_state;
        state->profile.helpers[original->opcode]++;
    }
    rv_insn_t ir = *original;
    ir.next = ir.branch_taken = ir.branch_untaken = NULL;
    rv->PC = ir.pc;
    const bool ok = ir.impl(rv, &ir, rv->csr_cycle, ir.pc) && !rv->next_insn;

    /* Generated tails chain only below the cycle limit; a halt must end the
     * chain even when the helper falls through to its successor.
     */
    if (rv->halt)
        rv->wasm_jit_limit = 0;
    return ok;
}

struct wasm_emitter {
    uint8_t *code;
    uint32_t size;
    uint32_t loaded, dirty, inputs;
    uint32_t cycles;
    bool failed;
    riscv_t *rv;
    bool keep_locals;
};

static void wasm_byte(struct wasm_emitter *e, uint8_t byte)
{
    if (e->size == WASM_BODY_SIZE) {
        e->failed = true;
        return;
    }
    e->code[e->size++] = byte;
}

static void wasm_uleb(struct wasm_emitter *e, uint32_t value)
{
    do {
        uint8_t byte = value & 127;
        value >>= 7;
        wasm_byte(e, byte | (value ? 128 : 0));
    } while (value);
}

static void wasm_sleb(struct wasm_emitter *e, int64_t value)
{
    bool more;
    do {
        uint8_t byte = value & 127;
        value >>= 7;
        more = !((value == 0 && !(byte & 64)) || (value == -1 && (byte & 64)));
        wasm_byte(e, byte | (more ? 128 : 0));
    } while (more);
}

static void wasm_const(struct wasm_emitter *e, int32_t value)
{
    wasm_byte(e, W_I32_CONST);
    wasm_sleb(e, value);
}

static void wasm_const64(struct wasm_emitter *e, int64_t value)
{
    wasm_byte(e, W_I64_CONST);
    wasm_sleb(e, value);
}

static void wasm_local(struct wasm_emitter *e, uint8_t op, unsigned local)
{
    wasm_byte(e, op);
    wasm_uleb(e, local);
}

static void wasm_lget(struct wasm_emitter *e, unsigned local)
{
    wasm_local(e, W_LOCAL_GET, local);
}

static void wasm_lset(struct wasm_emitter *e, unsigned local)
{
    wasm_local(e, W_LOCAL_SET, local);
}

static void wasm_ltee(struct wasm_emitter *e, unsigned local)
{
    wasm_local(e, W_LOCAL_TEE, local);
}

/* A load or store with its natural alignment hint. */
static void wasm_mem(struct wasm_emitter *e, uint8_t op, unsigned offset)
{
    wasm_byte(e, op);

    /* A hint above the natural alignment fails validation. Guest-width accesses
     * use wasm_ram_access() instead.
     */
    unsigned align = 2;
    if (op == W_I64_LOAD || op == W_I64_STORE)
        align = 3;
    else if (op == W_I32_LOAD8_U)
        align = 0;
    else
        assert(op == W_I32_LOAD || op == W_I32_STORE);
    wasm_uleb(e, align);
    wasm_uleb(e, offset);
}

/* Load a field of riscv_t. */
static void wasm_rv_load(struct wasm_emitter *e, uint8_t op, unsigned offset)
{
    wasm_lget(e, 0);
    wasm_mem(e, op, offset);
}

/* Open a structured block, loop or if with a block type. */
static void wasm_open(struct wasm_emitter *e, uint8_t op, uint8_t type)
{
    wasm_byte(e, op);
    wasm_byte(e, type);
}

static void wasm_if(struct wasm_emitter *e)
{
    wasm_open(e, W_IF, W_VOID);
}

static void wasm_get(struct wasm_emitter *e, unsigned reg)
{
    if (!reg) {
        wasm_const(e, 0);
        return;
    }
    assert(reg < N_RV_REGS);
    if (!(e->loaded & (1U << reg))) {
        e->inputs |= 1U << reg;
        wasm_rv_load(e, W_I32_LOAD, offsetof(riscv_t, X) + 4 * reg);
        wasm_lset(e, reg);
        e->loaded |= 1U << reg;
    }
    wasm_lget(e, reg);
}

static void wasm_set(struct wasm_emitter *e, unsigned reg)
{
    if (!reg) {
        wasm_byte(e, W_DROP);
        return;
    }
    assert(reg < N_RV_REGS);
    wasm_lset(e, reg);
    e->loaded |= 1U << reg;
    e->dirty |= 1U << reg;
}

/* Add the emitter's pending cycles to the i64 on the stack. */
static void wasm_add_cycles(struct wasm_emitter *e)
{
    if (!e->cycles)
        return;
    wasm_const64(e, e->cycles);
    wasm_byte(e, W_I64_ADD);
}

static void wasm_flush(struct wasm_emitter *e)
{
    for (unsigned r = 1; r < N_RV_REGS; r++) {
        if (!(e->dirty & (1U << r)))
            continue;
        wasm_lget(e, 0);
        wasm_lget(e, r);
        wasm_mem(e, W_I32_STORE, offsetof(riscv_t, X) + 4 * r);
    }
    e->dirty = 0;
    if (e->cycles || e->keep_locals) {
        wasm_lget(e, 0);
        if (e->keep_locals)
            wasm_lget(e, WASM_CYCLE);
        else
            wasm_rv_load(e, W_I64_LOAD, offsetof(riscv_t, csr_cycle));
        wasm_add_cycles(e);
        wasm_mem(e, W_I64_STORE, offsetof(riscv_t, csr_cycle));
        e->cycles = 0;
    }
}

/* Consume an i32 condition and leave the function when it is nonzero. */
static void wasm_return_if(struct wasm_emitter *e)
{
    wasm_if(e);
    wasm_byte(e, W_RETURN);
    wasm_byte(e, W_END);
}

/* Run the original instruction through the interpreter, leaving its status. */
static void wasm_call_step(struct wasm_emitter *e, const rv_insn_t *ir)
{
    wasm_lget(e, 0);
    wasm_const(e, (uintptr_t) ir);
    wasm_byte(e, W_CALL);
    wasm_uleb(e, 0); /* imported step */
}

/* Consume an i32 condition. When it is nonzero, materialize the preceding work,
 * run the original instruction through its helper and leave the block. The
 * fall-through edge keeps the emitter's unspilled state.
 */
static void wasm_helper_exit(struct wasm_emitter *e, const rv_insn_t *ir)
{
    wasm_if(e);
    uint32_t dirty = e->dirty, cycles = e->cycles;
    wasm_flush(e);
    wasm_call_step(e, ir);
    wasm_byte(e, W_DROP);
    wasm_byte(e, W_RETURN);
    wasm_byte(e, W_END);
    e->dirty = dirty;
    e->cycles = cycles;
}

/* Inline RAM accesses: direct in user mode, through the dTLB in kernel boot
 * mode. System-mode ELF runs leave every access to the interpreter.
 */
#if !RV32_HAS(ELF_LOADER)
struct wasm_access {
    uint8_t width, op;
    bool store;
};

static bool wasm_access_of(unsigned opcode, struct wasm_access *a)
{
    switch (opcode) {
    case rv_insn_lb:
        *a = (struct wasm_access) {1, W_I32_LOAD8_S, false};
        return true;
    case rv_insn_lbu:
        *a = (struct wasm_access) {1, W_I32_LOAD8_U, false};
        return true;
    case rv_insn_lh:
        *a = (struct wasm_access) {2, W_I32_LOAD16_S, false};
        return true;
    case rv_insn_lhu:
        *a = (struct wasm_access) {2, W_I32_LOAD16_U, false};
        return true;
    case rv_insn_lw:
#if RV32_HAS(EXT_C)
    case rv_insn_clw:
    case rv_insn_clwsp:
#endif
        *a = (struct wasm_access) {4, W_I32_LOAD, false};
        return true;
    case rv_insn_sb:
        *a = (struct wasm_access) {1, W_I32_STORE8, true};
        return true;
    case rv_insn_sh:
        *a = (struct wasm_access) {2, W_I32_STORE16, true};
        return true;
    case rv_insn_sw:
#if RV32_HAS(EXT_C)
    case rv_insn_csw:
    case rv_insn_cswsp:
#endif
        *a = (struct wasm_access) {4, W_I32_STORE, true};
        return true;
    default:
        return false;
    }
}

/* Whether accesses of this kind may take the inline RAM path at all. */
static bool wasm_ram_usable(const struct wasm_emitter *e,
                            const struct wasm_access *a)
{
    if (!e->rv->data || !PRIV(e->rv)->mem)
        return false;
#if RV32_HAS(ARCH_TEST)
    /* The interpreter store watches tohost for the test's completion. */
    if (a->store)
        return false;
#endif
    const memory_t *mem = PRIV(e->rv)->mem;
    return mem->mem_size >= a->width && mem->mem_size <= UINT32_MAX;
}

/* Push addr & (width - 1): nonzero when the access is misaligned. */
static void wasm_misalignment(struct wasm_emitter *e, unsigned width)
{
    wasm_lget(e, WASM_ADDR);
    wasm_const(e, width - 1);
    wasm_byte(e, W_I32_AND);
}

/* Leave the RAM offset of rs1 + imm in WASM_ADDR and a nonzero i32 on the stack
 * when the access must take the slow path. Nothing is written.
 */
static void wasm_ram_guard(struct wasm_emitter *e,
                           unsigned rs1,
                           int32_t imm,
                           const struct wasm_access *a)
{
    const memory_t *mem = PRIV(e->rv)->mem;
    wasm_get(e, rs1);
    wasm_const(e, imm);
    wasm_byte(e, W_I32_ADD);
    wasm_lset(e, WASM_ADDR); /* effective guest address, wrapping at 32 bits */
#if RV32_HAS(SYSTEM_MMIO)
    /* Same exact-VPN dTLB contract as the native T1C fast path. Permissions and
     * dirty bits are checked before touching RAM; misses/MMIO return to the
     * existing interpreter so translation and PTE updates stay in C.
     */
    wasm_lget(e, WASM_ADDR);
    wasm_const(e, RV_PG_SHIFT);
    wasm_byte(e, W_I32_SHR_U);
    wasm_ltee(e, WASM_VPN);
    wasm_const(e, TLB_MASK);
    wasm_byte(e, W_I32_AND);
    wasm_const(e, WASM_TLB_SHIFT);
    wasm_byte(e, W_I32_SHL);
    wasm_lget(e, 0);
    wasm_byte(e, W_I32_ADD);
    wasm_const(e, offsetof(riscv_t, dtlb));
    wasm_byte(e, W_I32_ADD);
    wasm_ltee(e, WASM_TLB);
    wasm_mem(e, W_I32_LOAD8_U, offsetof(tlb_entry_t, valid));
    wasm_byte(e, W_I32_EQZ);
    wasm_lget(e, WASM_TLB);
    wasm_mem(e, W_I32_LOAD, offsetof(tlb_entry_t, vpn));
    wasm_lget(e, WASM_VPN);
    wasm_byte(e, W_I32_NE);
    wasm_byte(e, W_I32_OR);
    wasm_lget(e, WASM_TLB);
    wasm_mem(e, W_I32_LOAD8_U, offsetof(tlb_entry_t, perm));
    wasm_const(e, a->store ? PTE_W : PTE_R);
    wasm_byte(e, W_I32_AND);
    wasm_byte(e, W_I32_EQZ);
    wasm_byte(e, W_I32_OR);
    if (a->store) {
        wasm_lget(e, WASM_TLB);
        wasm_mem(e, W_I32_LOAD8_U, offsetof(tlb_entry_t, dirty));
        wasm_const(e, 1);
        wasm_byte(e, W_I32_NE);
        wasm_byte(e, W_I32_OR);
    }
    if (a->width > 1) {
        wasm_misalignment(e, a->width);
        wasm_byte(e, W_I32_OR);
    }
    wasm_lget(e, WASM_TLB);
    wasm_mem(e, W_I32_LOAD, offsetof(tlb_entry_t, ppn));
    wasm_lget(e, WASM_ADDR);
    wasm_const(e, RV_PG_SIZE - 1);
    wasm_byte(e, W_I32_AND);
    wasm_byte(e, W_I32_OR);
    wasm_lset(e, WASM_ADDR);
#endif
    wasm_lget(e, WASM_ADDR);
    wasm_const(e, mem->mem_size - a->width);
    wasm_byte(e, W_I32_GT_U); /* including overflow at the RAM boundary */
#if RV32_HAS(SYSTEM_MMIO)
    wasm_byte(e, W_I32_OR);
#else
    /* allow_misalign is fixed for the VM's lifetime. */
    if (a->width > 1 && !e->rv->allow_misalign) {
        wasm_misalignment(e, a->width);
        wasm_byte(e, W_I32_OR);
    }
#endif
}

/* Access RAM at the offset held in local addr; reg is the source of a store or
 * the destination of a load. Wasm permits unaligned accesses here.
 */
static void wasm_ram_access(struct wasm_emitter *e,
                            const struct wasm_access *a,
                            unsigned reg,
                            unsigned addr)
{
    /* Reboots refill guest RAM in place, so its base never moves. */
    wasm_const(e, (uintptr_t) PRIV(e->rv)->mem->mem_base);
    wasm_lget(e, addr);
    wasm_byte(e, W_I32_ADD);
    if (a->store)
        wasm_get(e, reg);
    wasm_byte(e, a->op);
    wasm_uleb(e, 0); /* align 1 (log2 0): guest accesses may be misaligned */
    wasm_uleb(e, 0); /* offset */
    if (!a->store)
        wasm_set(e, reg);
}

/* A failed RAM guard exits through the original instruction. No state is
 * spilled on a hit; the slow edge materializes exactly the preceding work.
 */
static void wasm_ram_one(struct wasm_emitter *e,
                         const rv_insn_t *ir,
                         const struct wasm_access *a)
{
    wasm_ram_guard(e, ir->rs1, ir->imm, a);
    wasm_helper_exit(e, ir);
    wasm_ram_access(e, a, a->store ? ir->rs2 : ir->rd, WASM_ADDR);
}

static bool wasm_ram(struct wasm_emitter *e, const rv_insn_t *ir)
{
    struct wasm_access a;
    if (!wasm_access_of(ir->opcode, &a) || !wasm_ram_usable(e, &a))
        return false;
    wasm_ram_one(e, ir, &a);
    e->cycles++;
    return true;
}

/* Fused runs of loads (fuse4) or stores (fuse3). Every member is guarded before
 * any of them runs, so a slow path replays the whole record through the
 * interpreter from unchanged state.
 */
static bool wasm_ram_group(struct wasm_emitter *e, const rv_insn_t *ir)
{
    if (ir->opcode != rv_insn_fuse3 && ir->opcode != rv_insn_fuse4)
        return false;
    const bool store = ir->opcode == rv_insn_fuse3;
    const opcode_fuse_t *f = ir->fuse;
    const int n = ir->imm2;
    struct wasm_access a[FUSE_MAX_ENTRIES];
    if (n < 1 || n > FUSE_MAX_ENTRIES)
        return false;
    for (int i = 0; i < n; i++) {
        if (!wasm_access_of(f[i].opcode, &a[i]) || a[i].store != store ||
            !wasm_ram_usable(e, &a[i]))
            return false;
        /* Guards read every base up front, before any member loads. */
        for (int j = 0; !store && j < i; j++) {
            if (f[j].rd && f[j].rd == f[i].rs1)
                return false;
        }
    }
    for (int i = 0; i < n; i++) {
        wasm_ram_guard(e, f[i].rs1, f[i].imm, &a[i]);
        wasm_lget(e, WASM_ADDR);
        wasm_lset(e, WASM_GROUP + i);
        if (i)
            wasm_byte(e, W_I32_OR);
    }
    wasm_helper_exit(e, ir);
    for (int i = 0; i < n; i++)
        wasm_ram_access(e, &a[i], store ? f[i].rs2 : f[i].rd, WASM_GROUP + i);
    e->cycles += n;
    return true;
}

/* LW + ADDI (fuse11) and SW + ADDI (fuse13): one access, then the base register
 * advances by imm2.
 */
static bool wasm_ram_post_increment(struct wasm_emitter *e, const rv_insn_t *ir)
{
    if (ir->opcode != rv_insn_fuse11 && ir->opcode != rv_insn_fuse13)
        return false;
    const bool store = ir->opcode == rv_insn_fuse13;
    struct wasm_access a;
    wasm_access_of(store ? rv_insn_sw : rv_insn_lw, &a);
    if (!wasm_ram_usable(e, &a))
        return false;
    wasm_ram_one(e, ir, &a);
    wasm_get(e, ir->rs1);
    wasm_const(e, ir->imm2);
    wasm_byte(e, W_I32_ADD);
    wasm_set(e, ir->rs1);
    e->cycles += 2;
    return true;
}
#endif /* !RV32_HAS(ELF_LOADER) */

static void wasm_binary(struct wasm_emitter *e,
                        unsigned rd,
                        unsigned rs1,
                        unsigned rs2,
                        int32_t imm,
                        bool immediate,
                        uint8_t op)
{
    wasm_get(e, rs1);
    if (immediate)
        wasm_const(e, imm);
    else
        wasm_get(e, rs2);
    wasm_byte(e, op);
    wasm_set(e, rd);
}

#if RV32_HAS(EXT_M)
static void wasm_divrem(struct wasm_emitter *e, const rv_insn_t *ir)
{
    const bool sign = ir->opcode == rv_insn_div || ir->opcode == rv_insn_rem;
    const bool rem = ir->opcode == rv_insn_rem || ir->opcode == rv_insn_remu;

    /* First loads must dominate both arms, including helper-containing blocks.
     */
    wasm_get(e, ir->rs1);
    wasm_byte(e, W_DROP);
    wasm_get(e, ir->rs2);
    wasm_byte(e, W_I32_EQZ);
    wasm_open(e, W_IF, W_TYPE_I32); /* RISC-V division by zero does not trap */
    if (rem)
        wasm_get(e, ir->rs1);
    else
        wasm_const(e, -1);
    wasm_byte(e, W_ELSE);
    if (sign && !rem) {
        wasm_get(e, ir->rs1);
        wasm_const(e, INT32_MIN);
        wasm_byte(e, W_I32_EQ);
        wasm_get(e, ir->rs2);
        wasm_const(e, -1);
        wasm_byte(e, W_I32_EQ);
        wasm_byte(e, W_I32_AND);
        wasm_open(e, W_IF, W_TYPE_I32);
        wasm_const(e, INT32_MIN);
        wasm_byte(e, W_ELSE);
    }
    wasm_get(e, ir->rs1);
    wasm_get(e, ir->rs2);
    wasm_byte(e, rem ? (sign ? W_I32_REM_S : W_I32_REM_U)
                     : (sign ? W_I32_DIV_S : W_I32_DIV_U));
    if (sign && !rem)
        wasm_byte(e, W_END);
    wasm_byte(e, W_END);
    wasm_set(e, ir->rd);
}
#endif

static void wasm_target(struct wasm_emitter *e, uint32_t pc)
{
    wasm_const(e, pc);
    wasm_lset(e, WASM_NEXT_PC);
}

/* A conditional branch pushes both successors, then its condition, and ends
 * with wasm_select_next(): the next PC is taken when the condition holds.
 */
static void wasm_successors(struct wasm_emitter *e,
                            uint32_t taken,
                            uint32_t next)
{
    wasm_const(e, taken);
    wasm_const(e, next);
}

static void wasm_select_next(struct wasm_emitter *e)
{
    wasm_byte(e, W_SELECT);
    wasm_lset(e, WASM_NEXT_PC);
}

/* Return false for a helper operation. Its detached record retains all the
 * original fused operands and consumes the original number of cycles. Without
 * C, branch alignment depends on the taken edge. Let the interpreter perform
 * that check rather than trap on an untaken branch.
 */
static bool wasm_defer_misaligned(const rv_insn_t *ir)
{
#if RV32_HAS(EXT_C)
    (void) ir;
    return false;
#else
    if (ir->opcode == rv_insn_fuse12) /* ADDI + BNE: imm2 is the offset */
        return ir->imm2 & 3;
    return (ir->imm & 3) &&
           (ir->opcode == rv_insn_jal ||
            (ir->opcode >= rv_insn_beq && ir->opcode <= rv_insn_bgeu));
#endif
}

static bool wasm_lower(struct wasm_emitter *e, const rv_insn_t *ir)
{
    if (wasm_defer_misaligned(ir))
        return false;
#if !RV32_HAS(ELF_LOADER)
    if (wasm_ram(e, ir) || wasm_ram_group(e, ir) ||
        wasm_ram_post_increment(e, ir))
        return true;
#endif
    switch (ir->opcode) {
    case rv_insn_nop:
        break;
    case rv_insn_lui:
        wasm_const(e, ir->imm);
        wasm_set(e, ir->rd);
        break;
    case rv_insn_auipc:
        wasm_const(e, ir->pc + (uint32_t) ir->imm);
        wasm_set(e, ir->rd);
        break;
#define ALU(inst, op, immediate)                                          \
    case rv_insn_##inst:                                                  \
        wasm_binary(e, ir->rd, ir->rs1, ir->rs2, ir->imm, immediate, op); \
        break
        ALU(addi, W_I32_ADD, true);
        ALU(xori, W_I32_XOR, true);
        ALU(ori, W_I32_OR, true);
        ALU(andi, W_I32_AND, true);
        ALU(slti, W_I32_LT_S, true);
        ALU(sltiu, W_I32_LT_U, true);
        ALU(add, W_I32_ADD, false);
        ALU(sub, W_I32_SUB, false);
        ALU(xor, W_I32_XOR, false);
        ALU(or, W_I32_OR, false);
        ALU(and, W_I32_AND, false);
        ALU(slt, W_I32_LT_S, false);
        ALU(sltu, W_I32_LT_U, false);
        ALU(sll, W_I32_SHL, false); /* Wasm masks shift counts as RISC-V */
        ALU(srl, W_I32_SHR_U, false);
        ALU(sra, W_I32_SHR_S, false);
        ALU(slli, W_I32_SHL, true);
        ALU(srli, W_I32_SHR_U, true);
        ALU(srai, W_I32_SHR_S, true);
#if RV32_HAS(EXT_M)
        ALU(mul, W_I32_MUL, false);
    case rv_insn_div:
    case rv_insn_divu:
    case rv_insn_rem:
    case rv_insn_remu:
        wasm_divrem(e, ir);
        break;
    case rv_insn_mulh:
    case rv_insn_mulhsu:
    case rv_insn_mulhu:
        wasm_get(e, ir->rs1);
        wasm_byte(e, ir->opcode == rv_insn_mulhu ? W_I64_EXTEND_I32_U
                                                 : W_I64_EXTEND_I32_S);
        wasm_get(e, ir->rs2);
        wasm_byte(e, ir->opcode == rv_insn_mulh ? W_I64_EXTEND_I32_S
                                                : W_I64_EXTEND_I32_U);
        wasm_byte(e, W_I64_MUL);
        wasm_const64(e, 32);
        wasm_byte(e, W_I64_SHR_U); /* only the high 32 bits are retained */
        wasm_byte(e, W_I32_WRAP_I64);
        wasm_set(e, ir->rd);
        break;
#endif
#undef ALU
#define BRANCH(inst, op)                                             \
    case rv_insn_##inst:                                             \
        wasm_successors(e, ir->pc + (uint32_t) ir->imm, ir->pc + 4); \
        wasm_get(e, ir->rs1);                                        \
        wasm_get(e, ir->rs2);                                        \
        wasm_byte(e, op);                                            \
        wasm_select_next(e);                                         \
        break
        BRANCH(beq, W_I32_EQ);
        BRANCH(bne, W_I32_NE);
        BRANCH(blt, W_I32_LT_S);
        BRANCH(bge, W_I32_GE_S);
        BRANCH(bltu, W_I32_LT_U);
        BRANCH(bgeu, W_I32_GE_U);
#undef BRANCH
    case rv_insn_jal:
        wasm_const(e, ir->pc + 4);
        wasm_set(e, ir->rd);
        wasm_target(e, ir->pc + (uint32_t) ir->imm);
        break;
    case rv_insn_jalr:
        wasm_get(e, ir->rs1);
        wasm_const(e, ir->imm);
        wasm_byte(e, W_I32_ADD);
        wasm_const(e, -2);
        wasm_byte(e, W_I32_AND);
#if !RV32_HAS(EXT_C)
        /* Only a misaligned target reaches the interpreter, which traps before
         * its branch history table can chain into another block.
         */
        wasm_ltee(e, WASM_NEXT_PC);
        wasm_const(e, 2);
        wasm_byte(e, W_I32_AND);
        wasm_helper_exit(e, ir);
#else
        wasm_lset(e, WASM_NEXT_PC);
#endif
        wasm_const(e, ir->pc + 4);
        wasm_set(e, ir->rd);
        break;
#if RV32_HAS(EXT_C)
    case rv_insn_cnop:
        break;
    case rv_insn_cli:
    case rv_insn_clui:
        wasm_const(e, ir->imm);
        wasm_set(e, ir->rd);
        break;
    case rv_insn_cmv:
        wasm_get(e, ir->rs2);
        wasm_set(e, ir->rd);
        break;
    case rv_insn_caddi:
    case rv_insn_caddi16sp:
        wasm_binary(e, ir->rd, ir->rd, 0, ir->imm, true, W_I32_ADD);
        break;
    case rv_insn_caddi4spn:
        wasm_binary(e, ir->rd, rv_reg_sp, 0, ir->imm, true, W_I32_ADD);
        break;
    case rv_insn_cslli:
        wasm_binary(e, ir->rd, ir->rd, 0, ir->imm, true, W_I32_SHL);
        break;
    case rv_insn_csrli:
    case rv_insn_csrai:
        wasm_binary(e, ir->rs1, ir->rs1, 0, ir->shamt, true,
                    ir->opcode == rv_insn_csrli ? W_I32_SHR_U : W_I32_SHR_S);
        break;
    case rv_insn_candi:
        wasm_binary(e, ir->rs1, ir->rs1, 0, ir->imm, true, W_I32_AND);
        break;
    case rv_insn_cadd:
    case rv_insn_csub:
    case rv_insn_cxor:
    case rv_insn_cor:
    case rv_insn_cand: {
        uint8_t op = ir->opcode == rv_insn_cadd   ? W_I32_ADD
                     : ir->opcode == rv_insn_csub ? W_I32_SUB
                     : ir->opcode == rv_insn_cxor ? W_I32_XOR
                     : ir->opcode == rv_insn_cor  ? W_I32_OR
                                                  : W_I32_AND;
        wasm_binary(e, ir->rd, ir->rs1, ir->rs2, 0, false, op);
        break;
    }
    case rv_insn_cj:
    case rv_insn_cjal:
        if (ir->opcode == rv_insn_cjal) {
            wasm_const(e, ir->pc + 2);
            wasm_set(e, rv_reg_ra);
        }
        wasm_target(e, ir->pc + (uint32_t) ir->imm);
        break;
    case rv_insn_cjr:
    case rv_insn_cjalr:
        wasm_get(e, ir->rs1);
        wasm_const(e, -2);
        wasm_byte(e, W_I32_AND);
        wasm_lset(e, WASM_NEXT_PC);
        if (ir->opcode == rv_insn_cjalr) {
            wasm_const(e, ir->pc + 2);
            wasm_set(e, rv_reg_ra);
        }
        break;
    case rv_insn_cbeqz:
    case rv_insn_cbnez:
        wasm_successors(e, ir->pc + (uint32_t) ir->imm, ir->pc + 2);
        wasm_get(e, ir->rs1);
        if (ir->opcode == rv_insn_cbeqz)
            wasm_byte(e, W_I32_EQZ);
        wasm_select_next(e);
        break;
#endif
    case rv_insn_fuse12:
        wasm_binary(e, ir->rd, ir->rs1, 0, ir->imm, true, W_I32_ADD);
        wasm_successors(e, ir->pc + 4 + (uint32_t) ir->imm2, ir->pc + 8);
        wasm_get(e, ir->rd);
        wasm_select_next(e);
        e->cycles++;
        break;
    case rv_insn_fuse1:
    case rv_insn_fuse7:
    case rv_insn_fuse5:
        for (int i = 0; i < ir->imm2; i++) {
            const opcode_fuse_t *f = &ir->fuse[i];
            if (ir->opcode == rv_insn_fuse1) {
                wasm_const(e, f->imm);
                wasm_set(e, f->rd);
            } else {
                uint8_t op = ir->opcode == rv_insn_fuse7 ? W_I32_ADD
                             : f->opcode == rv_insn_slli ? W_I32_SHL
                             : f->opcode == rv_insn_srli ? W_I32_SHR_U
                                                         : W_I32_SHR_S;
                wasm_binary(e, f->rd, f->rs1, 0, f->imm, true, op);
            }
        }
        e->cycles += ir->imm2;
        return true;
    case rv_insn_fuse8:
        wasm_const(e, (uint32_t) ir->imm + (uint32_t) ir->imm2);
        wasm_set(e, ir->rd);
        e->cycles++; /* two original instructions */
        break;
    default:
        return false;
    }
    e->cycles++;
    return true;
}

static uint32_t wasm_slot(uint32_t pc, uint32_t satp)
{
    return ((pc >> WASM_SLOT_SHIFT_LO) ^ (pc >> WASM_SLOT_SHIFT_HI) ^ satp) &
           ((1 << WASM_LOOKUP_BITS) - 1);
}

#if RV32_HAS(SYSTEM)
static uint32_t wasm_page_slot(uint32_t va, uint32_t satp)
{
    return ((va >> RV_PG_SHIFT) ^
            ((satp * WASM_SATP_MIX) >> (32 - WASM_PAGE_BITS))) &
           ((1 << WASM_PAGE_BITS) - 1);
}
#endif

static void wasm_clear_hot(block_t *block)
{
    block->offset = 0;
    block->hot = false;
}

/* Unpublish a module, free its table slot and send its block, if any, back to
 * the interpreter.
 */
static void wasm_retire(struct jit_state *state, struct wasm_map *map)
{
    uint32_t slot = wasm_slot(map->pc, BLOCK_SATP(map));
    if (state->lookup[slot] == (uintptr_t) map)
        state->lookup[slot] = 0;
    if (map->entry) {
        wasm_remove(map->entry);
        map->entry = 0;
        state->offset -= map->size;
        const uint16_t index = map - state->maps + 1;
        state->free_maps[state->n_free++] = index - 1;
#if RV32_HAS(SYSTEM)
        uint16_t *link = &state->pages[wasm_page_slot(map->pc, map->satp)];
        while (*link != index) {
            assert(*link); /* every live map is in its bucket */
            link = &state->maps[*link - 1].next_in_page;
        }
        *link = map->next_in_page;
#endif
    }
    if (map->owner) {
        wasm_clear_hot(map->owner);
        map->owner = NULL;
    }
}

void jit_wasm_retire_block(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    if (state && block->hot && block->offset &&
        block->offset <= (uint32_t) state->n_blocks) {
        struct wasm_map *map = &state->maps[block->offset - 1];
        if (map->owner == block)
            wasm_retire(state, map);
    }
    wasm_clear_hot(block);
}

static void wasm_reset(struct jit_state *state)
{
    for (int i = 0; i < state->n_blocks; i++) {
        if (state->maps[i].entry)
            wasm_remove(state->maps[i].entry);
    }
    state->n_blocks = state->n_free = 0;
    state->offset = 0;
    memset(state->lookup, 0, sizeof(state->lookup));
#if RV32_HAS(SYSTEM)
    memset(state->pages, 0, sizeof(state->pages));
#endif
}

/* Drop every module and send all blocks back to the interpreter. */
static void wasm_flush_all(riscv_t *rv)
{
    wasm_reset(rv->jit_state);
    clear_cache_hot(rv->block_cache, (clear_func_t) wasm_clear_hot);
}

struct jit_state *jit_state_init(size_t size, uintptr_t mem_base)
{
    (void) mem_base;
    struct jit_state *state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->size = size;
    state->scratch = malloc(WASM_BODY_SIZE);
    if (!state->scratch) {
        free(state);
        return NULL;
    }
    return state;
}

void jit_state_exit(struct jit_state *state)
{
    if (WASM_PROFILE_ENABLED)
        wasm_profile_dump(&state->profile);
    wasm_reset(state);
    free(state->scratch);
    free(state);
}

void jit_wasm_run(riscv_t *rv, const block_t *block)
{
    struct jit_state *state = rv->jit_state;
    struct wasm_map *map = &state->maps[block->offset - 1];
    assert(map->owner == block && map->entry);

    /* A hash collision returns to rv_step, which refills the entry here.
     * Generated edges chain until the guest cycle count reaches the limit.
     */
    state->lookup[wasm_slot(map->pc, BLOCK_SATP(map))] = (uintptr_t) map;
    rv->wasm_jit_limit = rv->csr_cycle + WASM_CYCLE_SLICE;
#if RV32_HAS(SYSTEM)
    /* A trap the dispatcher has yet to take must stop the chain at once. */
    if (rv->trap_cnt)
        rv->wasm_jit_limit = 0;
#endif
    const uint64_t before = rv->csr_cycle;
    ((void (*)(riscv_t *))(uintptr_t) map->entry)(rv);
    if (WASM_PROFILE_ENABLED) {
        /* A helper may reboot the guest, which replaces the JIT state and
         * resets the cycle counter. Never touch the state saved before the
         * call.
         */
        struct jit_state *current = rv->jit_state;
        if (rv->csr_cycle >= before)
            current->profile.cycles += rv->csr_cycle - before;
    }
}

#define WASM_COUNTER(state, field) \
    (WASM_PROFILE_ENABLED ? &(state)->profile.field : NULL)

/* Emit ++*counter when profiling; counter is NULL otherwise. */
static void wasm_count(struct wasm_emitter *e, uint64_t *counter)
{
    if (!counter)
        return;
    wasm_const(e, (uintptr_t) counter);
    wasm_const(e, (uintptr_t) counter);
    wasm_mem(e, W_I64_LOAD, 0);
    wasm_const64(e, 1);
    wasm_byte(e, W_I64_ADD);
    wasm_mem(e, W_I64_STORE, 0);
}

/* Cross-module tail calls keep the native stack bounded and avoid a Wasm/C
 * boundary plus a general block-cache lookup on each short guest block.
 * Retiring a module clears its lookup entry before its block's IR can be
 * recycled.
 */
static void wasm_tail(struct wasm_emitter *e, struct jit_state *state)
{
    /* One compare bounds the chain. jit_wasm_run() sets the limit, or zeroes it
     * when a trap is already pending, and a helper that halts the hart zeroes
     * it too. A helper that raises a trap returns at once, so the trap is taken
     * in rv_step before any successor runs.
     */
    if (e->keep_locals) /* just stored to csr_cycle */
        wasm_lget(e, WASM_CYCLE);
    else
        wasm_rv_load(e, W_I64_LOAD, offsetof(riscv_t, csr_cycle));
    wasm_rv_load(e, W_I64_LOAD, offsetof(riscv_t, wasm_jit_limit));
    wasm_byte(e, W_I64_LT_U);
    wasm_if(e);
    /* map = lookup[wasm_slot(next PC, satp)] */
    wasm_const(e, (uintptr_t) state->lookup);
    wasm_lget(e, WASM_NEXT_PC);
    wasm_const(e, WASM_SLOT_SHIFT_LO);
    wasm_byte(e, W_I32_SHR_U);
    wasm_lget(e, WASM_NEXT_PC);
    wasm_const(e, WASM_SLOT_SHIFT_HI);
    wasm_byte(e, W_I32_SHR_U);
    wasm_byte(e, W_I32_XOR);
#if RV32_HAS(SYSTEM)
    wasm_rv_load(e, W_I32_LOAD, offsetof(riscv_t, csr_satp));
    wasm_ltee(e, WASM_TAIL_SATP);
    wasm_byte(e, W_I32_XOR);
#endif
    wasm_const(e, (1 << WASM_LOOKUP_BITS) - 1);
    wasm_byte(e, W_I32_AND);
    wasm_const(e, WASM_LOOKUP_SHIFT);
    wasm_byte(e, W_I32_SHL);
    wasm_byte(e, W_I32_ADD);
    wasm_mem(e, W_I32_LOAD, 0);
    wasm_ltee(e, WASM_TAIL_MAP);
    wasm_if(e);
    /* map->pc == next PC && map->entry [&& map->satp == satp] */
    wasm_lget(e, WASM_TAIL_MAP);
    wasm_mem(e, W_I32_LOAD, offsetof(struct wasm_map, pc));
    wasm_lget(e, WASM_NEXT_PC);
    wasm_byte(e, W_I32_EQ);
    wasm_lget(e, WASM_TAIL_MAP);
    wasm_mem(e, W_I32_LOAD, offsetof(struct wasm_map, entry));
    wasm_ltee(e, WASM_TAIL_FUNC);
    wasm_byte(e, W_I32_EQZ);
    wasm_byte(e, W_I32_EQZ);
    wasm_byte(e, W_I32_AND);
#if RV32_HAS(SYSTEM)
    wasm_lget(e, WASM_TAIL_MAP);
    wasm_mem(e, W_I32_LOAD, offsetof(struct wasm_map, satp));
    wasm_lget(e, WASM_TAIL_SATP);
    wasm_byte(e, W_I32_EQ);
    wasm_byte(e, W_I32_AND);
#endif
    wasm_if(e);
    wasm_count(e, WASM_COUNTER(state, lookup_hits));
    wasm_lget(e, 0);
    wasm_lget(e, WASM_TAIL_FUNC);
    wasm_byte(e, W_RETURN_CALL_INDIRECT);
    wasm_uleb(e, 1); /* (rv) -> void */
    wasm_uleb(e, 0); /* imported table */
    wasm_byte(e, W_END);
    wasm_byte(e, W_END);
    wasm_count(e, WASM_COUNTER(state, lookup_misses));
    wasm_byte(e, W_END);
    /* The chain ends here, so rv_step needs the PC; a callee does not. */
    wasm_lget(e, 0);
    wasm_lget(e, WASM_NEXT_PC);
    wasm_mem(e, W_I32_STORE, offsetof(riscv_t, PC));
}

/* Run ir in the interpreter with every live register flushed, and return when
 * it fails, traps, or leaves the block before next_pc.
 */
static void wasm_helper(struct wasm_emitter *e,
                        const rv_insn_t *ir,
                        uint32_t next_pc)
{
    wasm_flush(e);
    e->loaded = 0;
    wasm_call_step(e, ir);
    wasm_byte(e, W_I32_EQZ); /* unsuccessful interpreter exit */
    wasm_return_if(e);
#if RV32_HAS(SYSTEM)
    wasm_rv_load(e, W_I32_LOAD, offsetof(riscv_t, trap_cnt));
    wasm_return_if(e);
#endif

    /* Helpers may trap, emulate a misaligned access, or terminate a block.
     * Never execute later records after a diverted PC.
     */
    wasm_rv_load(e, W_I32_LOAD, offsetof(riscv_t, PC));
    wasm_const(e, next_pc);
    wasm_byte(e, W_I32_NE);
    wasm_return_if(e);
}

/* A bounded self-loop avoids dispatch and JS calls on tight guest loops, while
 * returning regularly for interrupts, timers and browser yields.
 */
static void wasm_loop_back(struct wasm_emitter *e, uint32_t pc_start)
{
    wasm_lget(e, WASM_BUDGET);
    wasm_const(e, 1);
    wasm_byte(e, W_I32_ADD);
    wasm_ltee(e, WASM_BUDGET);
    wasm_const(e, WASM_LOOP_LIMIT);
    wasm_byte(e, W_I32_LT_U);
    wasm_lget(e, WASM_NEXT_PC);
    wasm_const(e, pc_start);
    wasm_byte(e, W_I32_EQ);
    wasm_byte(e, W_I32_AND);
    wasm_byte(e, W_BR_IF);
    wasm_uleb(e, 0); /* the loop */
}

bool jit_translate(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    if (state->disabled)
        return false;
    const double start = WASM_PROFILE_ENABLED ? emscripten_get_now() : 0;
    struct wasm_emitter e = {.code = state->scratch, .rv = rv};

    /* First lower into the scratch buffer to discover blocks whose hot path
     * needs no helpers. Their loop-carried values need initial loads only once;
     * RAM guard failures still spill precise state before exiting.
     */
    bool keep = true;
    for (const rv_insn_t *ir = block->ir_head; ir; ir = ir->next) {
        if (!wasm_lower(&e, ir)) {
            keep = false;
            break;
        }
    }
    uint32_t loop_dirty = e.dirty;
    uint32_t inputs = e.inputs | loop_dirty;
    e.size = e.loaded = e.dirty = e.inputs = e.cycles = 0;
    e.failed = false;
    e.keep_locals = keep;

    /* Local declarations: x1..x31 and WASM_ADDR..WASM_BUDGET, the i64
     * WASM_CYCLE, then WASM_TLB, WASM_VPN and the group offsets.
     */
    wasm_uleb(&e, 3);
    wasm_uleb(&e, WASM_BUDGET);
    wasm_byte(&e, W_TYPE_I32);
    wasm_uleb(&e, 1);
    wasm_byte(&e, W_TYPE_I64);
    wasm_uleb(&e, WASM_GROUP + FUSE_MAX_ENTRIES - WASM_TLB);
    wasm_byte(&e, W_TYPE_I32);
    wasm_count(&e, WASM_COUNTER(state, calls));
    if (keep) {
        for (unsigned r = 1; r < N_RV_REGS; r++) {
            if (inputs & (1U << r)) {
                wasm_get(&e, r);
                wasm_byte(&e, W_DROP);
            }
        }
        wasm_rv_load(&e, W_I64_LOAD, offsetof(riscv_t, csr_cycle));
        wasm_lset(&e, WASM_CYCLE);
        e.dirty = loop_dirty; /* slow edges also spill prior iterations */
    }
    wasm_open(&e, W_LOOP, W_VOID);
    wasm_target(&e, block->pc_end);
    for (const rv_insn_t *ir = block->ir_head; ir; ir = ir->next) {
        if (!wasm_lower(&e, ir))
            wasm_helper(&e, ir, ir->next ? ir->next->pc : block->pc_end);
    }
    if (keep) {
        wasm_lget(&e, WASM_CYCLE);
        wasm_add_cycles(&e);
        wasm_lset(&e, WASM_CYCLE);
        e.cycles = 0;
    } else {
        wasm_flush(&e);
    }
    wasm_loop_back(&e, block->pc_start);
    wasm_byte(&e, W_END); /* loop */
    if (keep)
        wasm_flush(&e);
    wasm_tail(&e, state);
    wasm_byte(&e, W_END); /* function */
    if (e.failed || e.size > state->size) {
        block->translatable = false;
        return false;
    }
    if ((state->n_blocks == WASM_MAX_BLOCKS && !state->n_free) ||
        e.size > state->size - state->offset)
        wasm_flush_all(rv);
    const double module_start = WASM_PROFILE_ENABLED ? emscripten_get_now() : 0;
    int entry =
        wasm_install(e.code, e.size, (uintptr_t) wasm_step, block->pc_start,
                     BLOCK_SATP(block), WASM_PROFILE_ENABLED);
    if (WASM_PROFILE_ENABLED) {
        const double end = emscripten_get_now();
        state->profile.translate_ms += end - start;
        state->profile.module_ms += end - module_start;
        state->profile.blocks++;
        state->profile.bytes += e.size;
        state->profile.loops += keep;
    }
    if (!entry) {
        /* A first failure means runtime compilation is unavailable (a CSP, for
         * example); a later one rejects only this block.
         */
        if (!state->installed)
            state->disabled = true;
        block->translatable = false;
        return false;
    }
    state->installed = true;
    const int index =
        state->n_free ? state->free_maps[--state->n_free] : state->n_blocks++;
    struct wasm_map *map = &state->maps[index];
    *map = (struct wasm_map) {
        .pc = block->pc_start, .entry = entry, .size = e.size, .owner = block};
#if RV32_HAS(SYSTEM)
    map->satp = block->satp;
    uint16_t *head = &state->pages[wasm_page_slot(map->pc, map->satp)];
    map->next_in_page = *head;
    *head = index + 1;
#endif
    state->lookup[wasm_slot(map->pc, BLOCK_SATP(map))] = (uintptr_t) map;
    state->offset += e.size;
    block->offset = index + 1;
    block->hot = true;
    return true;
}

#if RV32_HAS(SYSTEM)
void jit_flush(riscv_t *rv)
{
    wasm_flush_all(rv);
}

void jit_retire_space(struct jit_state *state, uint32_t satp)
{
    for (int i = 0; i < state->n_blocks; i++) {
        struct wasm_map *map = &state->maps[i];
        if (map->entry && map->satp == satp)
            wasm_retire(state, map);
    }
}

bool jit_page_has_code(const struct jit_state *state,
                       uint32_t va,
                       uint32_t satp)
{
    for (uint16_t i = state->pages[wasm_page_slot(va, satp)]; i;
         i = state->maps[i - 1].next_in_page) {
        const struct wasm_map *map = &state->maps[i - 1];
        if (map->satp == satp &&
            (map->pc >> RV_PG_SHIFT) == (va >> RV_PG_SHIFT))
            return true;
    }
    return false;
}
#endif
