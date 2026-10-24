# Code Generation

## Overview
rv32emu combines an interpreter, tier-1 JIT, and tier-2 AOT compiler.
The interpreter provides baseline execution while the JIT compiler generates native machine code for hot paths,
significantly improving performance for compute-intensive workloads.

The code generation infrastructure supports both x86-64 and Arm64 host architectures through
an abstraction layer that maps common operations to architecture-specific instructions.

## Execution Tiers
The emulator uses three execution tiers:
1. Interpreter: Direct execution of RISC-V instructions via tail-call threaded dispatch
2. Tier-1 JIT: Template-based native code generation for basic blocks
3. Tier-2 AOT: LLVM-based compilation for frequently executed hot paths

When a basic block reaches a configurable execution threshold, it gets promoted from interpreter to Tier-1 JIT.
Observed blocks can be collected as LLVM bitcode, compiled offline, and loaded
on subsequent executions for additional optimization.

## Source File Organization

| File | Purpose |
|------|---------|
| `src/rv32_template.c` | Interpreter instruction implementations using RVOP macro |
| `src/rv32_v_template.c` | Vector (V) extension interpreter handlers (experimental, decode + partial execution) |
| `src/rv32_jit.c` | Tier-1 JIT code generators using GEN macro (included by jit.c) |
| `src/rv32_constopt.c` | IR-level constant folding and optimization |
| `src/rv32_v_constopt.c` | Constant-folding hooks for the V extension |
| `src/jit.c` | Tier-1 JIT infrastructure, emit_* API, and fused instruction handlers |
| `src/aot.c` | Runtime artifact loading, compatibility, and bitcode collection |
| `src/aot_cache.c` | Native binding caches: replacement and invalidation |
| `src/t2c.c` | LLVM bitcode collector (includes t2c_template.c) |
| `src/t2c_template.c` | Tier-2 AOT instruction handlers using T2C_OP macro |
| `src/emulate.c` | Main execution loop, tail-call dispatch, and macro-op fusion |

## Interpreter Implementation
The interpreter uses the `RVOP` macro to define instruction handlers.
Each handler receives the emulator state and decoded instruction, then performs the operation directly in C:
```c
RVOP(name, { body })
```

This expands to a function that:
- Receives: rv (emulator state), ir (decoded instruction), cycle (counter), PC
- Returns: bool indicating whether to continue execution

Example implementation for the addi instruction:
```c
RVOP(addi, { rv->X[ir->rd] = rv->X[ir->rs1] + ir->imm; })
```

The interpreter uses Tail Call Threaded Code (TCTC) for efficient instruction dispatch.
Each handler ends with a tail call (using the `musttail` attribute) to the next instruction's handler,
avoiding function call overhead and enabling better branch prediction compared to switch-based dispatch.

## Tier-1 JIT Code Generation
The Tier-1 JIT compiler uses the GEN macro to define native code generators:
```c
GEN(name, { body })
```

Each generator emits native machine code that performs the equivalent operation using host CPU instructions.
The `emit_*` functions append bytes to the JIT buffer.

Example: The addi instruction generates a host instruction sequence like:
```
mov  VR0, [memory address of (rv->X + rs1)]
mov  VR1, VR0
add  VR1, imm
```

Note: This is conceptual assembly.
Actual instruction generation depends on the dynamic register allocator state and whether source registers are already mapped.

## Register Allocation (Tier-1)
The Tier-1 JIT maintains a register mapping between RISC-V and host registers:

| Register | Purpose |
|----------|---------|
| `vm_reg[0..2]` | Host registers mapped to VM registers for current operation |
| `temp_reg` | Scratch register for intermediate calculations |
| `parameter_reg[0]` | Points to the `riscv_t` structure |

Register allocation is performed dynamically.
When a RISC-V register value is needed,
the allocator either returns an already-mapped host register or loads the value from memory into an available host register.

## Tier-2 AOT Compilation
Tier-2 collects LLVM IR during a training run. `tools/aot.py` applies LLVM
optimization and native code generation offline; `-A` loads the resulting
library without loading LLVM.
Instruction handlers are defined in `src/t2c_template.c` using the `T2C_OP` macro:
```c
T2C_OP(name, { body })
```

Each handler translates the RISC-V instruction semantics into LLVM IR using the LLVM C API.
LLVM then applies its optimization passes and register allocation,
producing native code that typically outperforms Tier-1 for hot paths.

Tier-2 collects user traces through `t2c_trace_ebb()`. In system mode,
it forms shared graphs per virtual code page and content fingerprint. Multiple
entry PCs share the same native function and each decoded block is emitted
once in that graph, so joins and indirect targets do not require overlapping
root traces. Collection materializes graphs before IR reclamation and at
shutdown or reboot checkpoints, after successors have been observed. Entry-set
coverage retains additional variants when a later snapshot contributes a new
entry; offline selection merges their union and drops redundant graphs. Cold
privileged startup code is collected, while user processes must execute a
repeated T1 region before becoming collection roots, avoiding huge profiles
of one-shot ASLR mappings.
Within a region:

- In user traces, guest registers live in local variables that LLVM keeps in
  host registers. `t2c_promote_guest_regs()` fills them from `rv->X` on entry,
  writes back the ones the region changes before any call or return, and
  refills them after a call that comes back, since callees and the dispatcher
  read and write `rv->X`. A function that reaches `rv->X` any other way is
  left unpromoted. System page graphs keep guest registers in `rv->X`: they
  have many exits, and writing promoted registers back at each one doubled the
  Linux artifact.
- In user traces, an indirect jump, such as a function return, whose branch history shows one
  target at least 90% of the time is compared against that target, and the
  region continues there. A return without such a target branches to a
  switch over the return points of every call the region traced, so a callee
  shared by several call sites returns into the region rather than leaving
  it on every call: qsort, whose comparator returns to several sites, runs
  twice as fast. Together these let a loop that calls and returns through
  several functions compile as one loop.
- User traces follow the direct edges the interpreter observed, and also
  edges into blocks the region already holds or T1C compiled as hot, which
  T1C ran natively so the interpreter never linked them. Other unobserved
  code stays out: pulling it in enlarged regions and hindered their register
  promotion.
- System graphs rebuild direct targets from instruction PCs, independently of
  interpreter links that eviction removes. A shared local switch handles
  observed indirect targets and statically known call-return addresses in the
  same verified page. Its cases pass through budget gates. Unobserved targets and cross-page jumps use the native
  cache or dispatcher. Limiting local cases avoids artificial CFG edges and
  excessive SSA/register pressure.
- After `T2C_REGION_BUDGET` traced instructions, successors are no longer
  traced; the region jumps to their compiled code instead.

An indirect jump that leaves the region calls the target's compiled function
as a guaranteed tail call when it finds it in the jit-cache, and otherwise
returns to the dispatcher. `jit_cache_slot()` in `src/jit.h` defines the slot
for both the C code that fills the cache and the IR that probes it.

Tier-2 collection uses the LLVM C API and supports LLVM 19–23 on Linux
hosts. Each collected region owns an LLVM context, so types, builders, and
metadata do not use the deprecated global context. The Makefile prefers the
newest supported `llvm-config`; override with
`make LLVM_CONFIG=/path/to/llvm-config`. Use the matching installation with
`tools/aot.py --llvm-config`. Enable user collection with `jit_defconfig`, or
Linux system collection with `system_jit_defconfig`. See [build.md](build.md)
for collection, deployment, compatibility, and test commands.

System region symbols include a code-page fingerprint. FENCE.I and
code-affecting SFENCE.VMA invalidation discard bindings; the dispatcher binds
only matching page contents in the current address space. FNV is only a fast
candidate key; a complete SHA-256 digest must also match before binding. A
4,096-slot physical-page fingerprint cache avoids hashing the same page again
for each binding. SFENCE.VMA discards bindings while retaining physical hashes;
FENCE.I marks physical hashes stale. When an artifact is loaded, an optional
16 MiB page-snapshot cache permits lazy, exact byte comparisons before reusing
an unchanged page's SHA-256 digest. Changed pages are hashed again; allocation
failure falls back to recomputing hashes. T1 code-cache recycling retains
native bindings. Bindings follow the code retirement of the other tiers: a
pinned binding records its page's mapping as decoded code does, under the same
address space key: the current SATP, or SATP_SHARED for kernel code reached
only through global mappings, which is bound once for every process. Kernel
graphs look up targets under SATP_SHARED while S-mode paging is on and the
guest has bound kernel code there, and under the current SATP otherwise; the
dispatcher probes both keys. When a SFENCE.VMA finds
that the mapping of a recorded code page changed, the bindings of that address
space are cleared and rebound on their next entry. FENCE.I clears them all.
The offline compiler also emits an entry index and
the emulator a bitmap of the virtual pages it covers, which answers most
misses without probing the index. The dispatcher validates instruction-fetch
permission before publishing a new native binding, which records the fetch
privilege as T2C entries do. A missing entry uses T1 or interpreter fallback. Native entries
survive decoded IR eviction because their lifetime belongs to verified mappings
and the loaded library; fences still invalidate them.
Cross-page tail calls resolve through the runtime cache. User regions on
x86-64 use the preserve_none calling convention, so these tail calls save and
restore no registers; the collector records the convention in its manifest,
and tools/aot.py then adds a trampoline through which the emulator enters
them. System regions keep the C
convention: they call C helpers on every slow path, around which preserve_none
code must save its live values. Native memory accesses
require a valid dTLB RAM hit or a range-checked bare-mode RAM address. Slow
accesses, deadline yields and other exits are marked unlikely, so LLVM lays
them out after the hot path of a page graph. Slow accesses publish the
precise prefix through the memory instruction before invoking MMU/MMIO
helpers. Successful helpers resume native execution after restoring the
unexecuted suffix count. Monotonic trap and code epochs prevent continuation
through a trap, reboot, or code invalidation, even when nested traps restore
the original depth. Completed MMIO accesses are never retried.
The collector retains original instruction starts across fusion so helper calls
and trap exits publish precisely the retired prefix. Pure reads of CYCLE,
INSTRET, and TIME (including their high halves) can also execute in system AOT
regions. Their values use that exact instruction prefix; TIME includes the VM
timer offset. T1 eligibility stays separate, so its unsupported CSR handlers are
never used. Atomic instructions, LR/SC, and SSTATUS operations can use an
outlined semantic helper within a native page graph. This retains existing
reservation and fault behavior; SSTATUS writes yield for interrupt polling.
These operations are outlined calls, not LLVM atomic instructions.
Misaligned traps use a one-shot fallback that prevents repeatedly
entering the same AOT guard. System regions check the dispatcher's chain
deadline at entry and on edges to previously visited blocks, so timer
interrupts arrive on time. Every cycle crosses such an edge;
acyclic paths stay within one code page. Native execution therefore yields
without adding a budget branch to each instruction in a linear trap path.

Trap handlers run through the dispatcher, so their code is collected and
replayed like any other. Runtime policy is explicit and shared by every tier:
`-B` selects the block cache capacity and `-s` the dispatch instruction budget.
Loading an artifact does not silently change these settings. Native
diagnostics report charged instructions, native function entries including
tail entries, successful slow-path continuations, hash activity and bindings.
The offline object cache uses stable hash buckets so adding one page does not
reshuffle every later compilation unit.
LLVM alias metadata distinguishes VM state from bounds-checked guest RAM,
while guest RAM accesses retain their mutual aliasing.

## IR Optimization
Before execution or JIT compilation,
the emulator performs optimization passes on the internal instruction representation (IR).
Defined in `src/rv32_constopt.c`,
these passes analyze basic blocks to identify opportunities for constant propagation and folding.
For example, a `lui` followed by `addi` to construct a 32-bit constant can be optimized to reduce runtime computation.
This optimization benefits both the interpreter and JIT tiers.

## Code Generation Macros (Tier-1)
Helper macros reduce code duplication for common Tier-1 instruction patterns:

| Macro | Instructions |
|-------|--------------|
| `GEN_BRANCH` | beq, bne, blt, bge, bltu, bgeu |
| `GEN_CBRANCH` | cbeqz, cbnez (compressed) |
| `GEN_ALU_IMM` | addi, xori, ori, andi |
| `GEN_ALU_REG` | add, sub, xor, or, and |
| `GEN_SHIFT_IMM` | slli, srli, srai |
| `GEN_SHIFT_REG` | sll, srl, sra |
| `GEN_SLT_IMM` | slti, sltiu |
| `GEN_SLT_REG` | slt, sltu |
| `GEN_LOAD` | lb, lh, lw, lbu, lhu |
| `GEN_STORE` | sb, sh, sw |

Each macro encapsulates the common code generation pattern for its instruction class,
taking only the instruction-specific parameters (opcode, condition code, etc.).

## Dual-Architecture Support
The JIT supports both x86-64 and Arm64 through an abstraction layer in jit.c.
Constants use x86-64 encoding values but serve as symbolic identifiers on both architectures.
The `emit_*` functions contain architecture-specific implementations guarded by preprocessor conditionals:
```c
#if defined(__x86_64__)
    // x86-64 specific code generation
#elif defined(__aarch64__)
    // Arm64 specific code generation
#endif
```

For example, jump condition codes (`JCC_JE`, `JCC_JNE`, etc.) match x86-64 Jcc opcodes
but are mapped to equivalent Arm64 condition codes by `emit_jcc_offset()`.

## Emit API (Tier-1)
The `emit_*` functions provide the low-level interface for Tier-1 machine code generation:

| Function | Description |
|----------|-------------|
| `emit_alu32_imm32` | ALU operation with 32-bit immediate |
| `emit_alu32_imm8` | ALU operation with 8-bit immediate |
| `emit_alu32` | ALU operation between registers |
| `emit_load_imm` | Load immediate value into register |
| `emit_load` / `emit_store` | Memory access operations |
| `emit_mov` | Register-to-register move |
| `emit_cmp32` / `emit_cmp_imm32` | Comparison operations |
| `emit_jcc_offset` | Conditional jump |
| `emit_jmp` | Unconditional jump |
| `emit_call` | Function call to runtime helper |

## Block Chaining
Translated blocks can be chained together to avoid returning to the dispatcher between consecutive blocks.
When a block ends with a direct branch to another translated block,
the JIT patches the branch target to jump directly to the target block's native code.

Block chaining is controlled by the `ENABLE_BLOCK_CHAINING` configuration option.

## Macro-op Fusion
The emulator fuses common instruction sequences into single operations,
benefiting both the interpreter and JIT tiers. Fusion is implemented in `src/emulate.c`
via `match_pattern` and `do_fuse*` handlers, which transform the IR before execution.

For example, a lui followed by addi to construct a 32-bit constant can be fused into a single load-immediate operation.
The fused instructions are then handled by dedicated handlers in each tier:
- Interpreter: `do_fuse*` functions in `src/emulate.c`
- Tier-1 JIT: `do_fuse*` functions in `src/jit.c`
- Tier-2 AOT: `T2C_OP(fuse*, ...)` handlers in `src/t2c_template.c`

Macro-op fusion is controlled by the `ENABLE_MOP_FUSION` configuration option.

## Memory Access and MMIO
Load and store instructions check for memory-mapped I/O regions when system emulation is enabled.
The `GEN_LOAD` and `GEN_STORE` macros include conditional code generation for MMIO handling:
```c
IIF(RV32_HAS(SYSTEM_MMIO))(
    // MMIO check and handler call
,
    // Direct memory access
)
```

When MMIO is not enabled, the generated code performs direct memory access
without the overhead of region checking.

## Misaligned Accesses
Both JIT tiers keep the interpreter's semantics for halfword and word
accesses that are not naturally aligned. Unless the emulator runs with `-m`,
such an access raises an address-misaligned exception: a guest trap handler
sees it exactly as under the interpreter, and without one the user-mode
default handler emulates the access and resumes after it. In system mode this
also keeps a word that straddles a page boundary from being read through the
translation of its first byte.

Tier-1 emits a test of the address and a branch, not taken on the aligned
path, to a stub placed after the block that writes the guest registers back
and calls `jit_misaligned_trap()`. When the offset is itself aligned, the base
register is tested directly. A passed check also proves the base aligned, so
later accesses in the same block through the unmodified base register need no
check; any write to that register discards the fact. Tier-2 emits the
equivalent branch in LLVM IR, weighted as unlikely and followed by an
`llvm.assume` of the alignment, which lets LLVM drop later checks it implies.

A trap taken without a handler still leaves state behind: the cause, value and
exception PC registers, `mstatus`, and the privilege level that a later `mret`
returns to. A user-mode program that runs no privileged instruction and
accesses no supervisor or machine CSR can see none of it, nor install a
handler, so it cannot tell the default handler's emulation from the host
performing the access, and neither tier emits the checks for it. When the ELF
is loaded, its executable sections are decoded instruction by instruction for
anything other than `ecall`, `ebreak`, or a user-level CSR access such as
`fcsr`, `cycle` or `jvt`. Code the program generates at run time is not
scanned, just as user-mode JIT code is not invalidated when a program rewrites
its own instructions. On Dhrystone, the checks otherwise add about 18% to
the host instructions of tier-1 code. `tests/jit-misalign.S` covers the plain,
compressed, and fused forms with and without a guest trap vector. Its
`-nocsr` build uses no privileged state, and its `-nohandler` build only reads
the trap CSRs, which must keep the checks in place.

See [AOT profiling](aot-profiling.md) for function tracing, native sampling,
and the runtime costs that guided subsequent optimizations.
