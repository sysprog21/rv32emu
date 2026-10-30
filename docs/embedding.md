# Embedding rv32emu

The native user-mode library runs RV32 ELF programs through the C API in
[`include/rv32emu.h`](../include/rv32emu.h). The header is self-contained and
supports C++ through `extern "C"`. Internal headers in `src/` are not part of
this interface. Recompile clients when updating the library.

Build with your existing native user-mode configuration:

```sh
make -j8 library example-embed
build/embed path/to/program.elf arg1 arg2
make check-api
```

For a new checkout, run `make mini_defconfig` first for a smaller build without
SDL or floating point, or `make defconfig` for the default ISA and JIT support.
System emulation and WebAssembly configurations do not support this library
interface. The command-line emulator continues to build with `make`.

`make library` produces `build/librv32emu.a` and build-tree pkg-config metadata.
Use the same compiler toolchain as the library, especially with LTO enabled:

```sh
cc -c examples/embed.c -o build/my-host.o \
    $(pkg-config --cflags ./build/rv32emu.pc)
cc build/my-host.o -o build/my-host \
    $(pkg-config --libs --static ./build/rv32emu.pc)
```

The metadata includes configuration-specific dependencies such as SoftFloat and
SDL. These dependencies remain separate archives/libraries; copying just
`librv32emu.a` is insufficient. Paths refer to the checkout and should be
regenerated after moving it. `OUT=...` relocates generated build artifacts.

A typical lifecycle is:

```c
#include <rv32emu.h>

rv32emu_t *emu = NULL;
rv32emu_status_t status = rv32emu_create("program.elf", NULL, &emu);
if (status == RV32EMU_OK) {
    while ((status = rv32emu_step(emu, 1000)) == RV32EMU_OK) {
        /* Run other host work between execution slices. */
    }
    int guest_exit_code;
    if (status == RV32EMU_HALTED)
        rv32emu_exit_code(emu, &guest_exit_code);
    rv32emu_destroy(emu);
}
```

The complete executable example is [`examples/embed.c`](../examples/embed.c).
Use `rv32emu_options_init()` to select memory size (a multiple of 4 KiB and
the host page size, from 16 KiB to 4 GiB) and guest arguments. Without
options, the library allocates 64 MiB of guest address space and supplies the
ELF path as `argv[0]`. Input strings are needed only during creation. ELF and
allocation failures return `RV32EMU_CREATE_FAILED`, with the output handle set
to NULL, so the host may retry. Diagnostics go to the existing logger, whose
level creation sets to errors only for the whole process.

Read/write integer registers by architectural number and access the PC using
the dedicated functions. Memory copies check the complete range before touching
it. Memory writes are accepted only before the first execution slice; this
prevents modifying code that has already been translated. Destroy and recreate
the instance to load another program. Step budgets are approximate instruction
counts and can overshoot at block boundaries. Guest I/O may block; a slice is
not a wall-clock deadline. Library instances enable native block deadlines
and disable JIT region/loop inlining so native loops yield to the host; CLI
instances retain their existing optimization policy. A manual halt is terminal
for that instance and reads as exit code 0; an internal fatal error halts with
`EXIT_FAILURE`.

The current backend has process-global memory and dispatcher state. Only one
live instance is allowed; a second creation returns `RV32EMU_BUSY`. Serialize
all calls, including creation and destruction, and do not call the internal
`rv_*` API alongside the public API. The memory backend may install host signal
handlers for demand paging and restores them on destruction. Guest syscalls
can access host files and standard streams; this API does not provide isolation
for untrusted programs. Guest-opened streams are closed when the VM is
destroyed; host standard streams remain open.

Native builds use hidden symbol visibility by default. `RV32EMU_API` marks the
13 public entry points as visible, so embedding the static archive into a host
that exports symbols does not also export core or SoftFloat implementation
symbols. T2C-enabled builds retain five host services required by the optional
collector; the collector itself exports only its compile entry and three ABI
metadata symbols. Static archives still contain global cross-object symbols
needed by the linker; hidden visibility does not isolate their names from other
statically linked libraries.
