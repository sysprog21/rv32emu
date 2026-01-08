# Code Generation

## Overview
rv32emu employs a tiered execution strategy with an interpreter and a two-tier JIT compiler.
The interpreter provides baseline execution while the JIT compiler generates native machine code for hot paths,
significantly improving performance for compute-intensive workloads.

The code generation infrastructure supports both x86-64 and Arm64 host architectures through
an abstraction layer that maps common operations to architecture-specific instructions.

## Execution Tiers
The emulator uses three execution tiers:
1. Interpreter: Direct execution of RISC-V instructions via tail-call threaded dispatch
2. Tier-1 JIT: Template-based native code generation for basic blocks
3. Tier-2 JIT: LLVM-based compilation for frequently executed hot paths

When a basic block reaches a configurable execution threshold, it gets promoted from interpreter to Tier-1 JIT.
Blocks that continue to execute frequently may be further compiled by the Tier-2 LLVM backend for additional optimizations.

## Source File Organization

| File | Purpose |
|------|---------|
| `src/rv32_template.c` | Interpreter instruction implementations using RVOP macro |
| `src/rv32_jit.c` | Tier-1 JIT code generators using GEN macro (included by jit.c) |
| `src/rv32_constopt.c` | IR-level constant folding and optimization |
| `src/jit.c` | Tier-1 JIT infrastructure, emit_* API, and fused instruction handlers |
| `src/t2c.c` | Tier-2 JIT driver (includes t2c_template.c) |
| `src/t2c_template.c` | Tier-2 JIT instruction handlers using T2C_OP macro |
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

## Tier-2 JIT Compilation
The Tier-2 JIT uses LLVM to compile frequently executed blocks into highly optimized native code.
Instruction handlers are defined in `src/t2c_template.c` using the `T2C_OP` macro:
```c
T2C_OP(name, { body })
```

Each handler translates the RISC-V instruction semantics into LLVM IR using the LLVM C API.
LLVM then applies its optimization passes and register allocation,
producing native code that typically outperforms Tier-1 for hot paths.

Tier-2 compilation requires LLVM 18 and is enabled with `ENABLE_JIT=1` at build time.

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
- Tier-2 JIT: `T2C_OP(fuse*, ...)` handlers in `src/t2c_template.c`

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
