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
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#endif
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
#include "riscv_private.h"
#include "utils.h"

#if RV32_HAS(SYSTEM)
#include "system.h"
#endif

#if RV32_HAS(SYSTEM_MMIO)
/* JIT MMU fast-path ABI invariants.
 *
 * The inline dTLB probe emitted by the per-arch fast path hard-codes field
 * offsets and sizes of tlb_entry_t and memory_t. Any layout change must keep
 * these asserts true, or the emitted asm/IR will silently access the wrong
 * bytes. These mirror the structures in riscv_private.h and io.h.
 *
 * The fast path also assumes:
 *   - TLB_SIZE is a power of two so VPN -> index is a simple AND.
 *   - Each VPN tag identifies an exact 4 KiB slice. Its ppn includes the
 *     superpage offset's bits 21:12, so both hosts use a 12-bit offset.
 *     T2C still bails to C on superpage entries: an LLVM-IR select
 *     variant showed intermittent crashes after T2C compilation, root cause
 *     not isolated.
 *   - dirty == 1 on writes; clean PTE writes fall back to C so PTE_D update
 *     stays in dtlb_lookup() / mmu_translate().
 *   - paddr range is checked against memory_t mem_size to keep MMIO mappings
 *     on the slow path.
 */
_Static_assert(sizeof(tlb_entry_t) == 16, "tlb_entry_t must be 16 bytes");
_Static_assert(offsetof(tlb_entry_t, vpn) == 0, "tlb_entry_t.vpn offset");
_Static_assert(offsetof(tlb_entry_t, ppn) == 4, "tlb_entry_t.ppn offset");
_Static_assert(offsetof(tlb_entry_t, pte_addr) == 8,
               "tlb_entry_t.pte_addr offset");
_Static_assert(offsetof(tlb_entry_t, perm) == 12, "tlb_entry_t.perm offset");
_Static_assert(offsetof(tlb_entry_t, valid) == 13, "tlb_entry_t.valid offset");
_Static_assert(offsetof(tlb_entry_t, dirty) == 14, "tlb_entry_t.dirty offset");
_Static_assert(offsetof(tlb_entry_t, level) == 15, "tlb_entry_t.level offset");
_Static_assert((TLB_SIZE & (TLB_SIZE - 1)) == 0,
               "TLB_SIZE must be a power of two");
/* The x86-64 fast path masks the VPN with `and esi, TLB_SIZE - 1` using opcode
 * 0x83 /4, whose 8-bit immediate is sign-extended to 32 bits.  TLB_SIZE - 1
 * must therefore fit in a signed 8-bit value (<= 127); otherwise the mask
 * becomes negative and the dTLB index calculation is wrong.  If you ever need
 * a larger TLB, switch that single instruction to the imm32 form (0x81 /4).
 */
_Static_assert(TLB_SIZE <= 128,
               "TLB_SIZE > 128 needs imm32 AND encoding in the x86-64 "
               "fast path");
_Static_assert(RV_PG_SHIFT == 12, "fast path assumes 4 KiB pages");
_Static_assert(offsetof(memory_t, mem_base) == 0, "memory_t.mem_base offset");
_Static_assert(offsetof(memory_t, mem_size) == 8, "memory_t.mem_size offset");
_Static_assert(PTE_R == 0x02, "PTE_R must be bit 1");
_Static_assert(PTE_W == 0x04, "PTE_W must be bit 2");
#endif /* RV32_HAS(SYSTEM_MMIO) */

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
/* JIT_CALL_PAD follows a pushed rv at a call site: together they keep RSP
 * 16-byte aligned, and on Windows the pad also holds the callee's home space.
 */
#if defined(_WIN32)
#define JIT_FRAME_SIZE (STACK_SIZE + 4 * sizeof(uint64_t))
#define JIT_CALL_PAD 0x28
#else
#define JIT_FRAME_SIZE STACK_SIZE
#define JIT_CALL_PAD 0x8
#endif

/* The SYSTEM_MMIO fast path adds ~8 jump-table entries per load/store
 * (7 slow-path gating branches + 1 unconditional B), pushing
 * JUMPS_PER_INSN to 16.  state->jumps accumulates across chained blocks,
 * so the cap also gates how many instructions can be translated together.
 * 65536 keeps the table at 1 MiB (struct jump is 16 bytes under SYSTEM)
 * and lets a chained sequence translate up to ~4000 insns before bailing.
 */
#define MAX_JUMPS 65536

/* User-mode T1 links a direct branch to its target whether or not the
 * interpreter has taken that edge yet, and records a jump whose target is not
 * compiled so that the target's translation can patch it later. A block
 * compiled before its successors were known thus still chains natively.
 * System mode keeps linking learned edges only.
 */
#define JIT_LINK_ALL_EDGES (!RV32_HAS(SYSTEM))
#define MAX_PENDING_JUMPS 65536
#define MAX_BLOCKS 8192
#define IN_JUMP_THRESHOLD 256
/* Bound both guard overhead and speculative expansion of the code cache. */
#define IN_JUMP_TARGETS 2

/* Tier-1 indirect-jump specialization: inline guards for the most frequently
 * observed targets, and chained translation that follows them.
 *
 * TODO: allow this under T2C once generated code reports its own execution
 * count. Tier-2 promotion is measured at the dispatcher - n_invoke advances
 * only in rv_step - while chained tier-1 code jumps from block to block
 * without returning there, so a chained successor never promotes. Direct
 * chaining has that effect already and stays enabled; specializing indirect
 * jumps as well can absorb the region's entry block too, leaving nothing in
 * it that tier two ever sees. That is the whole of the difference, and it is
 * a limitation rather than a choice, so this is not a configuration option;
 * set it to 1 to measure the trade-off.
 */
#define JIT_INDIRECT_TARGETS (!RV32_HAS(T2C))

/* Guest memory base pinning.
 *
 * User-mode translation keeps mem_base in a host register the allocator never
 * hands out, so a guest access needs no address arithmetic at all on x86-64
 * System V (R12 as the SIB base, the guest register as the index) and only an
 * integer add on Arm64 (R19). System mode routes every access through the MMU
 * handler, and the Windows x64 ABI leaves no spare non-volatile register.
 *
 * Both the prologue that loads the register and every site that reads it are
 * spelled with these, so a pinned base can never drift out of lockstep with
 * its uses.
 */
#if defined(__x86_64__) && !defined(_WIN32) && !RV32_HAS(SYSTEM)
#define JIT_INDEXED_GUEST_MEM 1
#else
#define JIT_INDEXED_GUEST_MEM 0
#endif

#if defined(__aarch64__) && !RV32_HAS(SYSTEM)
#define JIT_BASED_GUEST_MEM 1
#else
#define JIT_BASED_GUEST_MEM 0
#endif

/* Worst-case jump targets per instruction.  Under SYSTEM_MMIO with the
 * inline dTLB fast path enabled, a single load/store can record:
 *   - 6 fast-path gating branches (align, vpn, valid, perm, dirty for stores,
 *     range) patched to the slow-path label
 *   - 2 branches selecting bare or paged address translation
 *   - 1 unconditional B at the end of the fast path patched to the merge label
 *   - 4 slow-path branches (LOC_0, LOC_1, TRAP, NORMAL)
 *   - 1 implicit jump inside emit_exit
 * Total: 14.  Branch epilogues are still 5 (2x emit_jmp + 2x emit_exit +
 * 1x emit_jump_target_offset).  We round up to 16 for headroom.
 *
 * Each load or store also has a misaligned-access guard: the branch to its
 * stub, the stub's emit_exit, and the jump around an inline stub.
 * block_jump_budget() adds those, for every access of a fused run.
 */
#define JUMPS_PER_INSN 16
#define JUMPS_PER_GUARD 3

/* The number of accesses in a record that may carry an alignment guard. */
static uint32_t guarded_accesses(const rv_insn_t *ir)
{
    switch (ir->opcode) {
    case rv_insn_fuse3:
    case rv_insn_fuse4:
        return ir->imm2;
    case rv_insn_lh:
    case rv_insn_lhu:
    case rv_insn_lw:
    case rv_insn_sh:
    case rv_insn_sw:
    case rv_insn_fuse11:
    case rv_insn_fuse13:
#if RV32_HAS(EXT_C)
    case rv_insn_clw:
    case rv_insn_csw:
    case rv_insn_clwsp:
    case rv_insn_cswsp:
#endif
        return 1;
    default:
        return 0;
    }
}

/* Worst-case jump targets a block records, including 2 for a page-terminated
 * epilogue (emit_jmp + emit_exit).
 */
static uint32_t block_jump_budget(const block_t *block)
{
    uint32_t n = 2;
    const rv_insn_t *ir = block->ir_head;
    for (uint32_t i = 0; i < block->n_insn; i++, ir = block_next_ir(ir))
        n += JUMPS_PER_INSN + guarded_accesses(ir) * JUMPS_PER_GUARD;
    return n;
}

/* Check if branch history table entry should trigger JIT translation */
static inline bool bht_should_translate(const branch_history_table_t *bt,
                                        int idx
#if RV32_HAS(SYSTEM)
                                        ,
                                        uint32_t csr_satp
#endif
)
{
    if (!JIT_INDIRECT_TARGETS)
        return false;
    /* times is the validity signal: a slot never observed still holds zero.
     * Testing the recorded PC instead would reject a genuine target at guest
     * address 0, and would not reject anything else, since the table
     * initializes PC to all ones rather than to zero.
     */
    if (bt->times[idx] < IN_JUMP_THRESHOLD)
        return false;
#if RV32_HAS(SYSTEM)
    if (bt->satp[idx] != csr_satp)
        return false;
#endif
    return true;
}

/* Keep guard emission and chained translation in the same frequency order.
 * Stable ties favor the lower slot; unobserved and foreign-context targets
 * never consume the limited guard budget.
 */
static int bht_select_targets(const branch_history_table_t *bt,
                              riscv_t *rv UNUSED,
                              int targets[IN_JUMP_TARGETS])
{
    int count = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
#if RV32_HAS(SYSTEM)
        if (!bht_should_translate(bt, i, rv->csr_satp))
#else
        if (!bht_should_translate(bt, i))
#endif
            continue;
        int pos = count;
        if (pos == IN_JUMP_TARGETS) {
            /* Full: displace the weakest kept target, or keep them all. */
            if (bt->times[i] <= bt->times[targets[IN_JUMP_TARGETS - 1]])
                continue;
            pos = IN_JUMP_TARGETS - 1;
        } else {
            count++;
        }
        while (pos > 0 && bt->times[i] > bt->times[targets[pos - 1]]) {
            targets[pos] = targets[pos - 1];
            pos--;
        }
        targets[pos] = i;
    }
    return count;
}

#if defined(__x86_64__)
/* indicate where the immediate value is in the emitted jump instruction.
 * For conditional jumps (JNE, JE, etc.), add 2 to skip the 2-byte opcode
 * (0x0f + condition). For unconditional jumps (JMP), add 1 to skip the
 * 1-byte opcode (0xe9).
 */
#define JUMP_LOC_0 jump_loc_0 + 2
#define JUMP_TRAP jump_trap + 2
#define JUMP_NORMAL jump_normal + 1
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
/* indicate where the immediate value is in the emitted jump instruction.
 * For ARM64, the offset is embedded within the instruction word itself,
 * so no additional offset adjustment is needed.
 */
#define JUMP_LOC_0 jump_loc_0
#define JUMP_TRAP jump_trap
#define JUMP_NORMAL jump_normal
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
    AS_ADDS = 1,
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
    DP2_SDIV = 0x1ac00c00U, /* 0001_1010_1100_0000_0000_1100_0000_0000 */
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
    COND_HI = 8,
    COND_LS = 9,
    COND_GE = 10,
    COND_LT = 11,
    COND_GT = 12,
    COND_LE = 13,
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
 * Inline fast paths may use callee-saved registers as scratch temporaries, so
 * they must stay in the saved non-volatile set even if the generic allocator
 * does not hand them out.
 */
#if defined(_WIN32)
static const int nonvolatile_reg[] = {RBP, RBX, RDI, RSI, R12, R13, R14, R15};
static const int parameter_reg[] = {RCX, RDX, R8, R9};
static struct host_reg register_map[] = {
    {RAX, -1, 0, 0}, {R10, -1, 0, 0}, {RDX, -1, 0, 0}, {R8, -1, 0, 0},
    {R9, -1, 0, 0},  {R14, -1, 0, 0}, {R15, -1, 0, 0}, {RDI, -1, 0, 0},
    {RSI, -1, 0, 0}, {RBX, -1, 0, 0},
};
static int temp_reg = R11;
#else
static const int nonvolatile_reg[] = {RBP, RBX, R12, R13, R14, R15};
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
 *   r19       Guest memory base in user-mode JIT (outside register_map)
 *   r20       Guest cycle count while generated code runs (cycle_reg)
 *   r19 - r23 Callee-saved registers
 *   r24       Temp - used for generating 32-bit immediates
 *   r25       Temp - used for modulous calculations
 *
 * Note: R18 is reserved on Apple and Windows platforms (platform register) and
 * must not be used. R16/R17 (IP0/IP1) are intra-procedure-call scratch
 * registers that may be corrupted by linker veneers across BLR calls; they are
 * safe for use within straight-line JIT code but values must not be expected to
 * survive function calls.
 */
static struct host_reg register_map[] = {
    {R5, -1, 0, 0},  {R6, -1, 0, 0},  {R7, -1, 0, 0},  {R9, -1, 0, 0},
    {R11, -1, 0, 0}, {R12, -1, 0, 0}, {R13, -1, 0, 0}, {R14, -1, 0, 0},
    {R15, -1, 0, 0}, {R16, -1, 0, 0}, {R17, -1, 0, 0}, {R26, -1, 0, 0},
};
#endif

static const int n_host_regs =
    ARRAY_SIZE(register_map); /* the number of avavliable host register */

/* Alignment, in bytes, that a passed guard has proven for each guest register
 * since it was last written in the current block; 0 if unknown. Every write to
 * a guest register goes through map_vm_reg() or its reserved variants, or marks
 * the host register dirty, and each of those clears the entry, as does
 * reset_reg() after a call into C.
 */
static uint8_t reg_align[N_RV_REGS];

/* Group 1 ALU opcodes (x86-64 encoding): 0x81 takes an imm32, 0x83 a
 * sign-extended imm8. On Arm64 the opcode is ignored and the ALU_* selector
 * picks the operation.
 */
#define ALU_GRP1_OPCODE 0x81
#define ALU_GRP1_IMM8_OPCODE 0x83

/* ALU operation selectors for group 1 operations.
 * On x86-64: ModR/M reg field values. On Arm64: switch case selectors
 * in emit_alu32_imm32() that map to native instructions.
 */
#define ALU_ADD 0
#define ALU_OR 1
#define ALU_AND 4
#define ALU_XOR 6

/* Shift and rotate operation identifiers: x86-64 ModR/M reg field values,
 * which the Arm64 emitters also accept for the shifts.
 */
#define SHIFT_ROL 0
#define SHIFT_ROR 1
#define SHIFT_SHL 4
#define SHIFT_SHR 5
#define SHIFT_SAR 7

/* Shift opcodes (x86-64 encoding). On Arm64, emit_alu32_imm8() and
 * emit_alu32() map SHIFT_* values to native instructions.
 */
#define SHIFT_IMM_OPCODE 0xc1 /* Shift by immediate */
#define SHIFT_REG_OPCODE 0xd3 /* Shift by CL */

/* Group 3 unary opcode (x86-64): NEG is /3, DIV /6, IDIV /7. */
#define UNARY_GRP3_OPCODE 0xf7
#define UNARY_NEG 3
#define UNARY_DIV 6
#define UNARY_IDIV 7

/* Regions can omit an eviction store only when every path overwrites that
 * value before a read, trap, helper, or leaf. Other translations keep all bits.
 */
static uint32_t spill_live_regs = UINT32_MAX;

static inline void set_dirty(int reg_idx, bool is_dirty)
{
    for (int i = 0; i < n_host_regs; i++) {
        /* ignore nonvolatile and parameter registers */
        if (register_map[i].reg_idx != reg_idx)
            continue;

        register_map[i].dirty = is_dirty;
        if (is_dirty && register_map[i].vm_reg_idx >= 0)
            reg_align[register_map[i].vm_reg_idx] = 0;
        return;
    }
}

static inline bool host_reg_maps_x0(int reg_idx)
{
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].reg_idx != reg_idx)
            continue;
        return register_map[i].vm_reg_idx == rv_reg_zero;
    }
    return false;
}

static inline uint32_t index_hash(uint64_t key, unsigned bits)
{
    return (uint32_t) ((key * 0x61c8864680b583ebull) >> (64 - bits));
}

static inline uint32_t offset_hash(uint32_t pc, uint32_t satp)
{
    return index_hash(((uint64_t) satp << 32) | pc, OFFSET_INDEX_BITS);
}

#if RV32_HAS(SYSTEM)
static inline uint32_t space_hash(uint32_t satp)
{
    return index_hash(satp, SPACE_INDEX_BITS);
}

static inline uint32_t page_hash(uint32_t va, uint32_t satp)
{
    return index_hash(((uint64_t) satp << 32) | (va >> RV_PG_SHIFT),
                      PAGE_INDEX_BITS_JIT);
}
#endif

/* Empty the offset map, every index over it and the jumps pending into it */
static void offset_map_reset(struct jit_state *state)
{
    state->n_blocks = 0;
    memset(state->offset_index, -1, sizeof(state->offset_index));
#if JIT_LINK_ALL_EDGES
    state->n_pending = 0;
    memset(state->pending_index, -1, sizeof(state->pending_index));
#endif
#if RV32_HAS(SYSTEM)
    memset(state->space_index, -1, sizeof(state->space_index));
    memset(state->page_index, -1, sizeof(state->page_index));
#endif
}

static inline void offset_map_insert(struct jit_state *state, block_t *block)
{
    assert(state->n_blocks < MAX_BLOCKS);

    const int32_t idx = state->n_blocks++;
    struct offset_map *map_entry = &state->offset_map[idx];
    map_entry->pc = block->pc_start;
    map_entry->offset = state->offset;

    int32_t *head =
        &state->offset_index[offset_hash(block->pc_start, BLOCK_SATP(block))];
    map_entry->next = *head;
    *head = idx;
#if RV32_HAS(SYSTEM)
    map_entry->satp = block->satp;
    map_entry->retired = false;
    head = &state->space_index[space_hash(block->satp)];
    map_entry->next_in_space = *head;
    *head = idx;
    head = &state->page_index[page_hash(block->pc_start, block->satp)];
    map_entry->next_in_page = *head;
    *head = idx;
#endif
}

/* The live entry translated for pc in address space satp, if any */
static struct offset_map *offset_map_find(struct jit_state *state,
                                          uint32_t pc,
                                          uint32_t satp UNUSED)
{
    for (int32_t i = state->offset_index[offset_hash(pc, satp)]; i >= 0;
         i = state->offset_map[i].next) {
        struct offset_map *entry = &state->offset_map[i];
        if (entry->pc != pc)
            continue;
#if RV32_HAS(SYSTEM)
        if (entry->satp != satp || entry->retired)
            continue;
#endif
        return entry;
    }
    return NULL;
}

#if RV32_HAS(SYSTEM)
void jit_retire_space(struct jit_state *state, uint32_t satp)
{
    /* Retired entries leave the chain, so each flush visits only the code
     * translated since the previous one.
     */
    int32_t *link = &state->space_index[space_hash(satp)];
    while (*link >= 0) {
        struct offset_map *entry = &state->offset_map[*link];
        if (entry->satp == satp) {
            entry->retired = true;
            *link = entry->next_in_space;
        } else {
            link = &entry->next_in_space;
        }
    }
}

bool jit_page_has_code(const struct jit_state *state,
                       uint32_t va,
                       uint32_t satp)
{
#if !RV32_HAS(BLOCK_CHAINING)
    /* Blocks may then span pages, so any code of the address space may hold
     * instructions of this page.
     */
    (void) va;
    for (int32_t i = state->space_index[space_hash(satp)]; i >= 0;
         i = state->offset_map[i].next_in_space) {
        if (state->offset_map[i].satp == satp)
            return true;
    }
    return false;
#else
    const uint32_t page = va & ~(RV_PG_SIZE - 1);
    for (int32_t i = state->page_index[page_hash(va, satp)]; i >= 0;
         i = state->offset_map[i].next_in_page) {
        const struct offset_map *entry = &state->offset_map[i];
        if (!entry->retired && entry->satp == satp &&
            (entry->pc & ~(RV_PG_SIZE - 1)) == page)
            return true;
    }
    return false;
#endif
}
#endif

#if !defined(__APPLE__)
#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *) (addr), (char *) (addr) + (size));
#endif

static bool should_flush = false;

#if defined(__APPLE__) && defined(__aarch64__)
/* Track JIT write mode to batch write protection toggling.
 * On Apple Silicon, rapid toggling of write protection can cause
 * cache coherency issues. We enable write mode at the start of
 * translation and disable it only after all code generation and
 * jump patching is complete.
 *
 * Must be thread-local because pthread_jit_write_protect_np operates
 * per-thread. A shared flag would cause race conditions if multiple
 * threads translate simultaneously.
 */
static __thread bool jit_write_mode = false;

static inline void jit_enter_write_mode(void)
{
    if (!jit_write_mode) {
        pthread_jit_write_protect_np(false);
        jit_write_mode = true;
    }
}

static inline void jit_exit_write_mode(void)
{
    if (jit_write_mode) {
        pthread_jit_write_protect_np(true);
        jit_write_mode = false;
    }
}
#endif

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
    /* If not in write mode (e.g., during initial setup), toggle temporarily.
     * During normal translation, jit_translate maintains write mode to avoid
     * rapid toggling which can cause cache coherency issues.
     */
    bool need_toggle = !jit_write_mode;
    if (need_toggle)
        pthread_jit_write_protect_np(false);
    memcpy(state->buf + state->offset, data, len);
    if (need_toggle) {
        sys_icache_invalidate(state->buf + state->offset, len);
        pthread_jit_write_protect_np(true);
    }
#else
    memcpy(state->buf + state->offset, data, len);
    sys_icache_invalidate(state->buf + state->offset, len);
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

/* The ModRM mod for a memory operand: no displacement, disp8, or disp32. A
 * base of RBP or R13 has no form without a displacement.
 */
static inline int x64_disp_mod(int base, int32_t d)
{
    if (d == 0 && (base & 7) != RBP)
        return 0x00;
    return (int8_t) d == d ? 0x40 : 0x80;
}

static inline void emit_disp(struct jit_state *state, int mod, int32_t d)
{
    if (mod == 0x40)
        emit1(state, d);
    else if (mod == 0x80)
        emit4(state, d);
}

static inline void emit_modrm_and_displacement(struct jit_state *state,
                                               int r,
                                               int m,
                                               int32_t d)
{
    const int mod = x64_disp_mod(m, d);
    emit_modrm(state, mod, r, m);
    emit_disp(state, mod, d);
}

/* ModRM, SIB, and displacement for [base + index * (1 << scale) + d]. */
static inline void emit_sib_operand(struct jit_state *state,
                                    int r,
                                    int base,
                                    int index,
                                    int scale,
                                    int32_t d)
{
    const int mod = x64_disp_mod(base, d);
    emit_modrm(state, mod, r, 4);
    emit1(state, (scale << 6) | ((index & 7) << 3) | (base & 7));
    emit_disp(state, mod, d);
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
static void emit_add_imm(struct jit_state *state,
                         bool is64,
                         bool set_flags,
                         int dst,
                         int src,
                         int64_t imm);

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

/* Patch branch instruction without write protection toggle.
 * Caller must handle write protection and cache maintenance.
 */
static void patch_branch_imm(struct jit_state *state,
                             uint32_t offset,
                             int32_t imm)
{
    assert((imm & 3) == 0);
    uint32_t insn;
    imm >>= 2;
    memcpy(&insn, state->buf + offset, sizeof(uint32_t));
    if ((insn & 0xfe000000U) == 0x54000000U /* Conditional branch immediate. */
        || (insn & 0x7e000000U) ==
               0x34000000U) { /* Compare and branch immediate. */
        assert((imm >> 19) == INT64_C(-1) || (imm >> 19) == 0);
        insn = (insn & ~(UINT32_C(0x7ffff) << 5)) | ((imm & 0x7ffff) << 5);
    } else if ((insn & 0x7c000000U) == 0x14000000U) {
        /* Unconditional branch immediate.  */
        assert((imm >> 26) == INT64_C(-1) || (imm >> 26) == 0);
        insn = (insn & ~UINT32_C(0x03ffffff)) | (imm & 0x03ffffffU);
    } else {
        assert(false);
        insn = BAD_OPCODE;
    }
    memcpy(state->buf + offset, &insn, sizeof(uint32_t));
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

#if defined(__x86_64__)
/* Encode an ALU r/m32, r32 form without claiming the destination was written:
 * CMP shares this encoding but only sets flags.
 */
static inline void emit_alu32_raw(struct jit_state *state,
                                  int op,
                                  int src,
                                  int dst)
{
    /* The REX prefix and ModRM byte are emitted.
     * The MR encoding is utilized when a choice is available. The 'src' is
     * often used as an opcode extension.
     */
    if (src & 8 || dst & 8)
        emit_basic_rex(state, 0, src, dst);
    emit1(state, op);
    emit_modrm_reg2reg(state, src, dst);
}
#endif

static inline void emit_alu32(struct jit_state *state, int op, int src, int dst)
{
#if defined(__x86_64__)
    emit_alu32_raw(state, op, src, dst);
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
    set_dirty(dst, true);
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
        emit_add_imm(state, false, false, dst, dst, imm);
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
    set_dirty(dst, true);
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
    /* Immediate shifts are aliases of the 32-bit bitfield moves:
     * LSL #n = UBFM #(-n % 32), #(31 - n); LSR #n = UBFM #n, #31;
     * ASR #n = SBFM #n, #31. Like the register forms, count modulo 32.
     */
    const uint32_t n = (uint8_t) imm & 31;
    const uint32_t ubfm = 0x53000000U, sbfm = 0x13000000U;
    switch (src) {
    case 4:
        emit_a64(state, ubfm | ((-n & 31) << 16) | ((31 - n) << 10) |
                            (dst << 5) | dst);
        break;
    case 5:
        emit_a64(state, ubfm | (n << 16) | (31 << 10) | (dst << 5) | dst);
        break;
    case 7:
        emit_a64(state, sbfm | (n << 16) | (31 << 10) | (dst << 5) | dst);
        break;
    default:
        __UNREACHABLE;
        break;
    }
    set_dirty(dst, true);
#endif
}

static inline void UNUSED emit_alu64(struct jit_state *state,
                                     int op,
                                     int src,
                                     int dst)
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

#if RV32_HAS(EXT_M)
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

/* Register to register mov (preserves all 64 bits including sign extension) */
static inline void emit_mov(struct jit_state *state, int src, int dst)
{
#if defined(__x86_64__)
    emit_alu64(state, 0x89, src, dst);
#elif defined(__aarch64__)
    /* Use 64-bit ORR with zero register: MOV Xd, Xm = ORR Xd, XZR, Xm
     * This preserves all 64 bits including any sign extension in the upper 32.
     * Previous implementation used 32-bit ADD which zero-extended the result.
     */
    emit_logical_register(state, true, LOG_ORR, dst, RZ, src);
    set_dirty(dst, true);
#endif
}

/* Copy an RV32 result and clear the upper bits, even for a self-move, so a
 * folded identity cannot pass a stale upper half through to a consumer that
 * reads the host register at full width.
 */
static inline void emit_mov32(struct jit_state *state, int src, int dst)
{
#if defined(__x86_64__)
    emit_alu32(state, 0x89, src, dst);
#elif defined(__aarch64__)
    emit_logical_register(state, false, LOG_ORR, dst, RZ, src);
    set_dirty(dst, true);
#endif
}

#if RV32_HAS(EXT_M)
#if defined(__x86_64__)
/* IMUL dst, src: two-operand multiply, 32- or 64-bit. */
static void emit_imul(struct jit_state *state, bool is64, int dst, int src)
{
    emit_basic_rex(state, is64, dst, src);
    emit1(state, 0x0f);
    emit1(state, 0xaf);
    emit_modrm_reg2reg(state, dst, src);
    set_dirty(dst, true);
}
#endif

static void emit_mul32(struct jit_state *state, int lhs, int rhs, int dst)
{
#if defined(__x86_64__)
    if (dst == rhs) {
        rhs = lhs;
    } else if (dst != lhs) {
        emit_mov32(state, lhs, dst);
    }
    /* Signed and unsigned multiplication have identical low 32 bits. */
    emit_imul(state, false, dst, rhs);
#elif defined(__aarch64__)
    emit_dataproc_3source(state, false, DP3_MADD, dst, lhs, rhs, RZ);
#endif
}
#endif

/* Like libriscv 3f06e04e's shift_rr, keep all three operands explicit.
 * The CPU masks 32-bit register shift counts to five bits on both hosts.
 */
static void emit_shift32(struct jit_state *state,
                         int kind,
                         int lhs,
                         int count,
                         int dst)
{
    assert(kind == SHIFT_SHL || kind == SHIFT_SHR || kind == SHIFT_SAR);
#if defined(__x86_64__)
    if (state->has_bmi2) {
        /* VEX.NDS.LZ.66/F2/F3.0F38.W0 F7 /r: SHLX/SHRX/SARX.
         * ModRM.reg is dst, ModRM.r/m is lhs, VEX.vvvv is count.
         * BMI2 uses only GPRs, so no AVX OS state is required.
         */
        emit1(state, 0xc4);
        emit1(state, (!(dst & 8) << 7) | 0x40 | (!(lhs & 8) << 5) | 2);
        emit1(state, ((~count & 15) << 3) | (kind == SHIFT_SHL   ? 1
                                             : kind == SHIFT_SHR ? 3
                                                                 : 2));
        emit1(state, 0xf7);
        emit_modrm_reg2reg(state, dst, lhs);
    } else {
#if defined(_WIN32)
        /* RCX holds rv on Windows and is outside the register allocator. */
        emit_push(state, RCX);
#endif
        emit_mov(state, count, RCX);
        if (lhs != dst)
            emit_mov(state, lhs, dst);
        emit_alu32(state, SHIFT_REG_OPCODE, kind, dst);
#if defined(_WIN32)
        emit_pop(state, RCX);
#endif
    }
#elif defined(__aarch64__)
    emit_dataproc_2source(state, false,
                          kind == SHIFT_SHL   ? DP2_LSLV
                          : kind == SHIFT_SHR ? DP2_LSRV
                                              : DP2_ASRV,
                          dst, lhs, count);
#endif
    set_dirty(dst, true);
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
    if (!imm) {
        /* TEST sets ZF and SF as CMP 0 does and clears CF and OF. */
        emit_alu32_raw(state, 0x85, dst, dst);
        return;
    }
    emit_alu32_raw(state, 0x81, 7, dst); /* GRP1 /7 = CMP r/m32, imm32 */
    emit4(state, imm);
#elif defined(__aarch64__)
    if (!imm) {
        emit_addsub_register(state, false, AS_SUBS, RZ, dst, RZ);
        return;
    }
    /* CMP #imm12, or CMN #imm12 for a small negative value: both leave the
     * flags CMP with a register would, without materializing the constant.
     */
    if (imm > -4096 && imm < 4096) {
        emit_add_imm(state, false, true, RZ, dst, -(int64_t) imm);
        return;
    }
    emit_load_imm(state, R10, imm);
    emit_addsub_register(state, false, AS_SUBS, RZ, dst, R10);
#endif
}

static inline void emit_cmp32(struct jit_state *state, int src, int dst)
{
#if defined(__x86_64__)
    emit_alu32_raw(state, 0x39, src, dst);
#elif defined(__aarch64__)
    emit_addsub_register(state, false, AS_SUBS, RZ, dst, src);
#endif
}

static inline void emit_jcc_offset(struct jit_state *state, int code)
{
#if defined(__x86_64__)
    /* unconditional jump instruction does not have 0x0f prefix */
    if (code != JCC_JMP)
        emit1(state, 0x0f);
    emit1(state, code);
    emit4(state, 0);
#elif defined(__aarch64__)
    switch (code) {
    case JCC_JE: /* BEQ */
        code = COND_EQ;
        break;
    case JCC_JNE: /* BNE */
        code = COND_NE;
        break;
    case JCC_JL: /* BLT */
        code = COND_LT;
        break;
    case JCC_JGE: /* BGE */
        code = COND_GE;
        break;
    case JCC_JB: /* BLTU */
        code = COND_LO;
        break;
    case JCC_JAE: /* BGEU */
        code = COND_HS;
        break;
    case JCC_JBE:
        code = COND_LS;
        break;
    case JCC_JA:
        code = COND_HI;
        break;
    case JCC_JLE:
        code = COND_LE;
        break;
    case JCC_JG:
        code = COND_GT;
        break;
    case JCC_JMP: /* AL */
        code = COND_AL;
        break;
    default:
        assert(NULL);
        __UNREACHABLE;
    }
    emit_a64(state, BR_Bcond | (0 << 5) | code);
#endif
}

/* Move a lone x0 operand of "lhs <jcc> rhs" to the right, swapping the
 * condition, so the emitter tests the other register against zero.
 */
static inline void branch_zero_rhs(uint8_t *lhs, uint8_t *rhs, int *jcc)
{
    if (*lhs != rv_reg_zero || *rhs == rv_reg_zero)
        return;
    *lhs = *rhs;
    *rhs = rv_reg_zero;
    switch (*jcc) {
    case JCC_JL:
        *jcc = JCC_JG;
        break;
    case JCC_JGE:
        *jcc = JCC_JLE;
        break;
    case JCC_JB:
        *jcc = JCC_JA;
        break;
    case JCC_JAE:
        *jcc = JCC_JBE;
        break;
    default:
        break; /* equality is symmetric */
    }
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
#if defined(__x86_64__)
/* Emit the opcode for a guest load of @size. MOVZX and MOVSX are two-byte;
 * a word load is a plain MOV. The caller has already emitted any REX and
 * follows with the addressing bytes.
 */
static inline void emit_load_opcode(struct jit_state *state,
                                    enum operand_size size,
                                    bool sign)
{
    if (size == S8 || size == S16) {
        emit1(state, 0x0f);
        emit1(state, (sign ? 0xbe : 0xb6) + (size == S16));
        return;
    }
    assert(size == S32 && !sign);
    emit1(state, 0x8b);
}

/* Emit the opcode for a guest store of @size from a register. A halfword
 * store needs the operand-size override, which must precede any REX, so the
 * caller emits that itself before calling this.
 */
static inline void emit_store_opcode(struct jit_state *state,
                                     enum operand_size size)
{
    assert(size == S8 || size == S16 || size == S32);
    emit1(state, size == S8 ? 0x88 : 0x89);
}

#if JIT_INDEXED_GUEST_MEM
/* R12 is the pinned RAM base, so even offset zero needs no displacement.
 * Retain the same SIB address and select its shortest signed displacement.
 * Inspired by libriscv 7c6c877e's direct arena load/store lowering.
 */
static void emit_guest_indexed_operand(struct jit_state *state,
                                       int reg,
                                       int index,
                                       int32_t offset)
{
    emit_sib_operand(state, reg, R12, index, 0, offset);
}
#endif

#endif

static inline void emit_load(struct jit_state *state,
                             enum operand_size size,
                             int src,
                             int dst,
                             int32_t offset)
{
    if (host_reg_maps_x0(dst)) {
        /* if dst is x0, load 0x0 into host register */
        emit_load_imm(state, dst, 0x0);
        set_dirty(dst, true);
        return;
    }

#if defined(__x86_64__)
    if (src & 8 || dst & 8)
        emit_basic_rex(state, 0, dst, src);
    emit_load_opcode(state, size, false);
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

/* RV32 has no sign-extending word load, so this never widens past 32 bits:
 * the destination's upper half is always zero on return. Guest registers are
 * 32 bits, and a mapped host register that kept a sign extension would move a
 * later address computation 4 GiB down, so keep that guarantee enforced.
 */
static inline void emit_load_sext(struct jit_state *state,
                                  enum operand_size size,
                                  int src,
                                  int dst,
                                  int32_t offset)
{
    assert(size == S8 || size == S16);

    if (host_reg_maps_x0(dst)) {
        /* if dst is x0, load 0x0 into host register */
        emit_load_imm(state, dst, 0x0);
        set_dirty(dst, true);
        return;
    }

#if defined(__x86_64__)
    if (src & 8 || dst & 8)
        emit_basic_rex(state, 0, dst, src);
    emit_load_opcode(state, size, true);
    emit_modrm_and_displacement(state, dst, src, offset);
#elif defined(__aarch64__)
    emit_loadstore_imm(state, size == S8 ? LS_LDRSBW : LS_LDRSHW, dst, src,
                       offset);
#endif

    set_dirty(dst, !offset);
}

/* Copy the low 32 bits of @src into @dst, sign-extended to 64 bits. Both
 * hosts fold the move and the widening into one instruction, so a caller that
 * wants a widened copy never needs a separate mov. Passing src == dst widens
 * in place.
 */
static inline void UNUSED emit_mov_sext(struct jit_state *state,
                                        int src,
                                        int dst)
{
#if defined(__x86_64__)
    /* MOVSXD r64, r/m32 */
    emit_basic_rex(state, 1, dst, src);
    emit1(state, 0x63);
    emit_modrm_reg2reg(state, dst, src);
#elif defined(__aarch64__)
    /* SXTW Xd, Wn is SBFM Xd, Xn, #0, #31
     * Encoding: sf=1, opc=00, N=1, immr=0, imms=31
     * = 0x93407C00 | (Rn << 5) | Rd
     */
    uint32_t insn = 0x93407C00 | ((uint32_t) src << 5) | (uint32_t) dst;
    emit_a64(state, insn);
#endif

    set_dirty(dst, true);
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
    /* The W-form MOVZ/MOVK pair zeroes bits 63:32, so only a non-negative
     * immediate may take it. A negative one has to go through the X-form to
     * keep its sign across the upper half.
     */
    emit_movewide_imm(state, imm < 0 || (int32_t) imm != imm, dst, imm);
#endif
}

#if defined(__aarch64__)
/* dst = src + imm, using a 12-bit ADD or SUB immediate when it fits and R10
 * otherwise; set_flags selects ADDS/SUBS.
 */
static void emit_add_imm(struct jit_state *state,
                         bool is64,
                         bool set_flags,
                         int dst,
                         int src,
                         int64_t imm)
{
    if (imm >= 0 && imm < 4096) {
        emit_addsub_imm(state, is64, set_flags ? AS_ADDS : AS_ADD, dst, src,
                        imm);
    } else if (imm < 0 && imm > -4096) {
        emit_addsub_imm(state, is64, set_flags ? AS_SUBS : AS_SUB, dst, src,
                        -imm);
    } else {
        emit_load_imm_sext(state, R10, imm);
        emit_addsub_register(state, is64, set_flags ? AS_ADDS : AS_ADD, dst,
                             src, R10);
    }
}
#endif

/* Use integer arithmetic so negative offsets do not form a pointer before
 * the allocation on backends without indexed guest-memory accesses.
 * Return the displacement left for the memory instruction.
 */
static inline int32_t UNUSED emit_guest_address(struct jit_state *state,
                                                memory_t *m UNUSED,
                                                int base,
                                                int32_t offset)
{
#if JIT_BASED_GUEST_MEM
    emit_addsub_register(state, true, AS_ADD, temp_reg, R19, base);
    if (offset >= -256 && offset < 256)
        return offset;
    emit_add_imm(state, true, false, temp_reg, temp_reg, offset);
#else
    emit_load_imm_sext(state, temp_reg, (intptr_t) m->mem_base + offset);
    emit_alu64(state, 0x01, base, temp_reg);
#endif
    return 0;
}

/* The user-mode x86-64 entry pins mem_base in R12 for the entire JIT call. */
static inline void emit_guest_load(struct jit_state *state,
                                   memory_t *m UNUSED,
                                   enum operand_size size,
                                   int base,
                                   int dst,
                                   int32_t offset,
                                   bool sign)
{
#if JIT_INDEXED_GUEST_MEM
    if (host_reg_maps_x0(dst)) {
        emit_load_imm(state, dst, 0);
        return;
    }
    assert((base & 7) != RSP);
    emit_rex(state, 0, !!(dst & 8), !!(base & 8), 1);
    emit_load_opcode(state, size, sign);
    emit_guest_indexed_operand(state, dst, base, offset);
    set_dirty(dst, true);
#else
    int32_t displacement = emit_guest_address(state, m, base, offset);
    if (sign)
        emit_load_sext(state, size, temp_reg, dst, displacement);
    else
        emit_load(state, size, temp_reg, dst, displacement);
    /* A nonzero guest displacement is not a clean register-file reload. */
    set_dirty(dst, true);
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
    emit_store_opcode(state, size);
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

    /* Only register-file writeback makes a guest register clean. A guest
     * store may also use an immediate displacement without saving rv->X.
     */
    if (offset && dst == parameter_reg[0])
        set_dirty(src, false);
}

static inline void emit_jmp(struct jit_state *state,
                            uint32_t target_pc,
                            uint32_t target_satp UNUSED)
{
#if defined(__x86_64__)
    emit1(state, JCC_JMP);
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

/* Guest cycle counting.
 *
 * Blocks jump to one another inside generated code, so the dispatcher sees only
 * the first block of a chain and cannot charge the rest. Instead, each block
 * adds its cost to cycle_reg on entry, unless the program reads no counter
 * (jit_entry_cycles), in which case the dispatcher's charge is enough. While
 * generated code runs, the register stands in for rv->csr_cycle: it is loaded
 * on entry, stored on exit, and stored before and reloaded after any call,
 * since a callee may read or advance the counter. It is callee-saved, so it
 * survives the calls themselves.
 */
#if defined(__x86_64__)
static const int cycle_reg = RBP;
#elif defined(__aarch64__)
static const int cycle_reg = R20;
#endif

/* The block whose records are being emitted, for unretired_after(). */
static const block_t *cur_block;

/* A block charges all its instructions on entry. A trap at pc ends it early,
 * so the instructions after pc, which never retire, are refunded. Fused
 * records span several 4-byte instructions, and their members trap at their
 * own pc. Programs that read no counter are charged per chain instead.
 */
static uint32_t unretired_after(const riscv_t *rv, uint32_t pc)
{
    if (rv->jit_entry_cycles || !cur_block)
        return 0;
    uint32_t n = 0;
    for (const rv_insn_t *ir = cur_block->ir_head, *next; ir; ir = next) {
        next = ir == cur_block->ir_tail ? NULL : block_next_ir(ir);
        const uint32_t end = next ? next->pc : cur_block->pc_end;
        const uint32_t count = end - ir->pc > 4 ? (end - ir->pc) / 4 : 1;
        if (pc < ir->pc)
            n += count;
        else if (pc < end && count > 1)
            n += count - 1 - (pc - ir->pc) / 4;
    }
    return n;
}

static void emit_cycle_count(struct jit_state *state, uint32_t n)
{
#if defined(__x86_64__)
    /* add rbp, imm: block costs nearly always fit the 8-bit form */
    if (n < 0x80) {
        emit_alu64(state, 0x83, 0, cycle_reg);
        emit1(state, n);
    } else {
        emit_alu64_imm32(state, 0x81, 0, cycle_reg, n);
    }
#elif defined(__aarch64__)
    if (n < 0x1000) {
        emit_addsub_imm(state, true, AS_ADD, cycle_reg, cycle_reg, n);
    } else {
        emit_load_imm(state, temp_imm_reg, n);
        emit_addsub_register(state, true, AS_ADD, cycle_reg, cycle_reg,
                             temp_imm_reg);
    }
#endif
}

/* Subtract n from cycle_reg. */
static void emit_cycle_refund(struct jit_state *state, uint32_t n)
{
#if defined(__x86_64__)
    if (n < 0x80) {
        emit_alu64(state, 0x83, 5, cycle_reg);
        emit1(state, n);
    } else {
        emit_alu64_imm32(state, 0x81, 5, cycle_reg, n);
    }
#elif defined(__aarch64__)
    if (n < 0x1000) {
        emit_addsub_imm(state, true, AS_SUB, cycle_reg, cycle_reg, n);
    } else {
        emit_load_imm(state, temp_imm_reg, n);
        emit_addsub_register(state, true, AS_SUB, cycle_reg, cycle_reg,
                             temp_imm_reg);
    }
#endif
}

/* Arm64 reaches the counter with a scaled 12-bit offset. */
_Static_assert(offsetof(riscv_t, csr_cycle) % 8 == 0 &&
                   offsetof(riscv_t, csr_cycle) / 8 < 4096,
               "csr_cycle must be reachable by a scaled LDR/STR offset");

/* Move rv->csr_cycle into cycle_reg (load) or back (store). */
static void emit_cycle_move(struct jit_state *state, bool load)
{
    const uint32_t offset = offsetof(riscv_t, csr_cycle);
#if defined(__x86_64__)
    /* mov rbp, [rv + offset] / mov [rv + offset], rbp */
    emit_basic_rex(state, 1, cycle_reg, parameter_reg[0]);
    emit1(state, load ? 0x8b : 0x89);
    emit_modrm_and_displacement(state, cycle_reg, parameter_reg[0], offset);
#elif defined(__aarch64__)
    /* LDR/STR Xt, [Xn, #offset], unsigned scaled offset */
    emit_a64(state, (load ? UINT32_C(0xf9400000) : UINT32_C(0xf9000000)) |
                        ((offset / 8) << 10) |
                        ((uint32_t) parameter_reg[0] << 5) |
                        (uint32_t) cycle_reg);
#endif
}

static inline void save_reg(struct jit_state *, int);
static inline void unmap_vm_reg(int);

#if defined(__x86_64__)
/* Open (enter) or close a call frame: save rv, which the callee may clobber,
 * and pad RSP by JIT_CALL_PAD, which with the push keeps it 16-byte aligned.
 */
static inline void emit_call_frame(struct jit_state *state, bool enter)
{
    if (enter) {
        emit_push(state, parameter_reg[0]);
        emit_alu64_imm32(state, 0x81, 5, RSP, JIT_CALL_PAD);
    } else {
        emit_alu64_imm32(state, 0x81, 0, RSP, JIT_CALL_PAD);
        emit_pop(state, parameter_reg[0]);
    }
}
#endif

/* Call a helper that takes rv as its first argument. rv is preserved across the
 * call, since the epilogue reaches rv->csr_cycle through it.
 */
static inline void emit_call(struct jit_state *state, intptr_t target)
{
    emit_cycle_move(state, false);
#if defined(__x86_64__)
    emit_call_frame(state, true);
    emit_load_imm_sext(state, RAX, target);
    /* callq *%rax */
    emit1(state, 0xff);
    /* ModR/M byte: b11010000b = xd0, rax is register 0 */
    emit1(state, 0xd0);
    emit_call_frame(state, false);
    emit_cycle_move(state, true);
#elif defined(__aarch64__)
    /* Save the link register and rv in one 16-byte slot. */
    emit_addsub_imm(state, true, AS_SUB, SP, SP, 16);
    emit_loadstore_imm(state, LS_STRX, R30, SP, 0);
    emit_loadstore_imm(state, LS_STRX, parameter_reg[0], SP, 8);

    emit_movewide_imm(state, true, temp_imm_reg, target);
    emit_uncond_branch_reg(state, BR_BLR, temp_imm_reg);

    /* Hold the result while rv is restored, since save_reg stores through it.
     */
    emit_logical_register(state, true, LOG_ORR, temp_imm_reg, RZ, R0);
    emit_loadstore_imm(state, LS_LDRX, parameter_reg[0], SP, 8);
    emit_cycle_move(state, true);
    save_reg(state, 0); /* R5 */
    unmap_vm_reg(0);    /* R5 */
    emit_logical_register(state, true, LOG_ORR, R5, RZ, temp_imm_reg);

    emit_loadstore_imm(state, LS_LDRX, R30, SP, 0);
    emit_addsub_imm(state, true, AS_ADD, SP, SP, 16);
#endif
}

static inline void emit_exit(struct jit_state *state)
{
#if defined(__x86_64__)
    emit1(state, JCC_JMP);
    emit_jump_target_address(state, TARGET_PC_EXIT, 0);
#elif defined(__aarch64__)
    emit_jmp(state, TARGET_PC_EXIT, 0);
#endif
}

/* Materialize a comparison without a data-dependent branch on x86.
 * Inspired by libriscv 3f06e04e's direct boolean-result lowering.
 */
static void emit_set_less_than(struct jit_state *state, int cond, int dst)
{
    assert(cond == JCC_JL || cond == JCC_JB);
#if defined(__x86_64__)
    /* REX is mandatory for SIL/DIL and the low bytes of extended registers. */
    emit_rex(state, 0, 0, 0, !!(dst & 8));
    emit1(state, 0x0f);
    emit1(state, 0x90 | (cond & 0xf)); /* SETL/SETB dst8 */
    emit_modrm_reg2reg(state, 0, dst);
    emit_rex(state, 0, !!(dst & 8), 0, !!(dst & 8));
    emit1(state, 0x0f);
    emit1(state, 0xb6); /* MOVZX dst32, dst8 also clears the upper 32 bits */
    emit_modrm_reg2reg(state, dst, dst);
    set_dirty(dst, true);
#elif defined(__aarch64__)
    /* CSET Wd, cond is CSINC Wd, WZR, WZR with the condition inverted. */
    const uint32_t a64_cond = cond == JCC_JL ? COND_LT : COND_LO;
    emit_a64(state, 0x1a800400 | (RZ << 16) | ((a64_cond ^ 1) << 12) |
                        (RZ << 5) | dst);
    set_dirty(dst, true);
#endif
}

#if RV32_HAS(EXT_M)
#if defined(__aarch64__)
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

    /* Use SDIV for signed operations, UDIV for unsigned */
    emit_dataproc_2source(state, is64, sign ? DP2_SDIV : DP2_UDIV, div_dest, rn,
                          rm);
    if (mod)
        emit_dataproc_3source(state, is64, DP3_MSUB, rd, rm, div_dest, rn);

    if (sign) {
        /* handle overflow */
        uint32_t jump_loc_0 = state->offset;
        emit_jcc_offset(state, JCC_JNE);
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
    bool mod = (opcode & JIT_ALU_OP_MASK) == (JIT_OP_MOD_IMM & JIT_ALU_OP_MASK);

    if (mul) {
        /* The operands are extended RV32 words, so their full product fits
         * in 64 bits. Two-operand IMUL gives the same low 64 bits for every
         * signedness combination without touching RAX or RDX.
         */
        emit_imul(state, true, dst, src);
        return;
    }

    /* IDIV/DIV require EDX:EAX. Preserve unrelated cached guest registers;
     * src is the reserved temporary and cannot alias either fixed register.
     */
    int d1 = register_map[0].dirty, d2 = register_map[2].dirty;
    int r1 = register_map[0].vm_reg_idx, r2 = register_map[2].vm_reg_idx;
    if (dst != RAX) {
        unmap_vm_reg(0);
        emit_push(state, RAX);
    }
    if (dst != RDX) {
        unmap_vm_reg(2);
        emit_push(state, RDX);
    }
    emit_mov32(state, dst, RAX);

    /* Inspired by libriscv ec321244: keep zero and -1 off the native signed
     * divide path. One unsigned comparison excludes both, including the
     * INT32_MIN / -1 overflow. All results below are 32-bit writes, preserving
     * the allocator's zero-extended RV32 register invariant.
     */
    if (sign) {
        /* LEA EDX, [src + 1]; CMP EDX, 1: divisors 0 and -1 compare below or
         * equal.
         */
        emit_basic_rex(state, 0, RDX, src);
        emit1(state, 0x8d);
        emit_modrm_and_displacement(state, RDX, src, 1);
        emit_cmp_imm32(state, RDX, 1);
        emit_jcc_offset(state, JCC_JBE);
    } else {
        emit_cmp_imm32(state, src, 0);
        emit_jcc_offset(state, JCC_JE);
    }
    uint32_t exceptional = state->offset - 4;

    if (sign)
        emit1(state, 0x99); /* CDQ: sign-extend EAX into EDX:EAX */
    else
        emit_alu32(state, 0x31, RDX, RDX);
    emit_alu32_raw(state, UNARY_GRP3_OPCODE, sign ? UNARY_IDIV : UNARY_DIV,
                   src);
    emit_jcc_offset(state, JCC_JMP);
    uint32_t done = state->offset - 4;

    emit_jump_target_offset(state, exceptional, state->offset);
    uint32_t minus_one_done = 0;
    if (sign) {
        emit_cmp_imm32(state, src, 0);
        emit_jcc_offset(state, JCC_JE);
        uint32_t zero = state->offset - 4;
        if (mod)
            emit_load_imm(state, RDX, 0);
        else
            emit_alu32(state, UNARY_GRP3_OPCODE, UNARY_NEG,
                       RAX); /* NEG EAX wraps INT32_MIN */
        emit_jcc_offset(state, JCC_JMP);
        minus_one_done = state->offset - 4;
        emit_jump_target_offset(state, zero, state->offset);
    }
    if (mod)
        emit_mov32(state, RAX, RDX); /* x % 0 = x */
    else
        emit_load_imm(state, RAX, UINT32_MAX); /* x / 0 = -1 */
    emit_jump_target_offset(state, done, state->offset);
    if (sign)
        emit_jump_target_offset(state, minus_one_done, state->offset);

    if (dst != RDX) {
        if (mod)
            emit_mov32(state, RDX, dst);
        emit_pop(state, RDX);
        register_map[2].vm_reg_idx = r2;
        register_map[2].dirty = d2;
    }
    if (dst != RAX) {
        if (!mod)
            emit_mov32(state, RAX, dst);
        emit_pop(state, RAX);
        register_map[0].vm_reg_idx = r1;
        register_map[0].dirty = d1;
    }
#elif defined(__aarch64__)
    switch (opcode) {
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

void jit_misaligned_trap(riscv_t *rv, uint32_t addr, uint32_t flags)
{
    rv->compressed = flags & JIT_MISALIGN_COMPRESSED;
    if (flags & JIT_MISALIGN_STORE)
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, STORE_MISALIGNED, addr)
    else
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, LOAD_MISALIGNED, addr)
}

#if RV32_HAS(SYSTEM_MMIO)
static uint32_t jit_mmio_read_wrapper(riscv_t *rv, uint32_t addr)
{
    MMIO_READ();
    __UNREACHABLE;
}

/* jit_mmu_handler()'s type argument carries the refund in its upper half. */
#define MMU_TYPE_MASK UINT32_C(0xffff)

/* Slow-path entry from the JIT.
 *
 * vaddr / type / pc / vreg_idx arrive as call arguments rather than via the
 * rv->jit_mmu staging area.  Skipping the three pre-call memory stores per
 * memop saves slow-path host bytes and one round-trip through L1.  Results
 * (is_mmio, paddr) still land in rv->jit_mmu for the JIT to read back; once
 * we have a register-return story those reads can go too.
 */
static void jit_mmu_handler(riscv_t *rv,
                            uint32_t vreg_idx,
                            uint32_t vaddr,
                            uint32_t type,
                            uint32_t pc)
{
    assert(vreg_idx < 32);
    /* The block charged its later instructions on entry; a fault means they
     * never retire. Refund them before the guest handler can read a counter.
     */
    const uint32_t refund = type >> 16;
    type &= MMU_TYPE_MASK;

    uint32_t addr;
    uint32_t access_size;

    /* Determine access size based on instruction type */
    switch (type) {
    case rv_insn_lb:
    case rv_insn_lbu:
    case rv_insn_sb:
        access_size = 1;
        break;
    case rv_insn_lh:
    case rv_insn_lhu:
    case rv_insn_sh:
        access_size = 2;
        break;
    case rv_insn_lw:
    case rv_insn_sw:
        access_size = 4;
        break;
    default:
        /* Catch unhandled instruction types early */
        assert(!"Unhandled JIT MMU instruction type");
        __UNREACHABLE;
    }

    /* Set rv->PC to the faulting instruction's PC BEFORE calling mem_translate.
     * This is necessary because if a page fault occurs, on_trap is called from
     * inside mem_translate (via SET_CAUSE_AND_TVAL_THEN_TRAP) and the trap
     * handler uses rv->PC to set sepc/mepc (the return address). Without this,
     * the kernel would resume at the wrong instruction after sret.
     */
    rv->PC = pc;

    rv->csr_cycle -= refund;
    if (type == rv_insn_lb || type == rv_insn_lh || type == rv_insn_lbu ||
        type == rv_insn_lhu || type == rv_insn_lw)
        addr = rv->io.mem_translate(rv, vaddr, R);
    else
        addr = rv->io.mem_translate(rv, vaddr, W);

    /* A page fault in mem_translate may leave a trap pending (trap_cnt), or
     * its handler may have run already and resumed somewhere else
     * (need_handle_signal). Either way the access did not complete, and the
     * block stops here for the dispatcher to settle. Mark it as MMIO so that no
     * direct memory access follows, without performing an MMIO operation.
     */
    rv->jit_mmu.abort = rv->trap_cnt || need_handle_signal;
    if (rv->jit_mmu.abort) {
        rv->jit_mmu.is_mmio = 1;
        return;
    }
    rv->csr_cycle += refund;

    /* Only treat as RAM if entire access range [addr, addr+size) is within
     * valid guest memory bounds. This prevents buffer overflow on multi-byte
     * accesses near the memory boundary.
     */
    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, access_size)) {
        rv->jit_mmu.is_mmio = 0;
        rv->jit_mmu.paddr = addr;
        return;
    }

    uint32_t val;
    rv->jit_mmu.is_mmio = 1;

    switch (type) {
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
    /* A load into x0 still reads the device, but x0 stays zero. */
    case rv_insn_lb:
        val = (int8_t) jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lh:
        val = (int16_t) jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lw:
        val = jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lbu:
        val = (uint8_t) jit_mmio_read_wrapper(rv, addr);
        break;
    case rv_insn_lhu:
        val = (uint16_t) jit_mmio_read_wrapper(rv, addr);
        break;
    default:
        assert(NULL);
        __UNREACHABLE;
    }
    if (type != rv_insn_sb && type != rv_insn_sh && type != rv_insn_sw &&
        vreg_idx)
        rv->X[vreg_idx] = val;
}

#if RV32_HAS(SYSTEM_MMIO)
#if defined(__aarch64__)
/* CBZ Wt, #0 — 32-bit form, displacement patched later.  Returns the
 * instruction offset so the caller can record it for resolve_jumps().
 */
static uint32_t emit_a64_cbz(struct jit_state *state, int rt)
{
    uint32_t loc = state->offset;
    emit_a64(state, UINT32_C(0x34000000) | (uint32_t) rt);
    return loc;
}

/* CBNZ Wt, #0 — 32-bit form, displacement patched later. */
static uint32_t emit_a64_cbnz(struct jit_state *state, int rt)
{
    uint32_t loc = state->offset;
    emit_a64(state, UINT32_C(0x35000000) | (uint32_t) rt);
    return loc;
}

/* B.cond #0 — displacement patched later.  Returns the instruction offset. */
static uint32_t emit_a64_bcond(struct jit_state *state, int cond)
{
    uint32_t loc = state->offset;
    emit_a64(state, UINT32_C(0x54000000) | (uint32_t) cond);
    return loc;
}

#endif

#if defined(__x86_64__) && !defined(_WIN32)
/* cmp byte ptr [r12+disp8], imm8 — 6 bytes: 41 80 7c 24 disp imm. */
static void emit_x64_cmp_byte_r12_disp_imm8(struct jit_state *state,
                                            uint8_t disp,
                                            uint8_t imm)
{
    emit1(state, 0x41);
    emit1(state, 0x80);
    emit1(state, 0x7c);
    emit1(state, 0x24);
    emit1(state, disp);
    emit1(state, imm);
}

/* test byte ptr [r12+disp8], imm8 — 5 bytes: 41 f6 44 24 disp imm. */
static void emit_x64_test_byte_r12_disp_imm8(struct jit_state *state,
                                             uint8_t disp,
                                             uint8_t imm)
{
    emit1(state, 0x41);
    emit1(state, 0xf6);
    emit1(state, 0x44);
    emit1(state, 0x24);
    emit1(state, disp);
    emit1(state, imm);
}

/* jcc rel32: emits 0F <opc> <rel32=0>; returns the offset of the rel32 field
 * so the caller can record it for resolve_jumps() to patch.
 */
static uint32_t emit_x64_jcc32(struct jit_state *state, uint8_t opc)
{
    emit1(state, 0x0f);
    emit1(state, opc);
    uint32_t loc = state->offset;
    emit4(state, 0);
    return loc;
}

#endif

/* Inline dTLB fast path for a load or store.
 *
 * Caller contract (set up by the GEN_LOAD / GEN_STORE macros in rv32_jit.c):
 *   - parameter_reg[0] holds rv
 *   - temp_reg holds the guest virtual address (vaddr = rs1 + imm)
 *   - The host register allocator has been reset (caller did store_back +
 *     reset_reg) and load_dest_reg is the host register the caller will
 *     subsequently bind to ir->rd via map_vm_reg.  For stores, load_dest_reg
 *     is unused; the data source is read straight from rv->X[ir->rs2] via a
 *     scratch.
 *
 * The fast path bails out (falls through to where the caller emits the slow
 * path) whenever any of the following are not met, so each one keeps the
 * slow path's behavior intact:
 *   - vaddr aligned to access_size
 *   - With nonzero SATP: dTLB[idx] is valid and its VPN matches
 *   - With nonzero SATP: entry.perm has PTE_R (load) or PTE_W (store)
 *   - For paged stores: entry.dirty == 1 (so PTE_D update stays in C)
 *   - paddr + access_size <= mem_size (MMIO and bound-violators take slow path)
 *
 * Both ordinary pages and superpages are handled inline. Every exact VPN
 * caches a translated 4 KiB slice, so both hosts use a twelve-bit offset.
 *
 * Aligned + same page implies no page crossing, so we do not check that.
 *
 * Returns the buf offset of the unconditional branch that the caller must
 * patch (via emit_jump_target_offset) to the "fast path end" label, i.e. the
 * common merge point after the slow path. Returns 0 when no fast path was
 * emitted (caller proceeds straight to slow path).
 */
static uint32_t emit_jit_mmu_fastpath(struct jit_state *state,
                                      const rv_insn_t *ir,
                                      uint32_t insn_type,
                                      int load_dest_reg UNUSED,
                                      const memory_t *mem)
{
    uint8_t access_size = 0;
    bool is_store = false;
    bool is_signed UNUSED = false;
    switch (insn_type) {
    case rv_insn_lb:
        access_size = 1;
        is_signed = true;
        break;
    case rv_insn_lbu:
        access_size = 1;
        break;
    case rv_insn_lh:
        access_size = 2;
        is_signed = true;
        break;
    case rv_insn_lhu:
        access_size = 2;
        break;
    case rv_insn_lw:
        access_size = 4;
        break;
    case rv_insn_sb:
        access_size = 1;
        is_store = true;
        break;
    case rv_insn_sh:
        access_size = 2;
        is_store = true;
        break;
    case rv_insn_sw:
        access_size = 4;
        is_store = true;
        break;
    default:
        return 0;
    }

    /* Small RAM images cannot satisfy multi-byte accesses in the inline fast
     * path. Keep them on the checked slow path and avoid unsigned underflow in
     * the max_paddr calculation below.
     */
    if (mem->mem_size < access_size)
        return 0;
#if defined(__aarch64__)
    /* Guest RAM is capped at 4 GiB.  The exact 4 GiB case is supported:
     * mem_size itself no longer fits in uint32_t, but mem_size - access_size
     * still does for every load/store width we JIT here.
     */
    assert(mem->mem_size <= 0x100000000ULL);

    /* Track the offsets of every conditional branch that funnels to the slow
     * path so we can patch them once we know the slow-path label.
     *
     * Maximum count:
     *   1 align + 1 vpn + 1 valid + 1 perm + 1 dirty (store) + 1 range
     */
    uint32_t slow_branches[8];
    int n_slow = 0;

    /* Alignment check (skip for byte access)
     * AND w10, w8, #(size - 1); CBNZ w10, slow
     */
    if (access_size > 1) {
        emit_load_imm(state, R10, access_size - 1);
        emit_logical_register(state, false, LOG_AND, R10, R8, R10);
        slow_branches[n_slow++] = emit_a64_cbnz(state, R10);
    }

    /* Bare addresses share the range check without probing the dTLB. */
    emit_load_imm_sext(state, R24,
                       (int64_t) (uintptr_t) offsetof(riscv_t, csr_satp));
    emit_addsub_register(state, true, AS_ADD, R24, R0, R24);
    emit_loadstore_imm(state, LS_LDRW, R24, R24, 0);
    uint32_t paged_branch = emit_a64_cbnz(state, R24);
    emit_logical_register(state, false, LOG_ORR, R24, RZ, R8);
    uint32_t bare_branch = state->offset;
    emit_jcc_offset(state, JCC_JMP);
    emit_jump_target_offset(state, paged_branch, state->offset);

    /* vpn = vaddr >> 12
     * LSR Wd, Wn, #12 = UBFM Wd, Wn, #12, #31
     * sf=0, opc=10, N=0: 0x53000000 | (immr<<16) | (imms<<10) | (Rn<<5) | Rd
     */
    emit_a64(state, UINT32_C(0x53000000) | (UINT32_C(12) << 16) |
                        (UINT32_C(31) << 10) | ((uint32_t) R8 << 5) |
                        (uint32_t) R10);

    /* idx = vpn & (TLB_SIZE - 1) into w24 */
    emit_load_imm(state, R24, TLB_SIZE - 1);
    emit_logical_register(state, false, LOG_AND, R24, R10, R24);

    /* x24 = idx << 4 (each tlb_entry_t is 16 bytes)
     * LSL Xd, Xn, #4 = UBFM Xd, Xn, #(64-4)=60, #(63-4)=59
     * sf=1, N=1: 0xd3400000 | (60<<16) | (59<<10) | (Rn<<5) | Rd
     */
    emit_a64(state, UINT32_C(0xd3400000) | (UINT32_C(60) << 16) |
                        (UINT32_C(59) << 10) | ((uint32_t) R24 << 5) |
                        (uint32_t) R24);

    /* x25 = rv + offsetof(dtlb) + idx*16 */
    emit_load_imm_sext(state, R25,
                       (int64_t) (uintptr_t) offsetof(riscv_t, dtlb));
    emit_addsub_register(state, true, AS_ADD, R25, R0, R25);
    emit_addsub_register(state, true, AS_ADD, R25, R25, R24);
    /* w10 still holds vpn (LSR result above; not clobbered by the AND into
     * R24 nor by the LSL on R24).
     */

    /* VPN match: LDR w24,[x25,#0]; CMP w24,w10; B.NE slow */
    emit_loadstore_imm(state, LS_LDRW, R24, R25, 0);
    emit_addsub_register(state, false, AS_SUBS, RZ, R24, R10);
    slow_branches[n_slow++] = emit_a64_bcond(state, COND_NE);

    /* valid != 0: LDRB w24,[x25,#13]; CBZ w24, slow */
    emit_loadstore_imm(state, LS_LDRB, R24, R25, 13);
    slow_branches[n_slow++] = emit_a64_cbz(state, R24);

    /* perm has needed bit: LDRB w24,[x25,#12]; AND with PTE_R/W */
    emit_loadstore_imm(state, LS_LDRB, R24, R25, 12);
    emit_load_imm(state, R10, is_store ? PTE_W : PTE_R);
    emit_logical_register(state, false, LOG_AND, R24, R24, R10);
    slow_branches[n_slow++] = emit_a64_cbz(state, R24);

    /* For stores, dirty must be 1 (so PTE_D update stays in C) */
    if (is_store) {
        emit_loadstore_imm(state, LS_LDRB, R24, R25, 14);
        slow_branches[n_slow++] = emit_a64_cbz(state, R24);
    }

    /* The exact VPN tag identifies a translated 4 KiB slice, including
     * superpage entries, so the offset always has twelve bits.
     * paddr = ppn | (vaddr & 0xFFF)
     */
    emit_loadstore_imm(state, LS_LDRW, R24, R25, 4); /* w24 = ppn */
    /* UBFX w10, w8, #0, #12 = UBFM w10, w8, #0, #11 */
    emit_a64(state, 0x53000000U | (11 << 10) | (R8 << 5) | R10);
    emit_logical_register(state, false, LOG_ORR, R24, R24, R10); /* paddr */

    emit_jump_target_offset(state, bare_branch, state->offset);

    /* Range check: paddr <= mem_size - access_size
     * Branch to slow path if paddr > max_paddr (unsigned).
     */
    uint32_t max_paddr = (uint32_t) (mem->mem_size - access_size);
    emit_load_imm(state, R25, max_paddr);
    /* CMP w25, w24 — sets C if no borrow (w25 >= w24).  COND_LO branches when
     * C clear, i.e. w25 < w24 (paddr exceeds the max).
     */
    emit_addsub_register(state, false, AS_SUBS, RZ, R25, R24);
    slow_branches[n_slow++] = emit_a64_bcond(state, COND_LO);

    /* host address: x25 = mem_base + paddr
     * The OR above was a 32-bit op, which zero-extends, so the high 32 bits of
     * x24 are already zero and the 64-bit ADD is correct.
     */
    emit_load_imm_sext(state, R25, (int64_t) (uintptr_t) mem->mem_base);
    emit_addsub_register(state, true, AS_ADD, R25, R25, R24);

    /* Perform the access */
    if (is_store) {
        emit_load(state, S32, R0, R10,
                  offsetof(riscv_t, X) + 4 * (uint32_t) ir->rs2);
        a64opcode_t store_op;
        switch (access_size) {
        case 1:
            store_op = LS_STRB;
            break;
        case 2:
            store_op = LS_STRH;
            break;
        case 4:
            store_op = LS_STRW;
            break;
        default:
            __UNREACHABLE;
        }
        emit_loadstore_imm(state, store_op, R10, R25, 0);
    } else {
        if (host_reg_maps_x0(load_dest_reg)) {
            emit_load_imm(state, load_dest_reg, 0);
            set_dirty(load_dest_reg, true);
        } else {
            a64opcode_t load_op;
            switch (access_size) {
            case 1:
                load_op = is_signed ? LS_LDRSBW : LS_LDRB;
                break;
            case 2:
                load_op = is_signed ? LS_LDRSHW : LS_LDRH;
                break;
            case 4:
                load_op = LS_LDRW;
                break;
            default:
                __UNREACHABLE;
            }
            /* Load directly into the host register the caller has bound to rd.
             * Mark it dirty so the next store_back() spills the new value to
             * rv->X[rd]; otherwise the architectural write is lost on block
             * exit.
             */
            emit_loadstore_imm(state, load_op, load_dest_reg, R25, 0);
            set_dirty(load_dest_reg, true);
        }
    }

    /* Final unconditional B; caller patches the offset to fastpath_end */
    uint32_t fastpath_end_loc = state->offset;
    emit_a64(state, UINT32_C(0x14000000)); /* B #0 (patched later) */

    /* Slow path begins right after our emission; patch every gating branch. */
    uint32_t slow_label = state->offset;
    for (int i = 0; i < n_slow; i++)
        emit_jump_target_offset(state, slow_branches[i], slow_label);

    return fastpath_end_loc;
#elif defined(__x86_64__) && !defined(_WIN32)
    /* Guest RAM is capped at 4 GiB.  The exact 4 GiB case is supported:
     * mem_size itself no longer fits in uint32_t, but mem_size - access_size
     * still does for every load/store width we JIT here.
     */
    assert(mem->mem_size <= 0x100000000ULL);
    /* Slow path's reset_reg + map_vm_reg(rd) deterministically returns
     * register_map[0].reg_idx which is RAX on this ABI.  The caller passes
     * that same value through load_dest_reg so the fast path lands the
     * loaded value in the same host register the slow path will use.
     */
    if (!is_store)
        assert(load_dest_reg == RAX);

    /* Up to 7 conditional branches funnel to the slow-path label.  Each
     * entry stores the offset of the rel32 displacement field, matching
     * what resolve_jumps() patches.
     */
    uint32_t slow_branches[8];
    int n_slow = 0;

    /* Alignment check */
    if (access_size > 1) {
        /* test cl, (size - 1) */
        emit1(state, 0xf6);
        emit_modrm(state, 0xc0, 0, RCX);
        emit1(state, (uint8_t) (access_size - 1));
        /* jnz rel32 (0f 85 rel32) — rel32 starts 2 bytes after opcode */
        emit1(state, 0x0f);
        emit1(state, 0x85);
        slow_branches[n_slow++] = state->offset;
        emit4(state, 0);
    }

    /* Bare addresses share the range check without probing the dTLB. */
    /* mov eax, [rdi + csr_satp] */
    emit1(state, 0x8b);
    emit_modrm_and_displacement(state, RAX, RDI, offsetof(riscv_t, csr_satp));
    emit_cmp_imm32(state, RAX, 0);
    /* jne */
    uint32_t paged_branch = emit_x64_jcc32(state, 0x85);
    /* mov eax, ecx */
    emit1(state, 0x89);
    emit_modrm_reg2reg(state, RCX, RAX);
    emit1(state, 0xe9);
    uint32_t bare_branch = state->offset;
    emit4(state, 0);
    emit_jump_target_offset(state, paged_branch, state->offset);

    /* vpn = vaddr >> 12 into EAX (kept around for the CMP) */
    /* mov eax, ecx */
    emit1(state, 0x89);
    emit_modrm_reg2reg(state, RCX, RAX);
    /* shr eax, 12 */
    emit1(state, 0xc1);
    emit_modrm(state, 0xc0, 5, RAX);
    emit1(state, 12);

    /* idx (in ESI) = vpn & (TLB_SIZE - 1); then ESI <<= 4 */
    /* mov esi, eax */
    emit1(state, 0x89);
    emit_modrm_reg2reg(state, RAX, RSI);
    /* and esi, TLB_SIZE - 1   (imm8 fits since TLB_SIZE - 1 = 63) */
    emit1(state, 0x83);
    emit_modrm(state, 0xc0, 4, RSI);
    emit1(state, (uint8_t) (TLB_SIZE - 1));
    /* shl rsi, 4 — REX.W + C1 /4 imm8 */
    emit1(state, 0x48);
    emit1(state, 0xc1);
    emit_modrm(state, 0xc0, 4, RSI);
    emit1(state, 4);

    /* r12 = rdi + offsetof(dtlb) + rsi (idx*16 already in rsi)
     * LEA r12, [rdi + rsi*1 + dtlb_offset]
     */
    emit_rex(state, 1, 1, 0, 0);
    emit1(state, 0x8d);
    emit_sib_operand(state, R12, RDI, RSI, 0, offsetof(riscv_t, dtlb));

    /* VPN match: cmp eax, [r12]; jne slow
     * REX.B for r12 base, opcode 3b, mod=00 r/m=100 SIB, SIB.base=R12.
     */
    emit1(state, 0x41);
    emit1(state, 0x3b);
    emit1(state, 0x04);
    emit1(state, 0x24);
    slow_branches[n_slow++] = emit_x64_jcc32(state, 0x85); /* jne */

    /* valid (byte at +13) != 0: je slow */
    emit_x64_cmp_byte_r12_disp_imm8(state, 13, 0);
    slow_branches[n_slow++] = emit_x64_jcc32(state, 0x84); /* je */

    /* perm (byte at +12) has needed bit: jz slow */
    emit_x64_test_byte_r12_disp_imm8(state, 12, is_store ? PTE_W : PTE_R);
    slow_branches[n_slow++] = emit_x64_jcc32(state, 0x84); /* jz */

    /* (stores) dirty (byte at +14) != 0: je slow */
    if (is_store) {
        emit_x64_cmp_byte_r12_disp_imm8(state, 14, 0);
        slow_branches[n_slow++] = emit_x64_jcc32(state, 0x84); /* je */
    }

    /* The exact VPN tag identifies a translated 4 KiB slice, including
     * superpage entries. The offset therefore always has twelve bits.
     */
    emit_mov32(state, RCX, RSI);
    emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_AND, RSI, 0xFFF);
    /* mov eax, [r12+4]  (4-byte ppn) */
    emit1(state, 0x41);
    emit1(state, 0x8b);
    emit1(state, 0x44);
    emit1(state, 0x24);
    emit1(state, 4);
    /* or eax, esi — 09 /r */
    emit1(state, 0x09);
    emit_modrm_reg2reg(state, RSI, RAX);

    emit_jump_target_offset(state, bare_branch, state->offset);

    /* range: cmp eax, max_paddr; ja slow */
    uint32_t max_paddr = (uint32_t) (mem->mem_size - access_size);
    emit1(state, 0x3d); /* cmp eax, imm32 (short form) */
    emit4(state, max_paddr);
    slow_branches[n_slow++] = emit_x64_jcc32(state, 0x87); /* ja */

    /* host addr: r12 = mem_base + rax */
    /* mov r12, imm64 — REX.WB + B8+(r&7) + imm64 */
    emit1(state, 0x49);
    emit1(state, 0xbc);
    emit8(state, (uint64_t) (uintptr_t) mem->mem_base);
    /* add r12, rax — REX.WB + 01 /r — opcode 01 has direction r/m += reg */
    emit1(state, 0x49);
    emit1(state, 0x01);
    emit_modrm_reg2reg(state, RAX, R12);

    /* Access */
    if (is_store) {
        /* Load rs2 from rv->X[rs2] into ESI. */
        int32_t off = (int32_t) (offsetof(riscv_t, X) + 4 * (uint32_t) ir->rs2);
        emit1(state, 0x8b);
        emit_modrm_and_displacement(state, RSI, RDI, off);
        switch (access_size) {
        case 1:
            /* mov [r12], sil — needs REX (sil is high reg requiring REX). */
            emit1(state, 0x41); /* REX.B for r12 */
            emit1(state, 0x88); /* mov r/m8, r8 */
            emit1(state, 0x34);
            emit1(state, 0x24);
            break;
        case 2:
            emit1(state, 0x66); /* operand-size override (16-bit) */
            emit1(state, 0x41);
            emit1(state, 0x89);
            emit1(state, 0x34);
            emit1(state, 0x24);
            break;
        case 4:
            emit1(state, 0x41);
            emit1(state, 0x89);
            emit1(state, 0x34);
            emit1(state, 0x24);
            break;
        default:
            __UNREACHABLE;
        }
    } else {
        if (host_reg_maps_x0(load_dest_reg)) {
            emit_load_imm(state, load_dest_reg, 0);
            set_dirty(load_dest_reg, true);
        } else {
            /* Loads write to RAX (load_dest_reg). */
            switch (access_size) {
            case 1:
                emit1(state, 0x41); /* REX.B for r12 */
                emit1(state, 0x0f);
                emit1(state, is_signed ? 0xbe : 0xb6);
                emit1(state, 0x04);
                emit1(state, 0x24);
                break;
            case 2:
                emit1(state, 0x41);
                emit1(state, 0x0f);
                emit1(state, is_signed ? 0xbf : 0xb7);
                emit1(state, 0x04);
                emit1(state, 0x24);
                break;
            case 4:
                emit1(state, 0x41);
                emit1(state, 0x8b);
                emit1(state, 0x04);
                emit1(state, 0x24);
                break;
            default:
                __UNREACHABLE;
            }
            /* Mark RAX dirty so store_back() later spills the loaded value to
             * rv->X[rd]; otherwise the architectural write is lost on block
             * exit.
             */
            set_dirty(load_dest_reg, true);
        }
    }

    /* jmp rel32 to fastpath_end */
    emit1(state, 0xe9);
    uint32_t fastpath_end_loc = state->offset;
    emit4(state, 0);

    /* Slow path begins right after this; patch all gating branches. */
    uint32_t slow_label = state->offset;
    for (int i = 0; i < n_slow; i++)
        emit_jump_target_offset(state, slow_branches[i], slow_label);

    return fastpath_end_loc;
#else
    /* Windows x86-64 or other ABIs — fast path disabled. */
    (void) state;
    (void) ir;
    (void) access_size;
    (void) is_store;
    (void) is_signed;
    (void) load_dest_reg;
    (void) mem;
    return 0U;
#endif
}
#endif /* RV32_HAS(SYSTEM_MMIO) */

/* Emit the slow-path C call: jit_mmu_handler(rv, vreg_idx, vaddr, type, pc).
 *
 * Contract:
 *   - parameter_reg[0] holds rv on entry (the standard JIT calling convention)
 *   - vaddr_reg holds the guest virtual address.  Callers in the inline TLB
 *     fast path place vaddr in temp_reg as a side effect of the address
 *     compute; the fast-path probe preserves that register through the
 *     slow-path fall-through, so the typical caller passes vaddr_reg ==
 *     temp_reg.  do_fuse* callers load a constant vaddr into temp_reg
 *     immediately before this call for the same reason.
 *
 * This replaces three pre-call emit_stores into rv->jit_mmu.{vaddr,type,pc}.
 * The C handler now reads vaddr/type/pc directly from its arguments.  Results
 * (is_mmio, paddr) still land in rv->jit_mmu for the JIT to read back.
 */
static void emit_jit_mmu_handler(struct jit_state *state,
                                 uint8_t vreg_idx,
                                 int vaddr_reg,
                                 uint32_t insn_type,
                                 uint32_t insn_pc,
                                 uint32_t refund)
{
    assert(vreg_idx < 32);
    /* The handler refunds unretired instructions if the access faults. */
    assert(insn_type <= MMU_TYPE_MASK && refund <= UINT16_MAX);
    insn_type |= refund << 16;

    emit_cycle_move(state, false);
#if defined(__x86_64__) && defined(_WIN32)
    /* Preserve rv across the call, then reserve the required 32-byte shadow
     * space plus one 8-byte stack slot for the 5th argument.
     * prepare_translate() leaves %rsp 16-byte aligned inside the JIT body, so
     * push+sub 40 preserves Windows x64 call-site alignment.
     */
    emit_call_frame(state, true);

    /* mov r8, vaddr_reg — arg2 */
    if (vaddr_reg != R8) {
        emit_basic_rex(state, 1, vaddr_reg, R8);
        emit1(state, 0x89);
        emit_modrm_reg2reg(state, vaddr_reg, R8);
    }

    /* mov edx, vreg_idx — arg1 */
    emit1(state, 0xba);
    emit4(state, vreg_idx);

    /* mov r9d, insn_type — arg3 */
    emit1(state, 0x41);
    emit1(state, 0xb8 | (R9 & 7));
    emit4(state, insn_type);

    /* mov dword ptr [rsp + 32], insn_pc — arg4 */
    emit1(state, 0xc7);
    emit_modrm(state, 0x1 << 6, 0x0, RSP & 7);
    emit1(state, 0x24); /* SIB: no index, base rsp */
    emit1(state, 0x20);
    emit4(state, insn_pc);

    /* mov r11, &handler — caller-saved scratch */
    emit1(state, 0x49); /* REX.W + REX.B */
    emit1(state, 0xb8 | (R11 & 7));
    emit8(state, (uint64_t) (uintptr_t) &jit_mmu_handler);

    /* call r11 */
    emit1(state, 0x41); /* REX.B */
    emit1(state, 0xff);
    emit_modrm(state, 0x3 << 6, 0x2, R11 & 7);

    emit_call_frame(state, false);
#elif defined(__x86_64__)
    /* Preserve rv across the call. prepare_translate() leaves %rsp 16-byte
     * aligned inside the JIT body, so a matching 8-byte alignment slot keeps
     * the SysV call-site stack invariant after the push.
     */
    emit_call_frame(state, true);

    /* mov rdx, vaddr_reg — arg2. Must precede the type load that overwrites
     * RCX (== temp_reg == parameter_reg[3]).
     */
    if (vaddr_reg != RDX) {
        emit_basic_rex(state, 1, vaddr_reg, RDX);
        emit1(state, 0x89);
        emit_modrm_reg2reg(state, vaddr_reg, RDX);
    }

    /* mov ecx, insn_type — arg3 (overwrites RCX/temp_reg) */
    emit1(state, 0xb8 | (RCX & 7));
    emit4(state, insn_type);

    /* mov r8d, insn_pc — arg4 */
    emit1(state, 0x41);
    emit1(state, 0xb8 | (R8 & 7));
    emit4(state, insn_pc);

    /* mov esi, vreg_idx — arg1 */
    emit1(state, 0xbe);
    emit4(state, vreg_idx);

    /* mov r11, &handler — caller-saved scratch */
    emit1(state, 0x49); /* REX.W + REX.B */
    emit1(state, 0xb8 | (R11 & 7));
    emit8(state, (uint64_t) (uintptr_t) &jit_mmu_handler);

    /* call r11 */
    emit1(state, 0x41); /* REX.B */
    emit1(state, 0xff);
    emit_modrm(state, 0x3 << 6, 0x2, R11 & 7);

    emit_call_frame(state, false);
#elif defined(__aarch64__)
    uint32_t insn;

    /* push rv (X0) onto stack: str x0, [sp, #-16]! */
    insn = (0xf81f0fe << 4) | R0;
    emit_a64(state, insn);

    /* mov X2, vaddr_reg — arg2.  Sequence first because subsequent MOVZs may
     * use temp_reg, and on most call paths vaddr_reg == temp_reg.
     */
    if (vaddr_reg != R2) {
        /* MOV (register) is encoded as ORR Wd, WZR, Wm */
        emit_logical_register(state, false, LOG_ORR, R2, RZ, vaddr_reg);
    }

    /* movz X3, insn_type (+ movk if needed) — arg3 */
    emit_movewide_imm(state, true, R3, insn_type);

    /* movz X4, insn_pc (+ movk if needed) — arg4 */
    emit_movewide_imm(state, true, R4, insn_pc);

    /* movz X1, vreg_idx — arg1 (fits in 16 bits, single MOVZ) */
    emit_movewide_imm(state, false, R1, vreg_idx);

    /* load &jit_mmu_handler into temp_reg */
    emit_movewide_imm(state, true, temp_reg, (uintptr_t) &jit_mmu_handler);
    /* blr temp_reg */
    insn = (0xd63f << 16) | (temp_reg << 5);
    emit_a64(state, insn);

    /* pop rv: ldr x0, [sp], #16 */
    insn = (0xf84107e << 4) | R0;
    emit_a64(state, insn);
#endif
    emit_cycle_move(state, true);
}
#endif

static void prepare_translate(struct jit_state *state,
                              uintptr_t mem_base UNUSED)
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

    /* Allocate stack space, and on Windows the home space for 4 registers
     * that its ABI requires. Generated code leaves RSP balanced, so the
     * epilogue releases the same amount.
     */
    emit_alu64_imm32(state, 0x81, 5, RSP, JIT_FRAME_SIZE);
    emit_cycle_move(state, true);

    /* R12 is outside the allocator and survives calls and direct chains. */
#if JIT_INDEXED_GUEST_MEM
    emit_load_imm_sext(state, R12, mem_base);
#endif

    /* Jump to the entry point, which is stored in the second parameter. */
    emit1(state, 0xff);
    emit1(state, 0xe6);

    /* Epilogue */
    state->exit_loc = state->offset;

    emit_cycle_move(state, false);
    emit_alu64_imm32(state, 0x81, 0, RSP, JIT_FRAME_SIZE);

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

#if JIT_BASED_GUEST_MEM
    /* R19 is already saved above and never allocated to a guest register. */
    emit_load_imm_sext(state, R19, mem_base);
#endif
    emit_cycle_move(state, true);
    emit_uncond_branch_reg(state, BR_BR, R1);
    /* Epilogue */
    state->exit_loc = state->offset;
    emit_cycle_move(state, false);

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
/* Guest values kept in fixed host registers across a local loop backedge. */
static uint32_t loop_pinned_regs;

static bool reg_is_pinned(int idx)
{
    int reg = register_map[idx].vm_reg_idx;
    return reg >= 0 && (loop_pinned_regs & (UINT32_C(1) << reg));
}

static void reset_reg(void)
{
    loop_pinned_regs = 0;
    spill_live_regs = UINT32_MAX;
    memset(reg_align, 0, sizeof(reg_align));
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

    /* Unmapped temporaries and x0 have no register backing store. This also
     * permits using x0's host register as scratch without changing X[0].
     * Registers outside spill_live_regs are overwritten before any read.
     */
    const int vm_reg_idx = register_map[idx].vm_reg_idx;
    if (register_map[idx].dirty && vm_reg_idx > 0 &&
        (spill_live_regs & (1U << vm_reg_idx)))
        emit_store(state, S32, register_map[idx].reg_idx, parameter_reg[0],
                   offsetof(riscv_t, X) + 4 * vm_reg_idx);
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

/* Write back every dirty register, live or not: for exits whose target may
 * read the whole register file.
 */
static void store_back_all(struct jit_state *state)
{
    const uint32_t saved_live = spill_live_regs;
    spill_live_regs = UINT32_MAX;
    store_back(state);
    spill_live_regs = saved_live;
}

static inline void liveness_reset(void)
{
    memset(liveness, 0xff, sizeof(liveness));
}

static inline void candidate_queue_init(void)
{
    for (int i = 0; i < N_RV_REGS; i++) {
        candidate_queue[i] = i;
    }
}

static int liveness_cmp(const void *l, const void *r)
{
    int liveness_l = liveness[*(uint8_t *) l];
    int liveness_r = liveness[*(uint8_t *) r];

    /* Use explicit comparisons to avoid potential overflow from subtraction */
    if (liveness_l < liveness_r)
        return -1;
    if (liveness_l > liveness_r)
        return 1;

    /* Use register index as tie-breaker for stable sorting */
    uint8_t reg_l = *(uint8_t *) l;
    uint8_t reg_r = *(uint8_t *) r;
    if (reg_l < reg_r)
        return -1;
    if (reg_l > reg_r)
        return 1;
    return 0;
}

static inline void liveness_calc(block_t *block)
{
    uint32_t idx;
    rv_insn_t *ir;

    /* follow the order of operator in "src/rc32_template.c" */
    for (idx = 0, ir = block->ir_head; idx < block->n_insn;
         idx++, ir = block_next_ir(ir)) {
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
            liveness[ir->rs2] = idx;
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
            liveness[ir->rs2] = idx;
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
        case rv_insn_fuse6:
            /* LI a7 + ECALL: no registers to track (a7 is set internally) */
            break;
        case rv_insn_fuse7:
            /* Multiple ADDI: track rs1 for each operation */
            for (int i = 0; i < ir->imm2; i++) {
                liveness[ir->fuse[i].rs1] = idx;
            }
            break;
        case rv_insn_fuse8:
            /* LUI + ADDI: no source registers (rd = imm + imm2) */
            break;
        case rv_insn_fuse9:
            /* LUI + LW: no source registers (absolute address load) */
            break;
        case rv_insn_fuse10:
            /* LUI + SW: rs1 is source (value to store) */
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_fuse11:
            /* LW + ADDI: rs1 is source (base address and increment source) */
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_fuse12:
            /* ADDI + BNE: rs1 is source */
            liveness[ir->rs1] = idx;
            break;
        case rv_insn_fuse13:
            /* SW + ADDI: store value and base are both live. */
            liveness[ir->rs1] = idx;
            liveness[ir->rs2] = idx;
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

/* Pick a free host register: an unmapped one first, then one whose value is
 * dead in this block, clean before dirty. "Dead" uses block-local liveness,
 * so a successor in a chained region may still read that value. Taking the
 * first dead register would spill and reload it there even while other host
 * registers sit empty; libriscv's region register caching relies on values
 * staying resident. Returns -1 when every candidate is alive.
 */
static inline int reg_pick_free(int reserved1, int reserved2)
{
    int dead_clean = -1, dead_dirty = -1;
    for (int i = 0; i < n_host_regs; i++) {
        if (reg_is_pinned(i) || register_map[i].reg_idx == reserved1 ||
            register_map[i].reg_idx == reserved2 || register_map[i].alive)
            continue;
        if (register_map[i].vm_reg_idx == -1)
            return i;
        if (!register_map[i].dirty) {
            if (dead_clean < 0)
                dead_clean = i;
        } else if (dead_dirty < 0) {
            dead_dirty = i;
        }
    }
    return dead_clean >= 0 ? dead_clean : dead_dirty;
}

/* return the index in the register_map, avoiding two reserved registers */
static inline int reg_pick2(int reserved1, int reserved2)
{
    int free_idx = reg_pick_free(reserved1, reserved2);
    if (free_idx >= 0)
        return free_idx;

    /* If registers are exhausted, pick the one which has farthest liveness. */
    int idx = -1;
    for (int i = 0; i < N_RV_REGS; i++) {
        uint8_t candidate = candidate_queue[i];
        for (int j = 0; j < n_host_regs; j++) {
            if (reg_is_pinned(j) || register_map[j].reg_idx == reserved1 ||
                register_map[j].reg_idx == reserved2)
                continue;
            if (register_map[j].vm_reg_idx == candidate) {
                idx = j;
                goto end_pick_reg2;
            }
        }
    }
    __UNREACHABLE;

end_pick_reg2:
    assert(idx > -1 && idx < n_host_regs);
    return idx;
}

/* return the index in the register_map */
static inline int reg_pick(int reserved)
{
    return reg_pick2(reserved, reserved);
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
/* Map a guest register without implying it is about to be written. Callers
 * must not write the guest register through the result without marking it
 * dirty, or reg_align would keep a stale alignment proof.
 */
static inline int lookup_vm_reg(struct jit_state *state, int vm_reg_idx)
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
    return target_reg;
}

/* Map a guest register that the caller is about to write. */
static inline int map_vm_reg(struct jit_state *state, int vm_reg_idx)
{
    reg_align[vm_reg_idx] = 0;
    return lookup_vm_reg(state, vm_reg_idx);
}

static int ra_load(struct jit_state *state, int vm_reg_idx)
{
    int origin = -1;
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx)
            continue;
        origin = register_map[i].reg_idx;
    }

    int target_reg = lookup_vm_reg(state, vm_reg_idx);

    if (origin != target_reg)
        emit_load(state, S32, parameter_reg[0], target_reg,
                  offsetof(riscv_t, X) + 4 * vm_reg_idx);
    return target_reg;
}

/* Prevent the host register collision while the first vm register has already
 * been mapped and the second one is going to be mapped to the same host
 * register and invoke swapping.
 */
static inline int lookup_vm_reg_reserved(struct jit_state *state,
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
    return target_reg;
}

static inline int map_vm_reg_reserved(struct jit_state *state,
                                      int vm_reg_idx,
                                      int reserved_reg_idx)
{
    reg_align[vm_reg_idx] = 0;
    return lookup_vm_reg_reserved(state, vm_reg_idx, reserved_reg_idx);
}

/* Map a vm register while protecting two already-allocated host registers.
 * This prevents the register allocator from evicting either of the reserved
 * registers when allocating a third register (e.g., for rd after loading rs1
 * and rs2).
 */
static inline int map_vm_reg_reserved2(struct jit_state *state,
                                       int vm_reg_idx,
                                       int reserved_reg_idx1,
                                       int reserved_reg_idx2)
{
    reg_align[vm_reg_idx] = 0;
    for (int i = 0; i < n_host_regs; i++) {
        if (register_map[i].vm_reg_idx != vm_reg_idx)
            continue;
        return register_map[i].reg_idx;
    }

    int idx = reg_pick2(reserved_reg_idx1, reserved_reg_idx2);
    int target_reg = register_map[idx].reg_idx;

    save_reg(state, idx);
    unmap_vm_reg(idx);
    set_vm_reg(idx, vm_reg_idx);
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
        vm_reg[0] = vm_reg[1] = lookup_vm_reg(state, vm_reg_idx1);
    } else {
        vm_reg[0] = lookup_vm_reg(state, vm_reg_idx1);
        vm_reg[1] = lookup_vm_reg_reserved(state, vm_reg_idx2, vm_reg[0]);
        assert(vm_reg[0] != vm_reg[1]);
    }

    if (origin1 != vm_reg[0])
        emit_load(state, S32, parameter_reg[0], vm_reg[0],
                  offsetof(riscv_t, X) + 4 * vm_reg_idx1);
    if (origin2 != vm_reg[1])
        emit_load(state, S32, parameter_reg[0], vm_reg[1],
                  offsetof(riscv_t, X) + 4 * vm_reg_idx2);
}

/* Keep both operands resident until the store. Unlike an address built in
 * temp_reg, an indexed address still needs rs1 after rs2 is allocated.
 */
static void ra_store_guest(struct jit_state *state,
                           memory_t *m UNUSED,
                           enum operand_size size,
                           int rs1,
                           int rs2,
                           int32_t offset)
{
#if JIT_INDEXED_GUEST_MEM
    ra_load2(state, rs1, rs2);
    int base = vm_reg[0], src = vm_reg[1];
    assert((base & 7) != RSP);
    assert(size == S8 || size == S16 || size == S32);
    if (size == S16)
        emit1(state, 0x66);
    /* Always emit REX: byte stores must use SIL/DIL, never AH/CH/DH/BH. */
    emit_rex(state, 0, rs2 ? !!(src & 8) : 0, !!(base & 8), 1);
    if (rs2) {
        emit_store_opcode(state, size);
    } else {
        emit1(state, size == S8 ? 0xc6 : 0xc7);
    }
    emit_guest_indexed_operand(state, rs2 ? src : 0, base, offset);
    if (!rs2) {
        for (int i = 0; i < (size == S8 ? 1 : size == S16 ? 2 : 4); i++)
            emit1(state, 0);
    }
#else
    vm_reg[0] = ra_load(state, rs1);
    int32_t displacement = emit_guest_address(state, m, vm_reg[0], offset);
    vm_reg[1] = ra_load(state, rs2);
    emit_store(state, size, vm_reg[1], temp_reg, displacement);
#endif
}

#if RV32_HAS(EXT_M)
/* Shared preamble for the M-extension forms: load both operands, allocate the
 * destination, then copy rs2 to the scratch register and rs1 to the
 * destination, widening either copy whose operation reads it as signed.
 *
 * Widen the copies rather than the caller's operand registers. A guest
 * register is 32 bits, and a later use of the same host register as a
 * memory-address base adds all 64 (the SIB index on x86-64, the 64-bit ADD on
 * Arm64), which would move that access 4 GiB down and outside the guest
 * mapping. User mode spans the full 4 GiB, so an address with bit 31 set -
 * anything on the stack, for one - is ordinary rather than exotic.
 *
 * The destination is one of those copies and may alias rs1, so widening it
 * does touch a mapped register; muldivmod overwrites it with the result
 * before the value can outlive this instruction.
 */
static void ra_load2_muldiv(struct jit_state *state,
                            int rs1,
                            int rs2,
                            int rd,
                            bool sext1,
                            bool sext2)
{
    ra_load2(state, rs1, rs2);
    vm_reg[2] = map_vm_reg_reserved2(state, rd, vm_reg[0], vm_reg[1]);

    if (sext2)
        emit_mov_sext(state, vm_reg[1], temp_reg);
    else
        emit_mov(state, vm_reg[1], temp_reg);

    if (sext1)
        emit_mov_sext(state, vm_reg[0], vm_reg[2]);
    else
        emit_mov(state, vm_reg[0], vm_reg[2]);
}
#endif

/* Point the jump at offset_loc to target_loc, whatever it targeted before. */
static void patch_jump(struct jit_state *state,
                       uint32_t offset_loc,
                       uint32_t target_loc)
{
#if defined(__x86_64__)
    /* Assumes jump offset is at end of instruction */
    uint32_t rel = target_loc - (offset_loc + sizeof(uint32_t));
    memcpy(&state->buf[offset_loc], &rel, sizeof(uint32_t));
#elif defined(__aarch64__)
    patch_branch_imm(state, offset_loc, (int32_t) (target_loc - offset_loc));
#endif
}

/* User-mode T1 leaves an indirect jump through guard slots, each comparing the
 * target with one PC and branching to its translation. Translation fills them
 * with the targets the interpreter profiled; a slot left empty is filled by
 * jit_indirect_target() with the first compiled target that misses the others.
 * A block compiled before its indirect targets were profiled, or whose callee
 * returns to sites compiled later, thus still chains natively. Direct chains
 * skip the dispatcher already; system mode keeps returning there, where
 * interrupts and address-space changes are checked, and keeps profiled guards
 * only.
 */
#define JIT_INDIRECT_LOOKUP (JIT_INDIRECT_TARGETS && !RV32_HAS(SYSTEM))

#if JIT_INDIRECT_LOOKUP
/* A block ends in at most one indirect jump, whose site shares its index. */
#define MAX_INDIRECT_SITES MAX_BLOCKS
/* An empty slot compares with an odd PC, where no translated block starts. */
#define EMPTY_SLOT_PC UINT32_MAX

struct indirect_site {
    uint32_t imm_loc[IN_JUMP_TARGETS];  /* the PC each slot compares with */
    uint32_t jump_loc[IN_JUMP_TARGETS]; /* the branch to its translation */
    int n_used;
};

#if defined(__aarch64__)
/* MOVZ/MOVK pair that loads pc into R10 at fixed length. */
static void encode_slot_pc(uint32_t mov[2], uint32_t pc)
{
    mov[0] = MW_MOVZ | ((pc & 0xffff) << 5) | R10;
    mov[1] = MW_MOVK | (1 << 21) | ((pc >> 16) << 5) | R10;
}
#endif

/* Emit a slot that branches to the translation of pc, or an empty one. */
static void emit_indirect_slot(struct jit_state *state,
                               struct indirect_site *site,
                               int slot,
                               uint32_t pc)
{
    const bool empty = pc == EMPTY_SLOT_PC;
#if defined(__x86_64__)
    /* cmp temp, imm32; je rel32, which falls through until patched */
    emit_alu32_raw(state, 0x81, 7, temp_reg);
    site->imm_loc[slot] = state->offset;
    emit4(state, pc);
    emit1(state, 0x0f);
    emit1(state, JCC_JE);
    site->jump_loc[slot] = state->offset;
    if (empty)
        emit4(state, 0);
    else
        emit_jump_target_address(state, pc, 0);
#elif defined(__aarch64__)
    /* movz/movk R10, pc; cmp temp, R10; b.ne +8; b target, at fixed length so
     * that a slot can be filled in place.
     */
    uint32_t mov[2];
    encode_slot_pc(mov, pc);
    site->imm_loc[slot] = state->offset;
    emit_a64(state, mov[0]);
    emit_a64(state, mov[1]);
    emit_addsub_register(state, false, AS_SUBS, RZ, temp_reg, R10);
    emit_a64(state, 0x54000000U | (2 << 5) | COND_NE);
    site->jump_loc[slot] = state->offset;
    if (empty)
        emit_a64(state, UBR_B | 1); /* falls through until patched */
    else
        emit_jmp(state, pc, 0);
#endif
}

/* Fill the next empty slot of site with a branch from pc to target_loc. */
static void indirect_site_fill(struct jit_state *state,
                               struct indirect_site *site,
                               uint32_t pc,
                               uint32_t target_loc)
{
    if (site->n_used == IN_JUMP_TARGETS)
        return;
    const int slot = site->n_used++;
    const uint32_t imm_loc = site->imm_loc[slot];
    const uint32_t jump_loc = site->jump_loc[slot];
#if defined(__APPLE__) && defined(__aarch64__)
    jit_enter_write_mode();
#endif
    /* Branch first, so that the slot never matches before it leads there. */
    patch_jump(state, jump_loc, target_loc);
#if defined(__x86_64__)
    memcpy(state->buf + imm_loc, &pc, sizeof(pc));
#elif defined(__aarch64__)
    uint32_t mov[2];
    encode_slot_pc(mov, pc);
    memcpy(state->buf + imm_loc, mov, sizeof(mov));
#if defined(__APPLE__)
    jit_exit_write_mode();
#endif
    sys_icache_invalidate(state->buf + imm_loc, sizeof(mov));
    sys_icache_invalidate(state->buf + jump_loc, sizeof(uint32_t));
#endif
}

/* Return where generated code continues at rv->PC: the translated block there,
 * or the exit to the dispatcher. A compiled target fills an empty slot of the
 * indirect jump that missed, so that it takes the slot from then on.
 */
static uintptr_t jit_indirect_target(riscv_t *rv, uint32_t site)
{
    struct jit_state *state = rv->jit_state;
    const struct offset_map *target = offset_map_find(state, rv->PC, 0);
    if (!target)
        return (uintptr_t) state->buf + state->exit_loc;
    indirect_site_fill(state, &state->sites[site], rv->PC, target->offset);
    return (uintptr_t) state->buf + target->offset;
}
#endif

/* Jump to the target in temp_reg, with every guest register written back. */
static void emit_indirect_jump(struct jit_state *state,
                               riscv_t *rv,
                               rv_insn_t *ir)
{
    branch_history_table_t *bt = ir->branch_table;
    int targets[IN_JUMP_TARGETS];
    int count = bht_select_targets(bt, rv, targets);
#if JIT_INDIRECT_LOOKUP
    /* The block being translated is the last one in the offset map. */
    const uint32_t site_index = state->n_blocks - 1;
    struct indirect_site *site = &state->sites[site_index];
    site->n_used = count;
    for (int i = 0; i < IN_JUMP_TARGETS; i++)
        emit_indirect_slot(state, site, i,
                           i < count ? bt->PC[targets[i]] : EMPTY_SLOT_PC);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_load_imm(state, parameter_reg[1], site_index);
    emit_call(state, (intptr_t) &jit_indirect_target);
#if defined(__x86_64__)
    /* jmp *%rax */
    emit1(state, 0xff);
    emit1(state, 0xe0);
#elif defined(__aarch64__)
    /* emit_call leaves the result in R5. */
    emit_uncond_branch_reg(state, BR_BR, R5);
#endif
#else
    for (int i = 0; i < count; i++) {
        int idx = targets[i];
        emit_cmp_imm32(state, temp_reg, bt->PC[idx]);
        uint32_t jump_loc_0 = state->offset;
        emit_jcc_offset(state, JCC_JNE);
#if RV32_HAS(SYSTEM)
        emit_jmp(state, bt->PC[idx], bt->satp[idx]);
#else
        emit_jmp(state, bt->PC[idx], 0);
#endif
        emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    }
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
#endif
}

/* Set the zero flag from the low bits of dst that mask selects, leaving dst
 * intact. mask is a run of low bits that fits a byte, such as 1 or 3.
 */
static inline void emit_test_low_bits(struct jit_state *state,
                                      int dst,
                                      uint32_t mask)
{
    assert(mask && mask <= 0xff && !(mask & (mask + 1)));
#if defined(__x86_64__)
    /* TEST r/m8, imm8; a REX prefix selects SIL/DIL rather than DH/BH. */
    if (dst >= 4)
        emit1(state, 0x40 | !!(dst & 8));
    emit1(state, 0xf6);
    emit_modrm_reg2reg(state, 0, dst);
    emit1(state, mask);
#elif defined(__aarch64__)
    /* A run of low bits is a logical immediate: TST Wn, #(2^len - 1) is ANDS
     * WZR, Wn with N=0, immr=0, imms=len-1.
     */
    uint32_t len = (uint32_t) __builtin_popcount(mask);
    emit_a64(state, UINT32_C(0x72000000) | ((len - 1) << 10) |
                        ((uint32_t) dst << 5) | RZ);
#endif
}

/* A misaligned-access exit, emitted after the block so that the aligned path
 * stays contiguous. The branch into it leaves the host register base, the one
 * holding the access's rs1, unchanged.
 */
struct misalign_stub {
    uint32_t jump_loc; /* where to patch the branch that enters the stub */
    uint32_t pc;
    uint32_t refund; /* unretired_after(pc) */
    uint32_t flags;
    int base;
    int32_t offset;
    struct host_reg map[ARRAY_SIZE(register_map)];
};
#define MAX_MISALIGN_STUBS 256
static struct misalign_stub misalign_stubs[MAX_MISALIGN_STUBS];
static int n_misalign_stubs;

/* Form base + offset in temp_reg. */
static void emit_misalign_address(struct jit_state *state,
                                  int base,
                                  int32_t offset)
{
    emit_mov(state, base, temp_reg);
    if (offset)
        emit_alu32_imm32(state, 0x81, 0, temp_reg, offset);
}

/* Patch the stub's branch to here, write every guest register back as the guard
 * left them, raise the misaligned exception, and leave the block.
 */
static void emit_misalign_stub(struct jit_state *state,
                               const struct misalign_stub *stub)
{
    emit_jump_target_offset(state, stub->jump_loc, state->offset);
    emit_misalign_address(state, stub->base, stub->offset);
    memcpy(register_map, stub->map, sizeof(register_map));
    store_back_all(state);
    emit_mov(state, temp_reg, parameter_reg[1]);
    emit_load_imm(state, parameter_reg[2], stub->flags);
    if (stub->refund)
        emit_cycle_refund(state, stub->refund);
    emit_load_imm(state, temp_reg, stub->pc);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_call(state, (intptr_t) &jit_misaligned_trap);
    emit_exit(state);
}

/* Emit the interpreter's alignment check for a halfword or word access at
 * X[rs1] + offset. A misaligned access branches to a stub, emitted after the
 * block by emit_misalign_stubs(), that raises the exception. Nothing is emitted
 * for byte accesses, for an offset that keeps a base already proven aligned in
 * this block aligned, or when misaligned accesses are allowed (-m) or the
 * program uses no privileged state that could observe them, in which case the
 * host performs them directly.
 */
static void emit_misalign_guard(struct jit_state *state,
                                riscv_t *rv,
                                int rs1,
                                int32_t offset,
                                enum operand_size size,
                                uint32_t flags,
                                uint32_t pc)
{
    const uint32_t mask = size == S32 ? 3 : size == S16 ? 1 : 0;
    if (!mask || rv->jit_elide_align_checks)
        return;
    const bool offset_aligned = !((uint32_t) offset & mask);
    if (offset_aligned && reg_align[rs1] > mask)
        return;

    struct misalign_stub stub = {
        .pc = pc,
        .refund = unretired_after(rv, pc),
        .flags = flags,
        .offset = offset,
    };
    stub.base = ra_load(state, rs1);
    memcpy(stub.map, register_map, sizeof(stub.map));

    /* An aligned offset leaves the low bits of the base unchanged. */
    if (offset_aligned) {
        emit_test_low_bits(state, stub.base, mask);
    } else {
        emit_misalign_address(state, stub.base, offset);
        emit_test_low_bits(state, temp_reg, mask);
    }
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, JCC_JNE);
    stub.jump_loc = JUMP_LOC_0;

    if (n_misalign_stubs < MAX_MISALIGN_STUBS) {
        misalign_stubs[n_misalign_stubs++] = stub;
    } else {
        /* Out of stub slots: emit this one inline and branch around it. */
        uint32_t jump_normal = state->offset;
        emit_jcc_offset(state, JCC_JMP);
        emit_misalign_stub(state, &stub);
        memcpy(register_map, stub.map, sizeof(register_map));
        emit_jump_target_offset(state, JUMP_NORMAL, state->offset);
    }

    /* Past the guard, X[rs1] + offset is aligned, and so is X[rs1] itself when
     * offset is.
     */
    if (offset_aligned)
        reg_align[rs1] = mask + 1;
}

/* Emit the stubs that the block's guards branch to. translate() empties the
 * list before each block.
 */
static void emit_misalign_stubs(struct jit_state *state)
{
    for (int i = 0; i < n_misalign_stubs; i++)
        emit_misalign_stub(state, &misalign_stubs[i]);
}

/* Timer increment removed: timer is now derived from cycle counter at
 * interrupt check points (rv_check_interrupt) rather than per-instruction.
 * This eliminates per-instruction memory operations in the JIT hot path.
 */

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
        vm_reg[0] = map_vm_reg(state, fuse[i].rd);
        emit_load_imm(state, vm_reg[0], fuse[i].imm);
    }
}

static void do_fuse2(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
    /* rs2 = rs1 + rd, and rd holds the immediate: add it directly. */
    vm_reg[1] = ra_load(state, ir->rs1);
    vm_reg[2] = map_vm_reg_reserved(state, ir->rs2, vm_reg[1]);
    emit_mov(state, vm_reg[1], vm_reg[2]);
    emit_alu32_imm32(state, 0x81, 0, vm_reg[2], ir->imm);
}

static void do_fuse3(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = PRIV(rv)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        emit_misalign_guard(state, rv, fuse[i].rs1, fuse[i].imm, S32,
                            JIT_MISALIGN_STORE, ir->pc + 4 * i);
        ra_store_guest(state, m, S32, fuse[i].rs1, fuse[i].rs2, fuse[i].imm);
    }
}

static void do_fuse4(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = PRIV(rv)->mem;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        vm_reg[0] = ra_load(state, fuse[i].rs1);
        emit_misalign_guard(state, rv, fuse[i].rs1, fuse[i].imm, S32, 0,
                            ir->pc + 4 * i);
        vm_reg[1] = map_vm_reg(state, fuse[i].rd);
        emit_guest_load(state, m, S32, vm_reg[0], vm_reg[1], fuse[i].imm,
                        false);
    }
}

static void do_fuse5(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        switch (fuse[i].opcode) {
        case rv_insn_slli:
            vm_reg[0] = ra_load(state, fuse[i].rs1);
            vm_reg[1] = map_vm_reg_reserved(state, fuse[i].rd, vm_reg[0]);
            if (vm_reg[0] != vm_reg[1])
                emit_mov(state, vm_reg[0], vm_reg[1]);
            emit_alu32_imm8(state, 0xc1, 4, vm_reg[1], fuse[i].imm & 0x1f);
            break;
        case rv_insn_srli:
            vm_reg[0] = ra_load(state, fuse[i].rs1);
            vm_reg[1] = map_vm_reg_reserved(state, fuse[i].rd, vm_reg[0]);
            if (vm_reg[0] != vm_reg[1])
                emit_mov(state, vm_reg[0], vm_reg[1]);
            emit_alu32_imm8(state, 0xc1, 5, vm_reg[1], fuse[i].imm & 0x1f);
            break;
        case rv_insn_srai:
            vm_reg[0] = ra_load(state, fuse[i].rs1);
            vm_reg[1] = map_vm_reg_reserved(state, fuse[i].rd, vm_reg[0]);
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

/* fused LI a7, imm + ECALL
 * This fusion is only available in standard RV32I/M/A/F/C since RV32E
 * uses a different syscall convention (t0 instead of a7).
 */
#if !RV32_HAS(RV32E)
static void do_fuse6(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    /* Set a7 = syscall number (imm) */
    vm_reg[0] = map_vm_reg(state, rv_reg_a7);
    emit_load_imm(state, vm_reg[0], ir->imm);
    /* Store back all registers and call ecall handler.
     * ECALL is at ir->pc + 4 (second instruction in fused pair).
     */
    store_back(state);
    emit_load_imm(state, temp_reg, ir->pc + 4);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_call(state, (intptr_t) rv->io.on_ecall);
    emit_exit(state);
}
#else
/* RV32E stub: fuse6 pattern is never generated for RV32E.
 * Defensive fallback - emit exit if unexpectedly reached.
 */
static void do_fuse6(struct jit_state *state,
                     riscv_t *rv UNUSED,
                     rv_insn_t *ir UNUSED)
{
    assert(!"fuse6 should not be called in RV32E mode");
    emit_exit(state);
}
#endif

/* fused multiple ADDI */
static void do_fuse7(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        vm_reg[0] = ra_load(state, fuse[i].rs1);
        vm_reg[1] = map_vm_reg_reserved(state, fuse[i].rd, vm_reg[0]);
        if (vm_reg[0] != vm_reg[1])
            emit_mov(state, vm_reg[0], vm_reg[1]);
        emit_alu32_imm32(state, 0x81, 0, vm_reg[1], fuse[i].imm);
    }
}

/* fused LUI + ADDI: 32-bit constant load (li pseudo-op)
 * rd = (lui_imm << 12) + addi_imm = ir->imm + ir->imm2
 */
static void do_fuse8(struct jit_state *state, riscv_t *rv UNUSED, rv_insn_t *ir)
{
    vm_reg[0] = map_vm_reg(state, ir->rd);
    /* Compute combined immediate at JIT compile time.
     * Cast to uint32_t to avoid signed overflow UB.
     */
    uint32_t combined_imm = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    emit_load_imm(state, vm_reg[0], combined_imm);
}

/* fused LUI + LW: absolute address load
 * addr = ir->imm (lui << 12) + ir->imm2 (lw offset)
 * ir->rs2 = destination register for load
 */
static void do_fuse9(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = PRIV(rv)->mem;
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
#if RV32_HAS(SYSTEM_MMIO)
    /* Write LUI result to rd - required when rd != LW destination.
     * LUI completes before LW, so this write happens even if LW faults.
     */
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);

    /* Load constant vaddr into temp_reg.  It will be passed as the vaddr
     * argument to jit_mmu_handler, replacing the prior three stores into
     * rv->jit_mmu.{vaddr,type,pc}.
     */
    emit_load_imm(state, temp_reg, addr);
    store_back(state);
    emit_jit_mmu_handler(state, ir->rs2, temp_reg, rv_insn_lw, ir->pc + 4,
                         unretired_after(rv, ir->pc + 4));
    reset_reg();

    /* Check if trap occurred during MMU translation.
     * If trapped, skip the load entirely to avoid loading garbage.
     */
    emit_load(state, S8, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.abort));
    emit_cmp_imm32(state, temp_reg, 0);
    uint32_t jump_trap = state->offset;
    emit_jcc_offset(state, JCC_JNE); /* Jump to end if trapped */

    /* If MMIO, value already in X[rd]; otherwise load from translated paddr */
    emit_load(state, S8, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.is_mmio));
    emit_cmp_imm32(state, temp_reg, 0);
    vm_reg[0] = map_vm_reg(state, ir->rs2);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, JCC_JE);

    /* MMIO path: load from X[rd] */
    emit_load(state, S32, parameter_reg[0], vm_reg[0],
              offsetof(riscv_t, X) + 4 * ir->rs2);
    uint32_t jump_loc_1 = state->offset;
    emit_jcc_offset(state, JCC_JMP);

    /* RAM path: load from mem_base + paddr.
     * Reuse vm_reg[0] (already mapped to ir->rs2) for address calculation,
     * then load into the same register - matches GEN_LOAD pattern.
     */
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    emit_load(state, S32, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.paddr));
    emit_load_imm_sext(state, vm_reg[0], (intptr_t) m->mem_base);
    emit_alu64(state, ALU_OP_ADD, temp_reg, vm_reg[0]);
    emit_load(state, S32, vm_reg[0], vm_reg[0], 0);
    emit_jump_target_offset(state, JUMP_LOC_1, state->offset);
    /* Jump over trap exit to continue normally */
    uint32_t jump_normal = state->offset;
    emit_jcc_offset(state, JCC_JMP);
    /* Trap exit point - exit JIT block for trap handling */
    emit_jump_target_offset(state, JUMP_TRAP, state->offset);
    emit_exit(state);
    /* Normal continuation point */
    emit_jump_target_offset(state, JUMP_NORMAL, state->offset);
#else
    /* Write LUI result to rd - required when rd != LW destination */
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
    emit_load_imm_sext(state, temp_reg, (intptr_t) (m->mem_base + addr));
    vm_reg[1] = map_vm_reg(state, ir->rs2);
    emit_load(state, S32, temp_reg, vm_reg[1], 0);
#endif
}

/* fused LUI + SW: absolute address store
 * addr = ir->imm (lui << 12) + ir->imm2 (sw offset)
 * ir->rs1 = source register for store
 */
static void do_fuse10(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = PRIV(rv)->mem;
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
#if RV32_HAS(SYSTEM_MMIO)
    /* Write LUI result to rd - SW doesn't write registers, so rd may be
     * used later. LUI completes before SW, so this write happens even if
     * SW faults.
     */
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);

    /* Constant vaddr -> temp_reg, passed as call arg (see do_fuse9). */
    emit_load_imm(state, temp_reg, addr);
    store_back(state);
    emit_jit_mmu_handler(state, ir->rs1, temp_reg, rv_insn_sw, ir->pc + 4,
                         unretired_after(rv, ir->pc + 4));
    reset_reg();

    /* Check if trap occurred - skip store if trapped */
    emit_load(state, S8, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.abort));
    emit_cmp_imm32(state, temp_reg, 0);
    uint32_t jump_trap = state->offset;
    emit_jcc_offset(state, JCC_JNE); /* Jump to end if trapped */

    /* If MMIO, skip store (handled by MMU handler) */
    emit_load(state, S8, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.is_mmio));
    emit_cmp_imm32(state, temp_reg, 1);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, JCC_JE);

    /* RAM path: store to mem_base + paddr.
     * SW doesn't write registers, so rd can be used as scratch.
     * Note: Cannot use rv_reg_zero as scratch because emit_load has special
     * handling that returns 0 for any load targeting a register mapped to x0.
     */
    emit_load(state, S32, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.paddr));
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm_sext(state, vm_reg[0], (intptr_t) m->mem_base);
    emit_alu64(state, ALU_OP_ADD, temp_reg, vm_reg[0]);
    vm_reg[1] = ra_load(state, ir->rs1);
    emit_store(state, S32, vm_reg[1], vm_reg[0], 0);
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    /* Jump over trap exit to continue normally */
    uint32_t jump_normal = state->offset;
    emit_jcc_offset(state, JCC_JMP);
    /* Trap exit point - exit JIT block for trap handling */
    emit_jump_target_offset(state, JUMP_TRAP, state->offset);
    emit_exit(state);
    /* Normal continuation point */
    emit_jump_target_offset(state, JUMP_NORMAL, state->offset);
    reset_reg();
#else
    /* Write LUI result to rd - SW doesn't write registers, so rd may be used */
    vm_reg[0] = map_vm_reg(state, ir->rd);
    emit_load_imm(state, vm_reg[0], ir->imm);
    vm_reg[1] = ra_load(state, ir->rs1);
    emit_load_imm_sext(state, temp_reg, (intptr_t) (m->mem_base + addr));
    emit_store(state, S32, vm_reg[1], temp_reg, 0);
#endif
}

/* fused LW + ADDI (post-increment load)
 * addr = rv->X[ir->rs1] + ir->imm
 * ir->rd = load destination
 * ir->rs1 += ir->imm2 (increment)
 */
static void do_fuse11(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    memory_t *m = PRIV(rv)->mem;
#if RV32_HAS(SYSTEM_MMIO)
    /* Compute virtual address: rs1 + imm.  After the ADD, vaddr lives in
     * temp_reg and is handed to jit_mmu_handler as the vaddr argument.
     */
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_misalign_guard(state, rv, ir->rs1, ir->imm, S32, 0, ir->pc);
    emit_load_imm_sext(state, temp_reg, ir->imm);
    emit_alu32(state, ALU_OP_ADD, vm_reg[0], temp_reg);

    store_back(state);
    emit_jit_mmu_handler(state, ir->rd, temp_reg, rv_insn_lw, ir->pc,
                         unretired_after(rv, ir->pc));
    reset_reg();

    /* Check if trap occurred during MMU translation.
     * If trapped, skip the load and post-increment entirely.
     * trap_cnt is set by jit_mmu_handler when mem_translate faults.
     */
    emit_load(state, S8, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.abort));
    emit_cmp_imm32(state, temp_reg, 0);
    uint32_t jump_trap = state->offset;
    emit_jcc_offset(state, JCC_JNE); /* Jump to end if trapped */

    /* If MMIO, value already in X[rd]; otherwise load from translated paddr */
    emit_load(state, S8, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.is_mmio));
    emit_cmp_imm32(state, temp_reg, 0);
    vm_reg[0] = map_vm_reg(state, ir->rd);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, JCC_JE);

    /* MMIO path: load from X[rd] */
    emit_load(state, S32, parameter_reg[0], vm_reg[0],
              offsetof(riscv_t, X) + 4 * ir->rd);
    uint32_t jump_loc_1 = state->offset;
    emit_jcc_offset(state, JCC_JMP);

    /* RAM path: load from mem_base + paddr.
     * Reuse vm_reg[0] (already mapped to ir->rd) for address calculation,
     * then load into the same register - matches GEN_LOAD pattern.
     */
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    emit_load(state, S32, parameter_reg[0], temp_reg,
              offsetof(riscv_t, jit_mmu.paddr));
    emit_load_imm_sext(state, vm_reg[0], (intptr_t) m->mem_base);
    emit_alu64(state, ALU_OP_ADD, temp_reg, vm_reg[0]);
    emit_load(state, S32, vm_reg[0], vm_reg[0], 0);
    emit_jump_target_offset(state, JUMP_LOC_1, state->offset);

    /* Post-increment rs1 by imm2 (only executed if no trap).
     * Must use ra_load to load rs1's value from memory since reset_reg() was
     * called after store_back(). Without loading, we'd increment garbage.
     */
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm32(state, 0x81, 0, vm_reg[0], ir->imm2);
    /* Jump over trap exit to continue normally */
    uint32_t jump_normal = state->offset;
    emit_jcc_offset(state, JCC_JMP);
    /* Trap exit point - exit JIT block for trap handling */
    emit_jump_target_offset(state, JUMP_TRAP, state->offset);
    emit_exit(state);
    /* Normal continuation point */
    emit_jump_target_offset(state, JUMP_NORMAL, state->offset);
#else
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_misalign_guard(state, rv, ir->rs1, ir->imm, S32, 0, ir->pc);
    /* Compute address: mem_base + rs1 + imm */
    /* Load value into rd */
    vm_reg[1] = map_vm_reg(state, ir->rd);
    emit_guest_load(state, m, S32, vm_reg[0], vm_reg[1], ir->imm, false);
    /* Increment rs1 by imm2.  Mapping rd may have evicted rs1, whose value
     * was saved to memory, so reload it rather than assume it is mapped. */
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm32(state, 0x81, 0, vm_reg[0], ir->imm2);
    set_dirty(vm_reg[0], true); /* Mark rs1 dirty so it's saved to memory */
#endif
}

/* fused SW + ADDI (post-increment store). match_pattern() creates this
 * operation only in non-SYSTEM builds, where direct RAM stores cannot trap.
 */
static void do_fuse13(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
#if !RV32_HAS(SYSTEM)
    memory_t *m = PRIV(rv)->mem;
    emit_misalign_guard(state, rv, ir->rs1, ir->imm, S32, JIT_MISALIGN_STORE,
                        ir->pc);
    ra_store_guest(state, m, S32, ir->rs1, ir->rs2, ir->imm);
    /* Loading rs2 may have evicted rs1, so reload it before the increment. */
    vm_reg[0] = ra_load(state, ir->rs1);
    emit_alu32_imm32(state, 0x81, 0, vm_reg[0], ir->imm2);
    set_dirty(vm_reg[0], true);
#else
    (void) state;
    (void) rv;
    (void) ir;
    __UNREACHABLE;
#endif
}

/* fused ADDI + BNE (loop counter decrement-branch)
 * rd = rs1 + imm
 * if rd != 0, branch to PC + 4 + imm2
 * This is a branching instruction, so we must store back and exit
 */
static void do_fuse12(struct jit_state *state, riscv_t *rv, rv_insn_t *ir)
{
    /* Compute rd = rs1 + imm */
    vm_reg[0] = ra_load(state, ir->rs1);
    vm_reg[1] = map_vm_reg_reserved(state, ir->rd, vm_reg[0]);
    if (vm_reg[0] != vm_reg[1])
        emit_mov(state, vm_reg[0], vm_reg[1]);
    emit_alu32_imm32(state, 0x81, 0, vm_reg[1], ir->imm);
    store_back(state);
    /* Keep the comparison adjacent to its branch for host macro-fusion. */
    emit_cmp_imm32(state, vm_reg[1], 0);
    /* jne (jump if not equal) to taken path: 0x85 = JNE */
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, 0x85);
    /* Untaken path: rd == 0, fall through to PC + 8 */
    if (JIT_LINK_ALL_EDGES || ir->branch_untaken)
        emit_jmp(state, ir->pc + 8, rv->csr_satp);
    emit_load_imm(state, temp_reg, ir->pc + 8);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
    /* Taken path: rd != 0, branch to PC + 4 + imm2 */
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    if (JIT_LINK_ALL_EDGES || ir->branch_taken)
        emit_jmp(state, ir->pc + 4 + ir->imm2, rv->csr_satp);
    emit_load_imm(state, temp_reg, ir->pc + 4 + ir->imm2);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
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

static void clear_hot(block_t *block)
{
    block->hot = false;
}

static void code_cache_flush(struct jit_state *state, riscv_t *rv)
{
    should_flush = false;
    state->offset = state->org_size;
    offset_map_reset(state);
    clear_cache_hot(rv->block_cache, (clear_func_t) clear_hot);
#if RV32_HAS(T2C)
    jit_cache_clear(rv->jit_cache);
    inline_cache_clear(rv->inline_cache);
#endif
    return;
}

#if RV32_HAS(SYSTEM)
/* The caller holds cache_lock when T2C is built */
void jit_flush(riscv_t *rv)
{
    code_cache_flush(rv->jit_state, rv);
}
#endif

typedef void (*codegen_block_func_t)(struct jit_state *,
                                     riscv_t *,
                                     rv_insn_t *);

#if defined(__x86_64__)
struct shift_add {
    uint8_t source, base, result, scale;
};

/* Combine a scaled value with its following addition when the intermediate
 * is overwritten. Include the last member of a fused shift sequence.
 */
static bool shift_add_pair(const rv_insn_t *shift,
                           const rv_insn_t *add,
                           struct shift_add *plan)
{
    if (!add || add->opcode != rv_insn_add)
        return false;
    opcode_fuse_t op;
    if (shift->opcode == rv_insn_slli) {
        op = (opcode_fuse_t) {
            .opcode = shift->opcode,
            .rd = shift->rd,
            .rs1 = shift->rs1,
            .imm = shift->imm,
        };
    } else if (shift->opcode == rv_insn_fuse5 && shift->imm2 > 0) {
        op = shift->fuse[shift->imm2 - 1];
    } else {
        return false;
    }
    if (op.opcode != rv_insn_slli || !op.rd || add->rd != op.rd ||
        (op.imm & 31) < 1 || (op.imm & 31) > 3)
        return false;
    uint8_t base;
    if (add->rs1 == op.rd)
        base = add->rs2;
    else if (add->rs2 == op.rd)
        base = add->rs1;
    else
        return false;
    if (base == op.rd)
        return false;
    /* LEA leaves flags untouched. Retain ADD where the next record could
     * forward its carry into SLTU.
     */
    const rv_insn_t *next = block_next_ir(add);
    if (next && next->opcode == rv_insn_sltu)
        return false;
    *plan = (struct shift_add) {
        .source = op.rs1,
        .base = base,
        .result = op.rd,
        .scale = op.imm & 31,
    };
    return true;
}

static void emit_shift_add(struct jit_state *state,
                           const struct shift_add *plan)
{
    ra_load2(state, plan->source, plan->base);
    int index = vm_reg[0], base = vm_reg[1];
    int result = map_vm_reg_reserved2(state, plan->result, index, base);
    if ((result | index | base) & 8)
        emit_rex(state, 0, !!(result & 8), !!(index & 8), !!(base & 8));
    emit1(state, 0x8d); /* LEA r32, [base + index * scale] */
    emit_sib_operand(state, result, base, index, plan->scale, 0);
    set_dirty(result, true);
}
#endif

/* Translate one record, with two x86 peepholes across adjacent records: a
 * small left shift and the ADD consuming it become one LEA, emitted with the
 * shift, and an ADD forwards its carry to a following unsigned carry test.
 * Allocation emits only register moves and X[] spills between them, which
 * preserve x86 flags. Keep the comparison when its operand was overwritten by
 * the addition.
 */
static void translate_record(struct jit_state *state,
                             riscv_t *rv,
                             rv_insn_t *ir,
                             const rv_insn_t *previous)
{
#if defined(__x86_64__)
    struct shift_add scaled;
    if (previous && shift_add_pair(previous, ir, &scaled))
        return; /* Its result was emitted with the preceding shift. */
    if (shift_add_pair(ir, block_next_ir(ir), &scaled)) {
        if (ir->opcode == rv_insn_fuse5 && ir->imm2 > 1) {
            rv_insn_t prefix = *ir;
            prefix.imm2--;
            do_fuse5(state, rv, &prefix);
        }
        emit_shift_add(state, &scaled);
        return;
    }
    if (previous && previous->opcode == rv_insn_add && previous->rd &&
        ir->opcode == rv_insn_sltu && ir->rs1 == previous->rd &&
        ir->rs2 != previous->rd &&
        (ir->rs2 == previous->rs1 || ir->rs2 == previous->rs2)) {
        if (ir->rd)
            emit_set_less_than(state, JCC_JB, map_vm_reg(state, ir->rd));
        return;
    }
#else
    (void) previous;
#endif
    ((codegen_block_func_t) dispatch_table[ir->opcode])(state, rv, ir);
}

/* A local backedge can reuse one register map when every operand fits and
 * the body calls no helper. External entries still load X[], and both exits
 * spill before using the ordinary link ABI.
 */
#if !RV32_HAS(SYSTEM) && !RV32_HAS(GDBSTUB) && !defined(_WIN32)
#define JIT_REGISTER_LOOP 1
#else
#define JIT_REGISTER_LOOP 0
#endif

#if JIT_REGISTER_LOOP
#define LOOP_REG(r) (1U << (r))

/* Store pc as the guest PC and leave generated code. */
static void emit_set_pc_exit(struct jit_state *state, uint32_t pc)
{
    emit_load_imm(state, temp_reg, pc);
    emit_store(state, S32, temp_reg, parameter_reg[0], offsetof(riscv_t, PC));
    emit_exit(state);
}

/* Conditional codes form complementary pairs whose x86-64 encodings differ
 * only in bit 0; emit_jcc_offset() maps either member on Arm64.
 */
static inline int jcc_invert(int code)
{
    assert(code != JCC_JMP);
    return code ^ 1;
}

/* Report the guest registers that one body record reads (*use) and writes
 * (*def). Accept only records whose user-mode emitters do not call helpers
 * or reset the allocator. M-extension emitters borrow RAX/RDX on x86-64 but
 * restore their mappings. Reject a write to x0: some emitters would clobber
 * the host copy of x0 that a later read in the loop still relies on.
 */
static bool loop_body_regs(const rv_insn_t *ir,
                           uint32_t *use_out,
                           uint32_t *def_out)
{
    const opcode_fuse_t *fuse = ir->fuse;
    uint32_t use = 0, def = 0;
    /* The decoder fills in the implicit operands of the compressed forms:
     * rs1 = rd for those updating rd in place, and rs1 = sp for the
     * stack-relative ones, so they share the base groups.
     */
    switch (ir->opcode) {
    case rv_insn_nop:
#if RV32_HAS(EXT_C)
    case rv_insn_cnop:
#endif
        break;
    case rv_insn_lui:
    case rv_insn_auipc:
    case rv_insn_fuse8: /* LUI + ADDI */
#if RV32_HAS(EXT_C)
    case rv_insn_cli:
    case rv_insn_clui:
#endif
        def = LOOP_REG(ir->rd);
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
    case rv_insn_lb:
    case rv_insn_lh:
    case rv_insn_lw:
    case rv_insn_lbu:
    case rv_insn_lhu:
#if RV32_HAS(EXT_C)
    case rv_insn_caddi4spn:
    case rv_insn_caddi:
    case rv_insn_caddi16sp:
    case rv_insn_cslli:
    case rv_insn_csrli:
    case rv_insn_csrai:
    case rv_insn_candi:
    case rv_insn_clw:
    case rv_insn_clwsp:
#endif
        use = LOOP_REG(ir->rs1);
        def = LOOP_REG(ir->rd);
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
#if RV32_HAS(EXT_M)
    case rv_insn_mul:
    case rv_insn_mulh:
    case rv_insn_mulhsu:
    case rv_insn_mulhu:
    case rv_insn_div:
    case rv_insn_divu:
    case rv_insn_rem:
    case rv_insn_remu:
#endif
#if RV32_HAS(EXT_C)
    case rv_insn_csub:
    case rv_insn_cxor:
    case rv_insn_cor:
    case rv_insn_cand:
    case rv_insn_cadd:
#endif
        use = LOOP_REG(ir->rs1) | LOOP_REG(ir->rs2);
        def = LOOP_REG(ir->rd);
        break;
    case rv_insn_sb:
    case rv_insn_sh:
    case rv_insn_sw:
#if RV32_HAS(EXT_C)
    case rv_insn_csw:
    case rv_insn_cswsp:
#endif
        use = LOOP_REG(ir->rs1) | LOOP_REG(ir->rs2);
        break;
#if RV32_HAS(EXT_C)
    case rv_insn_cmv:
        use = LOOP_REG(ir->rs2);
        def = LOOP_REG(ir->rd);
        break;
#endif
    /* Fused records, in their user-mode (non-MMIO) forms. */
    case rv_insn_fuse1: /* LUI ... LUI */
        for (int i = 0; i < ir->imm2; i++)
            def |= LOOP_REG(fuse[i].rd);
        break;
    case rv_insn_fuse2: /* rd = imm; rs2 = rs1 + rd */
        use = LOOP_REG(ir->rs1) & ~LOOP_REG(ir->rd);
        def = LOOP_REG(ir->rd) | LOOP_REG(ir->rs2);
        break;
    case rv_insn_fuse3: /* SW ... SW */
        for (int i = 0; i < ir->imm2; i++)
            use |= LOOP_REG(fuse[i].rs1) | LOOP_REG(fuse[i].rs2);
        break;
    case rv_insn_fuse4: /* LW ... LW */
    case rv_insn_fuse5: /* shift-immediate ... */
    case rv_insn_fuse7: /* ADDI ... ADDI */
        for (int i = 0; i < ir->imm2; i++) {
            /* Fusion preserves instruction order. A value defined by an
             * earlier member is a temporary, not another loop input.
             */
            use |= LOOP_REG(fuse[i].rs1) & ~def;
            def |= LOOP_REG(fuse[i].rd);
        }
        break;
    case rv_insn_fuse9: /* LUI rd; LW rs2 */
        def = LOOP_REG(ir->rd) | LOOP_REG(ir->rs2);
        break;
    case rv_insn_fuse10: /* LUI rd; SW rs1 */
        use = LOOP_REG(ir->rs1);
        def = LOOP_REG(ir->rd);
        break;
    case rv_insn_fuse11: /* LW rd, (rs1); ADDI rs1 */
        use = LOOP_REG(ir->rs1);
        def = LOOP_REG(ir->rd) | LOOP_REG(ir->rs1);
        break;
    case rv_insn_fuse13: /* SW rs2, (rs1); ADDI rs1 */
        use = LOOP_REG(ir->rs1) | LOOP_REG(ir->rs2);
        def = LOOP_REG(ir->rs1);
        break;
    default:
        return false;
    }
    if (def & LOOP_REG(rv_reg_zero))
        return false;
    *use_out = use;
    *def_out = def;
    return true;
}

#if defined(__x86_64__)
struct loop_rotate {
    const rv_insn_t *shift, *invert;
    uint8_t mask, value, count;
};

/* A one-bit mask shifted by a count advancing once per iteration can rotate
 * instead. Guard the invariant source at entry; another source value uses an
 * ordinary copy of the loop. Keep the architectural mask current at exits.
 */
static bool loop_rotate_plan(const block_t *block,
                             uint32_t defined,
                             uint8_t lhs,
                             uint8_t rhs,
                             struct loop_rotate *plan)
{
    const rv_insn_t *tail = block->ir_tail;
    for (const rv_insn_t *shift = block->ir_head; shift != tail;
         shift = block_next_ir(shift)) {
        if (shift->opcode != rv_insn_sll || !shift->rd ||
            shift->rd == shift->rs1 || shift->rd == shift->rs2 ||
            shift->rs1 == shift->rs2 || (defined & LOOP_REG(shift->rs1)) ||
            lhs == shift->rd || rhs == shift->rd)
            continue;
        const rv_insn_t *invert = NULL;
        bool after_shift = false, increment = false, consumer = false;
        bool valid = true;
        for (const rv_insn_t *ir = block->ir_head; ir != tail;
             ir = block_next_ir(ir)) {
            uint32_t use, def;
            if (!loop_body_regs(ir, &use, &def)) {
                valid = false;
                break;
            }
            if (ir == shift) {
                after_shift = true;
                continue;
            }
            if (def & LOOP_REG(shift->rs2)) {
                if (!after_shift || increment || ir->opcode != rv_insn_addi ||
                    ir->rd != shift->rs2 || ir->rs1 != shift->rs2 ||
                    ir->imm != 1) {
                    valid = false;
                    break;
                }
                increment = true;
            }
            if ((use | def) & LOOP_REG(shift->rd)) {
                if (!after_shift) {
                    valid = false;
                    break;
                }
                if (ir->opcode == rv_insn_xori && ir->rd == shift->rd &&
                    ir->rs1 == shift->rd && ir->imm == -1 && !invert &&
                    !consumer) {
                    invert = ir;
                } else if (!(def & LOOP_REG(shift->rd)) &&
                           (ir->opcode == rv_insn_or ||
                            ir->opcode == rv_insn_xor ||
                            ir->opcode == rv_insn_and)) {
                    consumer = true;
                } else {
                    valid = false;
                    break;
                }
            }
        }
        if (tail->opcode == rv_insn_fuse12 && tail->rd == shift->rs2) {
            if (increment || tail->rs1 != shift->rs2 || tail->imm != 1)
                valid = false;
            increment = true;
        }
        if (valid && increment && consumer) {
            *plan = (struct loop_rotate) {
                .shift = shift,
                .invert = invert,
                .mask = shift->rd,
                .value = shift->rs1,
                .count = shift->rs2,
            };
            return true;
        }
    }
    return false;
}
#endif

/* A conditional block tail continues at target while "lhs <jcc> rhs" holds and
 * at other_pc otherwise. Returns false for any other tail, after filling lhs,
 * rhs, target, and other_pc from its operands.
 */
struct tail_branch {
    uint8_t lhs, rhs;
    int jcc;
    uint32_t target, other_pc;
    bool add_before_branch; /* fuse12: ADDI rd, rs1, imm; BNE rd, x0 */
};

static bool tail_branch(const rv_insn_t *tail, struct tail_branch *branch)
{
    *branch = (struct tail_branch) {
        .lhs = tail->rs1,
        .rhs = tail->rs2,
        .target = tail->pc + tail->imm,
        .other_pc = tail->pc + 4,
    };
    switch (tail->opcode) {
    case rv_insn_beq:
        branch->jcc = JCC_JE;
        return true;
    case rv_insn_bne:
        branch->jcc = JCC_JNE;
        return true;
    case rv_insn_blt:
        branch->jcc = JCC_JL;
        return true;
    case rv_insn_bge:
        branch->jcc = JCC_JGE;
        return true;
    case rv_insn_bltu:
        branch->jcc = JCC_JB;
        return true;
    case rv_insn_bgeu:
        branch->jcc = JCC_JAE;
        return true;
#if RV32_HAS(EXT_C)
    case rv_insn_cbeqz:
    case rv_insn_cbnez:
        branch->jcc = tail->opcode == rv_insn_cbeqz ? JCC_JE : JCC_JNE;
        branch->rhs = rv_reg_zero;
        branch->other_pc = tail->pc + 2;
        return true;
#endif
    case rv_insn_fuse12:
        /* The branch offset is relative to the BNE, the second instruction.
         * The matcher excludes rd = x0.
         */
        branch->jcc = JCC_JNE;
        branch->lhs = tail->rd;
        branch->rhs = rv_reg_zero;
        branch->target = tail->pc + 4 + tail->imm2;
        branch->other_pc = tail->pc + 8;
        branch->add_before_branch = true;
        return true;
    default:
        return false;
    }
}

/* Set the flags a conditional tail branches on, with lhs and rhs already
 * normalized by branch_zero_rhs(). A fused ADDI; BNE sets them by its addition.
 */
static void emit_tail_compare(struct jit_state *state,
                              const rv_insn_t *tail,
                              bool add_before_branch,
                              uint8_t lhs,
                              uint8_t rhs)
{
    if (add_before_branch) {
        vm_reg[0] = ra_load(state, tail->rs1);
        vm_reg[1] = map_vm_reg_reserved(state, tail->rd, vm_reg[0]);
        if (vm_reg[0] != vm_reg[1])
            emit_mov(state, vm_reg[0], vm_reg[1]);
#if defined(__x86_64__)
        emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_ADD, vm_reg[1], tail->imm);
#elif defined(__aarch64__)
        emit_add_imm(state, false, true, vm_reg[1], vm_reg[1], tail->imm);
#endif
    } else if (rhs) {
        ra_load2(state, lhs, rhs);
        emit_cmp32(state, vm_reg[1], vm_reg[0]);
    } else {
        emit_cmp_imm32(state, ra_load(state, lhs), 0);
    }
}

static bool translate_register_loop(struct jit_state *state,
                                    riscv_t *rv,
                                    block_t *block,
                                    bool allow_rotate UNUSED)
{
    cur_block = block;
    /* Classify the tail: the loop continues while "lhs <jcc> rhs" holds. */
    const rv_insn_t *tail = block->ir_tail;
    struct tail_branch branch;
    if (!tail_branch(tail, &branch) || !tail->branch_taken ||
        branch.target != block->pc_start)
        return false;
    uint8_t lhs = branch.lhs, rhs = branch.rhs;
    uint32_t exit_pc = branch.other_pc;
    int jcc = branch.jcc;

    /* Compare a lone nonzero operand with zero; x0 needs no host register. */
    branch_zero_rhs(&lhs, &rhs, &jcc);

    /* Registers read before any write in an iteration are loop inputs and
     * must be loaded on entry; the others are only mapped.
     */
    uint32_t regs = 0, live_in = 0, defined = 0, use, def;
    for (const rv_insn_t *ir = block->ir_head;; ir = block_next_ir(ir)) {
        if (ir != tail) {
            if (!loop_body_regs(ir, &use, &def))
                return false;
        } else if (tail->opcode == rv_insn_fuse12) {
            /* The ADDI reads rs1 before writing rd, which the BNE tests. */
            use = LOOP_REG(tail->rs1);
            def = LOOP_REG(tail->rd);
        } else {
            use = LOOP_REG(lhs) | (rhs ? LOOP_REG(rhs) : 0);
            def = 0;
        }
        live_in |= use & ~defined;
        defined |= def;
        regs |= use | def;
        if (ir == tail)
            break;
    }
    const bool spilling = __builtin_popcount(regs) > n_host_regs;
    /* Like libriscv 075154d6's region register caching, keep values resident
     * across the backedge. Leave room for two temporary operands and a
     * distinct destination. Temporaries are defined before their first use.
     */
    if (spilling && __builtin_popcount(live_in) > n_host_regs - 3)
        return false;

    /* A guard can exit before an iteration's first write to a register.
     * Full-register loops load every value up front. Spilling loops instead
     * commit temporaries at each backedge, so X[] supplies their previous
     * iteration values until this iteration defines them.
     */
    const bool guards = !rv->jit_elide_align_checks;
#if defined(__x86_64__)
    struct loop_rotate rotate = {0};
    bool rotating =
        allow_rotate && !guards && !spilling &&
        state->n_jumps + 2 * block_jump_budget(block) + 8 < MAX_JUMPS &&
        loop_rotate_plan(block, defined, lhs, rhs, &rotate);
#endif
    if (guards && !spilling)
        live_in = regs;

    reset_reg();
    if (spilling) {
        liveness_reset();
        liveness_calc(block);
    }
    for (int reg = 0; reg < N_RV_REGS; reg++) {
        if (live_in & LOOP_REG(reg))
            ra_load(state, reg);
        else if (!spilling && (regs & LOOP_REG(reg)))
            map_vm_reg(state, reg);
    }
    if (spilling)
        loop_pinned_regs = live_in;
    if (guards) {
        for (int i = 0; i < n_host_regs; i++) {
            int reg = register_map[i].vm_reg_idx;
            if (reg >= 0 && (defined & LOOP_REG(reg)))
                register_map[i].dirty = true;
        }
    }
#if defined(__x86_64__)
    uint32_t rotate_guard = 0;
    if (rotating) {
        emit_cmp_imm32(state, ra_load(state, rotate.value), 1);
        uint32_t jump_loc_0 = state->offset;
        emit_jcc_offset(state, JCC_JNE);
        rotate_guard = JUMP_LOC_0;
        int mask = map_vm_reg(state, rotate.mask);
        int count = ra_load(state, rotate.count);
        emit_load_imm(state, mask, 1);
        emit_shift32(state, SHIFT_SHL, mask, count, mask);
        if (rotate.invert)
            emit_alu32_imm32(state, ALU_GRP1_OPCODE, ALU_XOR, mask, -1);
    }
#endif
    /* Full-register loops keep every mapping through the backedge. Spilling
     * loops pin only live-in values and commit/discard temporary mappings
     * before repeating, so the next iteration starts with the same map.
     */
    emit_load_imm(state, parameter_reg[1], 1024);
    uint32_t loop_offset = state->offset;
    /* A program that reads counters charges each block on entry, so charge
     * every iteration; cycle_reg is outside the allocator.
     */
    if (!rv->jit_entry_cycles)
        emit_cycle_count(state, block->cycle_cost);
    uint32_t idx = 0;
    rv_insn_t *previous = NULL;
    for (rv_insn_t *ir = block->ir_head; ir != tail;
         previous = ir, ir = block_next_ir(ir), idx++) {
        if (spilling)
            regs_refresh(idx);
#if defined(__x86_64__)
        if (rotating && (ir == rotate.shift || ir == rotate.invert))
            continue;
#endif
        translate_record(state, rv, ir, previous);
    }

    emit_tail_compare(state, tail, branch.add_before_branch, lhs, rhs);
    /* Spilling loops repeat with only the pinned values mapped. With guards,
     * whose stubs read X[] mid-iteration, temporaries are stored here. Without
     * them, nothing reads X[] inside the loop, so the backedge drops their
     * mappings and only the exits store them, from iter_map: the registers
     * still hold the last iteration's values on both exit paths.
     */
    struct host_reg iter_map[ARRAY_SIZE(register_map)];
    const bool defer_spill = spilling && !guards;
    if (spilling) {
        memcpy(iter_map, register_map, sizeof(iter_map));
        for (int i = 0; i < n_host_regs; i++) {
            if (register_map[i].vm_reg_idx < 0 || reg_is_pinned(i))
                continue;
            if (!defer_spill)
                save_reg(state, i);
            register_map[i].vm_reg_idx = -1;
            register_map[i].alive = false;
            register_map[i].dirty = false;
        }
    }
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, jcc_invert(jcc));
    uint32_t exit_branch = JUMP_LOC_0;
#if defined(__x86_64__)
    if (rotating)
        emit_alu32_imm8(state, SHIFT_IMM_OPCODE, SHIFT_ROL,
                        ra_load(state, rotate.mask), 1);
#endif
    /* Limit the local loop so halt and reclamation cannot be starved.
     * RSI/R1 is outside the allocator, and a misalignment stub, the only
     * code that writes it, never returns to the loop. The decrement itself
     * sets the zero flag for the backedge.
     */
#if defined(__x86_64__)
    emit_alu32_imm8(state, ALU_GRP1_IMM8_OPCODE, ALU_ADD, parameter_reg[1], -1);
#elif defined(__aarch64__)
    emit_addsub_imm(state, false, AS_SUBS, parameter_reg[1], parameter_reg[1],
                    1);
#endif
    jump_loc_0 = state->offset;
    emit_jcc_offset(state, JCC_JNE);
    emit_jump_target_offset(state, JUMP_LOC_0, loop_offset);

    struct host_reg exit_map[ARRAY_SIZE(register_map)];
    if (defer_spill)
        memcpy(register_map, iter_map, sizeof(register_map));
    memcpy(exit_map, register_map, sizeof(exit_map));
#if defined(__x86_64__)
    /* The budget exit follows rotation to the next iteration's mask. */
    if (rotating)
        emit_alu32_imm8(state, SHIFT_IMM_OPCODE, SHIFT_ROR,
                        ra_load(state, rotate.mask), 1);
#endif
    store_back(state);
    emit_set_pc_exit(state, block->pc_start);

    emit_jump_target_offset(state, exit_branch, state->offset);
    memcpy(register_map, exit_map, sizeof(register_map));
    store_back(state);
    if (JIT_LINK_ALL_EDGES || tail->branch_untaken)
        emit_jmp(state, exit_pc, rv->csr_satp);
    emit_set_pc_exit(state, exit_pc);
    if (guards)
        emit_misalign_stubs(state);
#if defined(__x86_64__)
    if (rotating && !should_flush) {
        emit_jump_target_offset(state, rotate_guard, state->offset);
        if (!translate_register_loop(state, rv, block, false))
            __UNREACHABLE;
    }
#endif
    return true;
}
#endif

/* Tier-2 promotion counts dispatcher entries. Cloning would absorb those
 * entries before promotion, so keep its existing block boundaries intact.
 */
#define JIT_REGISTER_REGION (JIT_REGISTER_LOOP && !RV32_HAS(T2C))

#if JIT_REGISTER_REGION
/* External entries retain the ordinary memory-backed register ABI. Inside a
 * bounded tree, both outcomes can reuse the current map. Every leaf spills;
 * fault snapshots still describe the state at the individual instruction.
 */
#define REGION_MAX_BLOCKS 16
#define REGION_MAX_RECORDS 128
#define REGION_MAX_INSTRUCTIONS 256
#define REGION_MAX_DEPTH 6

struct region_flow {
    struct tail_branch br; /* jcc is JCC_JMP for an unconditional tail */
    uint8_t link;
    uint32_t return_pc;
};

struct region_node {
    block_t *block;
    struct region_flow flow;
    int child[2]; /* taken/unconditional, untaken */
    uint32_t live_offset;
    uint8_t known_link;
    uint32_t known_return;
};

struct region_plan {
    struct region_node node[REGION_MAX_BLOCKS];
    uint32_t records, instructions, jumps;
    uint32_t live[REGION_MAX_RECORDS];
    int count;
};

static bool region_flow(const block_t *block, struct region_flow *flow)
{
    const rv_insn_t *tail = block->ir_tail;
    if (tail_branch(tail, &flow->br))
        return true;
    flow->br.jcc = JCC_JMP;
    switch (tail->opcode) {
    case rv_insn_jal:
        flow->link = tail->rd;
        flow->return_pc = tail->pc + 4;
        return flow->br.target != block->pc_start;
#if RV32_HAS(EXT_C)
    case rv_insn_cj:
        return flow->br.target != block->pc_start;
    case rv_insn_cjal:
        flow->link = rv_reg_ra;
        flow->return_pc = tail->pc + 2;
        return flow->br.target != block->pc_start;
#endif
    default:
        return false;
    }
}

static bool region_returns(const rv_insn_t *tail, uint8_t link)
{
    if (!link)
        return false;
    if (tail->opcode == rv_insn_jalr)
        return !tail->rd && tail->rs1 == link && !tail->imm;
#if RV32_HAS(EXT_C)
    if (tail->opcode == rv_insn_cjr)
        return tail->rs1 == link;
#endif
    return false;
}

static bool region_body(const block_t *block, uint8_t *known_link)
{
    uint32_t use, def;
    for (const rv_insn_t *ir = block->ir_head; ir != block->ir_tail;
         ir = block_next_ir(ir)) {
        if (!loop_body_regs(ir, &use, &def))
            return false;
        if (*known_link && (def & LOOP_REG(*known_link)))
            *known_link = 0;
    }
    /* An ordinary indirect exit owns one live-patch site per standalone
     * block. Do not duplicate such sites within a region. Known returns use
     * direct edges, so several leaves can specialize them independently.
     */
    switch (block->ir_tail->opcode) {
    case rv_insn_jalr:
#if RV32_HAS(EXT_C)
    case rv_insn_cjr:
    case rv_insn_cjalr:
#endif
        return region_returns(block->ir_tail, *known_link);
    default:
        return true;
    }
}

static block_t *region_target(riscv_t *rv, uint32_t pc)
{
    block_t *block = cache_get(rv->block_cache, pc, false);
    if (!block || !block->translatable || !block_matches_context(rv, block))
        return NULL;
#if RV32_HAS(BLOCK_CHAINING)
    if (block->page_terminated)
        return NULL;
#endif
    return block;
}

static void plan_region(struct jit_state *state,
                        riscv_t *rv,
                        struct region_plan *plan,
                        int index,
                        int depth)
{
    struct region_node *node = &plan->node[index];
    node->child[0] = node->child[1] = -1;
    if (depth == REGION_MAX_DEPTH || !region_flow(node->block, &node->flow))
        return;
    struct region_flow *flow = &node->flow;
    uint8_t link = flow->link ? flow->link : node->known_link;
    uint32_t return_pc = flow->link ? flow->return_pc : node->known_return;
    if (flow->br.add_before_branch && node->block->ir_tail->rd == link)
        link = 0;
    block_t *target[2] = {
        region_target(rv, flow->br.target),
        flow->br.jcc == JCC_JMP ? NULL : region_target(rv, flow->br.other_pc),
    };
    /* Spend the bounded budget on shorter paths first, leaving space to
     * follow several small blocks around a loop before copying cold work.
     */
    int first =
        target[1] && (!target[0] || target[1]->n_insn < target[0]->n_insn);
    for (int edge = 0; edge < 2; edge++) {
        int side = first ^ edge;
        block_t *block = target[side];
        if (!block || plan->count == REGION_MAX_BLOCKS ||
            plan->records + block->n_insn > REGION_MAX_RECORDS ||
            plan->instructions + block->cycle_cost > REGION_MAX_INSTRUCTIONS)
            continue;
        uint32_t budget = block_jump_budget(block);
        if (state->n_jumps + plan->jumps + budget >= MAX_JUMPS)
            continue;
        uint8_t child_link = return_pc ? link : 0;
        if (!region_body(block, &child_link))
            continue;
        int child = plan->count++;
        node->child[side] = child;
        uint32_t live_offset = plan->records;
        plan->records += block->n_insn;
        plan->instructions += block->cycle_cost;
        plan->jumps += budget;
        plan->node[child] = (struct region_node) {
            .block = block,
            .live_offset = live_offset,
            .known_link = child_link,
            .known_return = return_pc,
        };
        plan_region(state, rv, plan, child, depth + 1);
    }
}

/* A guard observes all registers before its instruction, and so does a
 * helper: LUI+LW/SW (fuse9/fuse10) may call one. Byte accesses never carry a
 * guard; treating them as barriers too only keeps a few stores.
 */
static bool region_memory(const rv_insn_t *ir)
{
    if (guarded_accesses(ir))
        return true;
    switch (ir->opcode) {
    case rv_insn_lb:
    case rv_insn_lbu:
    case rv_insn_sb:
    case rv_insn_fuse9:
    case rv_insn_fuse10:
        return true;
    default:
        return false;
    }
}

static uint32_t region_liveness(struct region_plan *plan,
                                int index,
                                bool guards)
{
    struct region_node *node = &plan->node[index];
    block_t *block = node->block;
    uint32_t live = UINT32_MAX;
    if (node->child[0] >= 0 || node->child[1] >= 0) {
        uint32_t taken = node->child[0] < 0
                             ? UINT32_MAX
                             : region_liveness(plan, node->child[0], guards);
        if (node->flow.br.jcc == JCC_JMP) {
            live = taken & ~LOOP_REG(node->flow.link);
        } else {
            uint32_t untaken =
                node->child[1] < 0
                    ? UINT32_MAX
                    : region_liveness(plan, node->child[1], guards);
            live = taken | untaken;
            if (node->flow.br.add_before_branch) {
                live &= ~LOOP_REG(block->ir_tail->rd);
                live |= LOOP_REG(block->ir_tail->rs1);
            } else {
                live |=
                    LOOP_REG(node->flow.br.lhs) | LOOP_REG(node->flow.br.rhs);
            }
        }
    }
    /* Leaves expose the entire architectural register file, even when their
     * target is compiled. Never rely on execution beyond this region.
     */
    const rv_insn_t *record[REGION_MAX_RECORDS];
    uint32_t count = 0;
    for (const rv_insn_t *ir = block->ir_head; ir != block->ir_tail;
         ir = block_next_ir(ir))
        record[count++] = ir;
    assert(count + 1 == block->n_insn);
    plan->live[node->live_offset + count] = live;
    while (count) {
        const rv_insn_t *ir = record[--count];
        uint32_t use, def;
        if ((guards && region_memory(ir)) || !loop_body_regs(ir, &use, &def)) {
            live = UINT32_MAX;
            plan->live[node->live_offset + count] = live;
            continue;
        }
        live = (live & ~def) | use;
        /* Fused emitters may allocate more than once while defining their
         * intermediate results. Keep all of those definitions live through
         * the record, even when its final outputs will be overwritten.
         */
        plan->live[node->live_offset + count] = live | def;
    }
    return live;
}

static void emit_region_edge(struct jit_state *state, riscv_t *rv, uint32_t pc)
{
    store_back_all(state);
    emit_jmp(state, pc, rv->csr_satp);
    emit_set_pc_exit(state, pc);
}

static void translate_region(struct jit_state *state,
                             riscv_t *rv,
                             const struct region_plan *plan,
                             int index)
{
    const struct region_node *node = &plan->node[index];
    block_t *block = node->block;
    rv_insn_t *tail = block->ir_tail, *previous = NULL;
    cur_block = block;
    if (!rv->jit_entry_cycles)
        emit_cycle_count(state, block->cycle_cost);
    liveness_reset();
    liveness_calc(block);
    uint32_t idx = 0;
    for (rv_insn_t *ir = block->ir_head; ir != tail && !should_flush;
         previous = ir, ir = block_next_ir(ir), idx++) {
        regs_refresh(idx);
        spill_live_regs = plan->live[node->live_offset + idx];
        translate_record(state, rv, ir, previous);
    }
    if (should_flush)
        return;
    regs_refresh(idx);
    spill_live_regs = plan->live[node->live_offset + idx];
    if (node->known_return && region_returns(tail, node->known_link)) {
        emit_region_edge(state, rv, node->known_return);
        return;
    }
    if (node->child[0] < 0 && node->child[1] < 0) {
        translate_record(state, rv, tail, previous);
        return;
    }
    struct region_flow flow = node->flow;
    if (flow.link)
        emit_load_imm(state, map_vm_reg(state, flow.link), flow.return_pc);
    if (flow.br.jcc == JCC_JMP) {
        translate_region(state, rv, plan, node->child[0]);
        return;
    }
    branch_zero_rhs(&flow.br.lhs, &flow.br.rhs, &flow.br.jcc);
    emit_tail_compare(state, tail, flow.br.add_before_branch, flow.br.lhs,
                      flow.br.rhs);
    uint32_t jump_loc_0 = state->offset;
    emit_jcc_offset(state, flow.br.jcc);
    if (should_flush)
        return;
    struct host_reg taken_map[ARRAY_SIZE(register_map)];
    uint8_t taken_align[ARRAY_SIZE(reg_align)];
    memcpy(taken_map, register_map, sizeof(taken_map));
    memcpy(taken_align, reg_align, sizeof(taken_align));
    if (node->child[1] >= 0)
        translate_region(state, rv, plan, node->child[1]);
    else
        emit_region_edge(state, rv, flow.br.other_pc);
    if (should_flush)
        return;
    emit_jump_target_offset(state, JUMP_LOC_0, state->offset);
    memcpy(register_map, taken_map, sizeof(register_map));
    memcpy(reg_align, taken_align, sizeof(reg_align));
    if (node->child[0] >= 0)
        translate_region(state, rv, plan, node->child[0]);
    else
        emit_region_edge(state, rv, flow.br.target);
}
#endif

#if JIT_REGISTER_LOOP
#undef LOOP_REG
#endif

/* Successor translation recurses. Keep this frame, including its bounded
 * region plan, out of that recursion even when LTO would otherwise inline it.
 */
static void __attribute__((noinline)) translate(struct jit_state *state,
                                                riscv_t *rv,
                                                block_t *block)
{
    uint32_t idx;
    rv_insn_t *ir, *next, *previous = NULL;
    n_misalign_stubs = 0;
    cur_block = block;
#if JIT_REGISTER_LOOP
    if (translate_register_loop(state, rv, block, true))
        return;
#endif
    reset_reg();
#if JIT_REGISTER_REGION
    struct region_plan plan = {
        .node[0] = {.block = block},
        .records = block->n_insn,
        .instructions = block->cycle_cost,
        .jumps = block_jump_budget(block),
        .count = 1,
    };
    plan_region(state, rv, &plan, 0, 0);
    if (plan.count > 1) {
        region_liveness(&plan, 0, !rv->jit_elide_align_checks);
        translate_region(state, rv, &plan, 0);
        spill_live_regs = UINT32_MAX;
        if (!should_flush)
            emit_misalign_stubs(state);
        return;
    }
#endif
    if (!rv->jit_entry_cycles)
        emit_cycle_count(state, block->cycle_cost);
    liveness_reset();
    liveness_calc(block);
    for (idx = 0, ir = block->ir_head; idx < block->n_insn && !should_flush;
         idx++, previous = ir, ir = next) {
        next = block_next_ir(ir);
        regs_refresh(idx);
        translate_record(state, rv, ir, previous);
    }

#if RV32_HAS(BLOCK_CHAINING)
    /* Page-terminated block fallthrough: emit jump to next block or exit.
     * Unlike branch-terminated blocks, page-terminated blocks always fall
     * through to the next sequential address (pc_end).
     */
    if (block->page_terminated && !should_flush) {
        store_back(state);
        if (JIT_LINK_ALL_EDGES || block->ir_tail->branch_taken) {
            /* Fallthrough chain established - jump to next block */
            emit_jmp(state, block->pc_end, rv->csr_satp);
        }
        /* Store PC and exit for un-chained path */
        emit_load_imm(state, temp_reg, block->pc_end);
        emit_store(state, S32, temp_reg, parameter_reg[0],
                   offsetof(riscv_t, PC));
        emit_exit(state);
    }
#endif
    if (!should_flush)
        emit_misalign_stubs(state);
}

#if JIT_LINK_ALL_EDGES
/* Patch the pending jumps to every block translated from @first_block on. */
static void patch_pending_jumps(struct jit_state *state, int first_block)
{
    for (int b = first_block; b < state->n_blocks; b++) {
        const struct offset_map *block = &state->offset_map[b];
        int32_t *link = &state->pending_index[offset_hash(block->pc, 0)];
        while (*link >= 0) {
            struct pending_jump *jump = &state->pending[*link];
            if (jump->target_pc != block->pc) {
                link = &jump->next;
                continue;
            }
            patch_jump(state, jump->offset_loc, block->offset);
#if defined(__aarch64__)
            sys_icache_invalidate(state->buf + jump->offset_loc,
                                  sizeof(uint32_t));
#endif
            *link = jump->next;
        }
    }
}
#endif

static void resolve_jumps(struct jit_state *state)
{
    if (state->n_jumps == 0)
        return;

#if defined(__APPLE__) && defined(__aarch64__)
    /* Write mode is maintained by jit_translate during translation. */
#endif

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
            const struct offset_map *target =
                offset_map_find(state, jump.target_pc,
                                IIF(RV32_HAS(SYSTEM))(jump.target_satp, 0));
            if (target) {
                target_loc = target->offset;
            }
#if JIT_LINK_ALL_EDGES
            else if (state->n_pending < MAX_PENDING_JUMPS) {
                /* Fall through to the exit until the target is compiled. */
                int32_t *head =
                    &state->pending_index[offset_hash(jump.target_pc, 0)];
                state->pending[state->n_pending] = (struct pending_jump) {
                    .offset_loc = jump.offset_loc,
                    .target_pc = jump.target_pc,
                    .next = *head,
                };
                *head = state->n_pending++;
            }
#endif
        }
        patch_jump(state, jump.offset_loc, target_loc);
    }
}

static void translate_chained_block(struct jit_state *state,
                                    riscv_t *rv,
                                    block_t *block);

/* Follow one successor edge into the same translation unit. The recursion
 * guard lives in translate_chained_block, which keys it on RV_HASH_KEY; a
 * caller-side test on the bare program counter would disagree with it in
 * system mode, where the key also carries satp.
 */
static void translate_successor(struct jit_state *state,
                                riscv_t *rv,
                                uint32_t pc)
{
    block_t *block = cache_get(rv->block_cache, pc, false);
    if (!block || !block->translatable)
        return;
    if (!block_matches_context(rv, block))
        return;
    translate_chained_block(state, rv, block);
}

static void translate_chained_block(struct jit_state *state,
                                    riscv_t *rv,
                                    block_t *block)
{
    if (offset_map_find(state, block->pc_start, BLOCK_SATP(block)))
        return;

    /* A full offset map, retired entries included, is a full cache: flush it,
     * or no block could ever be translated again.
     */
    if (state->n_blocks == MAX_BLOCKS) {
        should_flush = true;
        return;
    }

    /* Check whether the remaining jump slots can accommodate this block. */
    if (state->n_jumps + block_jump_budget(block) >= MAX_JUMPS)
        return;

    offset_map_insert(state, block);
    translate(state, rv, block);
    if (unlikely(should_flush))
        return;
    rv_insn_t *ir = block->ir_tail;
    if (ir->branch_untaken)
        translate_successor(state, rv, ir->branch_untaken->pc);
    if (ir->branch_taken)
        translate_successor(state, rv, ir->branch_taken->pc);

    branch_history_table_t *bt = ir->branch_table;
    if (bt) {
        int targets[IN_JUMP_TARGETS];
        int count = bht_select_targets(bt, rv, targets);
        for (int i = 0; i < count; i++)
            translate_successor(state, rv, bt->PC[targets[i]]);
    }
}

bool jit_translate(riscv_t *rv, block_t *block)
{
    struct jit_state *state = rv->jit_state;
    const struct offset_map *translated =
        offset_map_find(state, block->pc_start, BLOCK_SATP(block));
    if (translated) {
        /* Block already translated - skip */
        block->offset = translated->offset;
        block->hot = true;
        return true;
    }
restart:
    /* Only clear the portion that was used in the previous translation.
     * MAX_JUMPS is sized for the worst-case SYSTEM_MMIO fast path (1 MiB
     * with the bumped cap), so a blanket memset here turned out to be the
     * dominant per-translation cost on small-block workloads like
     * Dhrystone / CoreMark.  state->n_jumps still tracks the live range
     * and resolve_jumps() only reads [0, n_jumps), so anything past the
     * previous high-water mark stays untouched.
     */
    if (state->n_jumps)
        memset(state->jumps, 0, state->n_jumps * sizeof(struct jump));
    state->n_jumps = 0;
    block->offset = state->offset;
#if JIT_LINK_ALL_EDGES
    const int first_block = state->n_blocks;
#endif
#if defined(__APPLE__) && defined(__aarch64__)
    /* Enter write mode for the entire translation phase.
     * This batches all write protection toggling into a single operation,
     * avoiding potential cache coherency issues from rapid toggling.
     */
    jit_enter_write_mode();
#endif
    translate_chained_block(state, rv, block);
    if (unlikely(should_flush)) {
#if defined(__APPLE__) && defined(__aarch64__)
        jit_exit_write_mode();
#endif
#if RV32_HAS(T2C)
        /* The flush empties the T2C caches, which the T2C thread writes under
         * cache_lock; a seqlock only protects readers.
         */
        hart_lock(rv, &rv->cache_lock);
#endif
        code_cache_flush(state, rv);
#if RV32_HAS(T2C)
        hart_unlock(rv, &rv->cache_lock);
#endif
        goto restart;
    }

    /* If the root block was not translated (e.g. jump budget exhausted on
     * the very first call to translate_chained_block), bail out without
     * marking the block hot.  Otherwise resolve_jumps and cache maintenance
     * would operate on an empty / stale code region.
     */
    if (!offset_map_find(state, block->pc_start, BLOCK_SATP(block))) {
#if defined(__APPLE__) && defined(__aarch64__)
        jit_exit_write_mode();
#endif
        return false;
    }

    resolve_jumps(state);
#if JIT_LINK_ALL_EDGES
    patch_pending_jumps(state, first_block);
#endif
#if defined(__aarch64__)
    /* Cache maintenance after patching branch immediates.
     * On Apple: sys_icache_invalidate performs DC CVAU + DSB + IC IVAU + DSB +
     * ISB. On Linux: __builtin___clear_cache performs similar cache
     * maintenance.
     */
#if defined(__APPLE__)
    __asm__ volatile("dmb ish" ::: "memory");
#endif
    sys_icache_invalidate(state->buf + block->offset,
                          state->offset - block->offset);
#if defined(__APPLE__)
    /* Exit write mode - page becomes executable. */
    jit_exit_write_mode();
#endif
    /* Full barrier sequence to ensure instruction coherency. */
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
#endif
    block->hot = true;
    return true;
}

struct jit_state *jit_state_init(size_t size, uintptr_t mem_base)
{
    struct jit_state *state = malloc(sizeof(struct jit_state));
    if (!state)
        return NULL;
    assert(state);

#if defined(__x86_64__)
    state->has_bmi2 = false;
#if (defined(__GNUC__) || defined(__clang__)) && !defined(JIT_DISABLE_BMI2)
    unsigned eax, ebx, ecx, edx;
    state->has_bmi2 =
        __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) && (ebx & bit_BMI2);
#endif
#endif

    state->offset = 0;
    state->n_jumps = 0;
    state->size = size;
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
    assert(state->buf != MAP_FAILED);

    offset_map_reset(state);
    reset_reg();
    prepare_translate(state, mem_base);
#if defined(__APPLE__) && defined(__aarch64__)
    /* Final cache flush for prologue/epilogue code.
     * emit_bytes handles per-instruction cache maintenance, but a final
     * flush ensures the entire region is coherent.
     */
    __builtin___clear_cache((char *) state->buf,
                            (char *) (state->buf + state->offset));
#endif

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

#if JIT_LINK_ALL_EDGES
    state->pending = calloc(MAX_PENDING_JUMPS, sizeof(struct pending_jump));
#if JIT_INDIRECT_LOOKUP
    state->sites = calloc(MAX_INDIRECT_SITES, sizeof(struct indirect_site));
#else
    state->sites = NULL;
#endif
    if (!state->pending || (JIT_INDIRECT_LOOKUP && !state->sites)) {
        free(state->sites);
        free(state->pending);
        free(state->jumps);
        free(state->offset_map);
        munmap(state->buf, state->size);
        free(state);
        return NULL;
    }
#endif

    return state;
}

void jit_state_exit(struct jit_state *state)
{
    munmap(state->buf, state->size);
    free(state->offset_map);
    free(state->jumps);
#if JIT_LINK_ALL_EDGES
    free(state->pending);
    free(state->sites);
#endif
    free(state);
}
