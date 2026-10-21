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

The scalar suite has 435 execution cases and six separate decoder checks.
Decoder rejection alone does not establish runtime trap delivery.

## Mandatory CSRs

Twenty-three of those cases cover machine CSRs that read as zero before
missing lookup entries started to trap. `mvendorid`, `marchid`, `mimpid`,
and `mhartid` must read their values without trapping. CSRRW and CSRRWI
with a zero source must trap under both handlers, since those forms always
write. `mie` must update its state, and a write to `mcounteren` must read
back. With `CONFIG_EXT_F=y` (the default), four more cases check that `frm`
reads and writes only `fcsr[7:5]` and keeps `fflags` in `fcsr[4:0]`, for
439 execution cases.

Fifteen cases cover `mcycle`/`mcycleh` writes, carry, read-only `cycle`
shadows, and an unaffected internal clock. `minstret`/`minstreth` remain
unsupported. SYSTEM builds add 12 checks that S-mode machine cycle accesses
trap, for 451 cases with F. Other CSR privilege checks remain separate work.

## RVV coverage

Enable `CONFIG_EXT_V=y CONFIG_EXT_F=y` to include 12 RVV cases in the normal
suite, for 451 execution cases. These cover `vl`/`vtype`/`vlenb`, CSRRW/CSRRWI
with zero source, and both guest and default handlers. Each access must
trap once at the CSR instruction, preserve `rd`, and stop dispatch before
continuation.

Nonzero-source writes are rejected by the decoder. Decoder rejection alone
does not prove runtime trap delivery. The CSRRS/CSRRC read-only helper paths
are fixed but remain unreachable through those decoder-rejected writes.
