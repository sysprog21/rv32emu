# Build and customization

`rv32emu` uses a [Kconfig](https://github.com/sysprog21/Kconfiglib)-based build
system. This document covers JIT compiler setup and detailed build-time
options.

## Tier-1 JIT and tier-2 AOT compilation

The default configuration uses the tier-1 JIT (T1C) on x86-64 and Arm64 hosts
and the interpreter elsewhere; it needs no LLVM. Tier-2 compilation (T2C)
uses a profile-guided ahead-of-time workflow for ELF programs and Linux
system emulation on Linux hosts.
LLVM never compiles native code during guest execution. Without an explicitly
selected AOT artifact, execution uses T1C and the interpreter.

Install LLVM 19-23 development libraries and matching clang, for example
`llvm-23-dev clang-23` from apt.llvm.org on Ubuntu 24.04, whose own
repositories stop at LLVM 18. LLVM 19, 20, 21, 22, and 23 are tested
separately on Ubuntu 24.04. The Makefile detects `llvm-config`;
use `make LLVM_CONFIG=/path/to/llvm-config` to select an installation.

```shell
$ make jit_defconfig
$ make
$ build/rv32emu -C build/program-profile program.elf [arguments]
$ python3 tools/aot.py build/program-profile -o build/program.so --llvm-config llvm-config-23
$ build/rv32emu -A ./build/program.so program.elf [arguments]
```

`-C` requires a new directory. The guest runs normally, then the collector
lifts surviving translated blocks into relocatable bitcode at shutdown.
User-mode evicted blocks are absent. System collection saves shared page
graphs before IR reclamation and at shutdown, including cold privileged startup
paths and counter reads that T1 cannot compile. User processes must repeatedly
execute a T1 region before becoming collection roots; this avoids collecting
one-shot ASLR mappings. Missing paths use T1 or interpreter fallback.
Representative training inputs improve coverage.
`tools/aot.py` optimizes complete regions in bounded compilation units and
links a shared library at `-O3` by default; `-O 0` through `-O 3` select the
offline level. `-j N` controls parallel compilation (up to eight host CPUs by
default), keeping large Linux profiles practical.
Regions can include up to 8192 IR instructions, exposing larger control-flow
and loop optimization opportunities than the former 2048-instruction JIT
budget. Collection and compilation costs are paid before the deployment run.
This follows rvdbt's separation of observed-code collection, offline LLVM
compilation, artifact identity checks, and runtime loading.

`-A` loads the library once and binds region entries as blocks are encountered.
Missing regions and unobserved branches fall back to T1C or the interpreter.
RAM and helper addresses come from the current VM, so artifacts tolerate host
ASLR. Artifacts require the same emulator executable, guest ELF or kernel bytes, memory
size, and misalignment option. Rebuild and recollect after changing any of
these. The collector and executable also verify a common source/configuration
and C-layout ABI fingerprint before collection starts. The manifest records
the producing LLVM major and the calling convention of the collected regions;
an offline compiler must be LLVM 19–23 and at least as new as that producer. Fingerprints check compatibility; explicitly loaded shared libraries
are native code and must be trusted. A relative library path names a file
from the current directory, never the dynamic loader's search path.

User-mode artifacts require immutable guest code: `FENCE.I` permanently
turns off AOT and makes collection fail. System artifacts identify regions by
virtual PC, a fast hash key, and a complete SHA-256 digest of their code page. Regions stay within that
page, and normal `FENCE.I`/`SFENCE.VMA` invalidation forces fresh translation
and binding; kernel code on global mappings is bound once for every address
space. Missing pages, new
processes, and changed code fall back to T1C/interpreter. Native RAM hits stay
in the region, including bare mode and superpages. Slow accesses publish the
precise retired prefix before calling MMU/MMIO helpers. Successful helpers
continue in native code unless trap or code epochs changed; misaligned traps use a one-shot T1 fallback. Execution
budgets return control for interrupts. Bound entries can bypass IR lookup.

System AOT also lowers pure counter reads, and can outline atomic, LR/SC, and
SSTATUS operations through the existing semantic handlers; other instructions,
WFI included, end the region and run in T1 or the interpreter. Use the same
`-s` dispatch budget and `-B` block-cache capacity for comparisons; artifact
selection does not change these options. Startup speedup does not imply the
same speedup for CPU-bound guest workloads.

LLVM is linked only into `libt2c.so`, which `-C` loads from the emulator's
directory. A deployment needs only the emulator and its explicit AOT artifact;
it does not load LLVM or the collector. `-A existing.so -C new-profile` can
collect uncovered execution while using an existing artifact. Warm reboot
checkpoints retain collector ownership and defer manifest finalization until
shutdown. Pass multiple profile directories to `tools/aot.py profile-a profile-b -o
merged.so` to merge compatible collections. Conflicting page graphs are
renamed through LLVM-C; the entry index preserves the union of observed PCs
and prefers the graph with broader coverage for shared entries. All profiles
must have the same executable/image/options identity. Automatic artifact
discovery remains outside the current workflow. Offline compilation caches
complete objects in
`<profile>/.objects`, keyed by bitcode, compiler, recipe, and optimization level;
`--cache-dir` selects a cache shared across profiles, which must be owned by
the current user and writable by no one else. macOS retains
T1C.

Native profile-guided optimization is optional and runs entirely offline.
Compile an instrumented artifact, replay representative inputs, merge the
result with the matching LLVM tools, and compile the deployment artifact:

```shell
$ python3 tools/aot.py build/program-profile -o build/program-training.so --llvm-config llvm-config-23 --profile-generate
$ LLVM_PROFILE_FILE="$PWD/build/program-%p-%m.profraw" build/rv32emu -A build/program-training.so program.elf
$ llvm-profdata-23 merge build/program-*.profraw -o build/program.profdata
$ python3 tools/aot.py build/program-profile -o build/program.so --llvm-config llvm-config-23 --profile-use build/program.profdata
```

Generation skips late SLP vectorization after IR profile instrumentation; the
deployment build retains the full optimization pipeline. This reduces analysis
work on large page graphs without changing the instrumented CFG.
Generation requires the matching Clang profiling runtime. If it is installed
outside Clang's resource directory, pass its static archive through
`--profile-runtime /path/to/libclang_rt.profile-x86_64.a`. Deployment artifacts
compiled with `--profile-use` do not need that runtime. Profile contents are
included in the object-cache key. Keep the collection, LLVM installation,
optimization level, and ThinLTO mode identical during generation and use,
and finish training with a clean emulator exit so the runtime writes its data.
Measure on held-out inputs before deploying;
the benefit depends on the workload.

Pass `--thin-lto` to enable cross-module optimization with the matching
`ld.lld` installed beside Clang. This works with or without native PGO and
keeps a separate incremental backend cache under the object-cache directory.
`-j` bounds both compilation and ThinLTO backend parallelism.

For Linux with an initramfs, use the same build and guest inputs for training
and deployment:

```shell
$ make system_jit_defconfig
$ make ENABLE_ROOTFS_EXT4=0 INITRD_SIZE=32 ENABLE_SDL=0
$ build/rv32emu -k build/linux-image/Image -i build/linux-image/rootfs.cpio -C build/linux-profile
# Log in, exercise the workload, then run poweroff to finish collection.
$ python3 tools/aot.py build/linux-profile -o build/linux.so --llvm-config llvm-config-23
$ build/rv32emu -k build/linux-image/Image -i build/linux-image/rootfs.cpio -A ./build/linux.so
```

The offline compiler accepts bitcode from its own or an older LLVM major, as
the manifest check above enforces; using one installation for both steps is
simplest. Select each version explicitly with `make LLVM_CONFIG=llvm-config-19` and
`tools/aot.py --llvm-config llvm-config-19`, for example.

Run `make CC=gcc CROSS_COMPILE=riscv64-linux-gnu- check-aot` in a user build,
or `check-system-aot` in a system build, for collection/compilation/replay
regressions. `make check` includes the applicable suite when the required
cross assembler and ISA extensions are available. The Ubuntu 24.04 CI matrix
builds and runs both suites with every LLVM release from 19 to 23; the default,
LLVM 23, also runs the user suite with native PGO and the full Linux
collect/compile/boot.
`.ci/boot-aot.py --emulator build/rv32emu --llvm-config llvm-config-23
--output build/linux-aot-results` checks login, guest commands, native AOT
entries, a held-out filesystem workload, and shutdown. It records alternating
paired login times against T1 with an identical `-s 4096` dispatch budget.
Override it with `--cycles`, and the block cache with `--cache-bits`.
Use `--runs 20 --cpu <allowed-cpu>` for pinned repeated measurements; results
include a paired bootstrap interval and native execution counters.
Offline compilation time is excluded; training and compilation must finish
before measuring deployment startup. Speedup depends on workload coverage and
host; `--min-speedup` can enforce a threshold on a controlled benchmark host.

Alternatively, enable only the tier-1 JIT with the legacy command-line option:
```shell
$ make ENABLE_JIT=1
```

If you don't want the JIT compilation feature, build the interpreter only:
```shell
$ make interpreter_defconfig
$ make
```

## Configuration methods

There are three ways to customize the build.

### 1. Predefined configurations

Use predefined configurations for common use cases:
```shell
$ make defconfig            # Default: tier-1 JIT, SDL enabled, all extensions
$ make interpreter_defconfig # Default without the JIT
$ make mini_defconfig       # Minimal: no SDL, basic extensions only
$ make jit_defconfig        # JIT and user-mode T2C AOT support
$ make system_defconfig     # System: Linux system emulation, tier-1 JIT
$ make system_interpreter_defconfig # System emulation without the JIT
$ make system_jit_defconfig # System emulation with T1 JIT and T2C AOT
$ make wasm_defconfig       # WebAssembly: build for browser deployment
$ make wasm_system_defconfig # WebAssembly: system emulation in the browser
```

### 2. Interactive configuration

Use the menu-driven interface to customize options:
```shell
$ make config
```

### 3. Command-line override (legacy)

Override individual options on the command line (for backward compatibility):
```shell
$ make ENABLE_EXT_F=0     # Build without floating-point support
$ make ENABLE_SDL=0       # Build without SDL support
```

## Available options

* `ENABLE_RV32E`: RV32E Base Integer Instruction Set
* `ENABLE_EXT_M`: Standard Extension for Integer Multiplication and Division
* `ENABLE_EXT_A`: Standard Extension for Atomic Instructions
* `ENABLE_EXT_F`: Standard Extension for Single-Precision Floating Point Instructions
* `ENABLE_EXT_C`: Standard Extension for Compressed Instructions (RV32C.D excluded)
* `EXT_V` (Kconfig only — no `ENABLE_EXT_V` shim): Standard Extension for Vector Instructions (experimental: decode plus partial execution; `VLEN` defaults to 128, override at the make line with `VLEN=<n>`; selecting V auto-selects F). Enable via `make config`.
* `ENABLE_Zba`: Standard Extension for Address Generation Instructions
* `ENABLE_Zbb`: Standard Extension for Basic Bit-Manipulation Instructions
* `ENABLE_Zbc`: Standard Extension for Carry-Less Multiplication Instructions
* `ENABLE_Zbs`: Standard Extension for Single-Bit Instructions
* `ENABLE_Zicsr`: Control and Status Register (CSR)
* `ENABLE_Zifencei`: Instruction-Fetch Fence
* `ENABLE_GDBSTUB`: GDB remote debugging support
* `ENABLE_SDL`: Display and Event System Calls for running video games
* `ENABLE_JIT`: Tier-1 (template) JIT for hot basic blocks. Wired through `mk/compat.mk` so command-line override is supported.
* `ENABLE_T2C`: Linux-host LLVM AOT collection and artifact execution for ELF and Linux guests. Selected by `jit_defconfig` when supported LLVM is detected. Requires explicit `-C` collection and `-A` artifact selection.
* `ENABLE_SYSTEM`: System emulation for booting the RV32 Linux kernel (MMU, UART, virtio, timer).
* `ENABLE_GOLDFISH_RTC`: Goldfish RTC peripheral; selectable only when `SYSTEM=y` and `ELF_LOADER=n`.
* `ENABLE_ELF_LOADER`: In system mode, run user ELF binaries directly instead of booting a kernel image. Required to run `make check` against the system build.
* `ENABLE_SDL_MIXER`: SDL2_mixer audio (depends on `SDL=y`).
* `ENABLE_MOP_FUSION`: Macro-operation fusion in the IR (default on).
* `ENABLE_BLOCK_CHAINING`: Chain translated blocks to bypass the dispatcher (default on).
* `ENABLE_LTO`: Link-time optimization (default on; requires GCC or Clang).
* `ENABLE_UBSAN`: Build with `-fsanitize=undefined` to surface UB at runtime.
* `ENABLE_ARCH_TEST`: Enable architectural-test exit reporting for the ACT4 harness; see [arch-test.md](arch-test.md).
* `INITRD_SIZE`: System mode only — initrd reservation in MiB. `mk/system.mk` auto-sizes from the on-disk `rootfs.cpio` (file size + 2 MiB) when present, otherwise defaults to 32 MiB. Override on the make line, e.g. `make system ENABLE_SYSTEM=1 INITRD_SIZE=64` for SDL workloads that bundle larger assets.
* `VLEN`: V extension vector length in bits (default 128). Override on the make line, e.g. `make VLEN=256`. See `EXT_V` above for enabling vector support.
