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

The scalar suite has 474 execution cases and 30 separate decoder checks
for nonzero-source writes to missing `0xfff` and the read-only
`cycle`/`instret` shadows.
Decoder rejection alone does not establish runtime trap delivery.

## Mandatory CSRs

Twenty-three of those cases cover machine CSRs that read as zero before
missing lookup entries started to trap. `mvendorid`, `marchid`, `mimpid`,
and `mhartid` must read their values without trapping. CSRRW and CSRRWI
with a zero source must trap under both handlers, since those forms always
write. `mie` must update its state, and a write to `mcounteren` must read
back. With `CONFIG_EXT_F=y` (the default), twelve more cases check that `frm`
and `fflags` update only `fcsr[7:5]` and `fcsr[4:0]`, respectively, including
`fflags` masks above bit 4, for 486 execution cases.

Fifteen cases cover `mcycle`/`mcycleh` writes, carry, read-only `cycle`
shadows, and an unaffected internal clock. Thirty-nine more cases cover
`minstret`/`minstreth`: all six forms, carry and 64-bit rollover, high-half
writes, cross-block persistence, read-only `instret` shadows, and U/S-mode
rejection. The instruction sequence checks read-before-increment and write
suppression, including a nonzero source register holding zero. Counter
writes must not change the internal clock or the other counter's offset.
SYSTEM builds add 12 checks that S-mode machine cycle accesses trap, for
498 cases with F. Other CSR privilege checks remain separate work.

`instret` and `minstret` still use the existing cycle-based approximation;
this does not establish exact retirement across exceptions, fused operations,
or native JIT execution. Counter access is not gated by `mcounteren` or
`scounteren`. These tests exercise short interpreted blocks, including in
JIT-enabled builds; they do not qualify native retirement accounting.

## RVV coverage

Enable `CONFIG_EXT_V=y CONFIG_EXT_F=y` to include 12 RVV cases in the normal
suite, for 498 execution cases. These cover `vl`/`vtype`/`vlenb`, CSRRW/CSRRWI
with zero source, and both guest and default handlers. Each access must
trap once at the CSR instruction, preserve `rd`, and stop dispatch before
continuation.

Nonzero-source writes are rejected by the decoder. Decoder rejection alone
does not prove runtime trap delivery. The CSRRS/CSRRC read-only helper paths
are fixed but remain unreachable through those decoder-rejected writes.
