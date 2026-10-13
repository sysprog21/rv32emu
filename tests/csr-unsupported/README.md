# Unsupported CSR regression

Run `make run-test-csr-unsupported` from the repository root. The test also
runs in `make check` and `make tests`. It uses the production decoder,
`rv_step()`, and trap callback; no assembler or cross compiler is needed.

## Configurations

Use `CONFIG_JIT=n` for the interpreter, or
`CONFIG_SYSTEM=y CONFIG_ELF_LOADER=y` for S-mode ELF execution. Use a
separate `OUT` directory for each configuration. The harness requires
Zicsr and RV32I; WASM and system MMIO are outside its scope.

## Test layout

Each execution case emits a prefix marker, CSR access, continuation marker,
and EBREAK. The prefix exposes stale fault-PC reporting inside a block.
The trap callback records cause, PC, and `rd` before calling the real
handler. A guest handler records EPC and advances it by four; the default
handler is also tested. Unsupported cases check `rd` at entry and after
exactly one trap/resumption; supported controls require no trap. Fresh PCs
avoid stale translated code.

The scalar suite has 397 execution cases and six separate decoder checks.
Decoder rejection alone does not establish runtime trap delivery.

## Separate RVV probe

RVV read-only trap handling is outside the passing scalar suite. To
reproduce its 12 cases, enable `CONFIG_EXT_V=y CONFIG_EXT_F=y` and insert
the following calls after the zero-valued supported controls and before
`decoder_failures` in a temporary copy of `test-csr-unsupported.c`:

```c
for (unsigned csr = 0xc20; csr <= 0xc22; csr++)
    for (unsigned op = 1; op <= 5; op += 4)
        for (unsigned def = 0; def < 2; def++)
            run_case(cpu, csr, op, 0, 0, 10, false, def, 0xa5);
```

This covers `vl`/`vtype`/`vlenb`, CSRRW/CSRRWI, and both guest and default
handlers. Zero-source writes reach the RVV helper; nonzero-source writes
are rejected earlier by the decoder. The helper traps but returns success
with zero, so `rd` is clobbered and the trap PC may precede the instruction.

An earlier separate local probe reported 409 cases and 12 failures both
before and after the unsupported-CSR fix. This is a historical result,
not a fresh run at the current revision. Re-run the probe for the revision
and configuration under review. Scalar passes in a V-enabled build do not
establish RVV trap coverage; keep these failures separate from passing
scalar results.
