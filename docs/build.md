# Build and customization

`rv32emu` uses a [Kconfig](https://github.com/sysprog21/Kconfiglib)-based build
system. This document covers JIT compiler setup and detailed build-time
options.

## Tiered JIT compilation

The tier-2 JIT compiler in `rv32emu` leverages LLVM for powerful optimization.
The target system must have [`LLVM`](https://llvm.org/) installed; versions 18
through 21 are accepted, and LLVM 20+ is the validated default exercised by
CI on both macOS (arm64, Homebrew) and Linux (x86-64, Ubuntu 24.04). If `LLVM`
is not installed, only the tier-1 JIT compiler will be used for performance
enhancement.

* macOS: `brew install llvm@20` (LLVM 18, 19, and 21 are also accepted)
* Ubuntu Linux / Debian: `sudo apt-get install llvm-20-dev clang-20 lld-20` — the `-dev` package is required because T2C builds against `<llvm-c/*.h>` and links via `llvm-config`. On releases that don't ship LLVM 20 in the base repos (Ubuntu 24.04 caps at llvm-18), add [apt.llvm.org](https://apt.llvm.org/) first. LLVM 18, 19, and 21 are also accepted.

The Makefile auto-detects `llvm-config` in `$PATH` (preferring the newest
supported version) and the matching Homebrew prefix; override with
`make LLVM_CONFIG=/path/to/llvm-config` to pin a specific install.

Build the emulator with JIT compiler using the predefined configuration:
```shell
$ make jit_defconfig
$ make
```

Alternatively, use the legacy command-line option (for backward compatibility):
```shell
$ make ENABLE_JIT=1
```

If you don't want the JIT compilation feature, simply build with the following:
```shell
$ make defconfig
$ make
```

## Configuration methods

There are three ways to customize the build.

### 1. Predefined configurations

Use predefined configurations for common use cases:
```shell
$ make defconfig            # Default: SDL enabled, all extensions
$ make mini_defconfig       # Minimal: no SDL, basic extensions only
$ make jit_defconfig        # JIT: enables tiered JIT compilation
$ make system_defconfig     # System: enables Linux system emulation
$ make system_jit_defconfig # System+JIT: enables Linux system emulation with JIT
$ make wasm_defconfig       # WebAssembly: build for browser deployment
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
* `ENABLE_T2C`: Tier-2 LLVM JIT layered on top of `JIT`. Auto-selected by `jit_defconfig` when a supported LLVM is detected (18-21, LLVM 20+ validated). Set `ENABLE_T2C=0` (or disable in `make config`) to stay on Tier-1 only.
* `T2C_OPT_LEVEL`: LLVM optimization level for the tier-2 JIT (0-3, default 3). Visible only when `T2C=y`.
* `ENABLE_SYSTEM`: System emulation for booting the RV32 Linux kernel (MMU, UART, virtio, timer).
* `ENABLE_GOLDFISH_RTC`: Goldfish RTC peripheral; selectable only when `SYSTEM=y` and `ELF_LOADER=n`.
* `ENABLE_ELF_LOADER`: In system mode, run user ELF binaries directly instead of booting a kernel image. Required to run `make check` against the system build.
* `ENABLE_SDL_MIXER`: SDL2_mixer audio (depends on `SDL=y`).
* `ENABLE_MOP_FUSION`: Macro-operation fusion in the IR (default on).
* `ENABLE_BLOCK_CHAINING`: Chain translated blocks to bypass the dispatcher (default on).
* `ENABLE_LTO`: Link-time optimization (default on; requires GCC or Clang).
* `ENABLE_UBSAN`: Build with `-fsanitize=undefined` to surface UB at runtime.
* `ENABLE_ARCH_TEST`: Build the RISCOF-driven arch-test harness; see [riscof.md](riscof.md).
* `INITRD_SIZE`: System mode only — initrd reservation in MiB. `mk/system.mk` auto-sizes from the on-disk `rootfs.cpio` (file size + 2 MiB) when present, otherwise defaults to 32 MiB. Override on the make line, e.g. `make system ENABLE_SYSTEM=1 INITRD_SIZE=64` for SDL workloads that bundle larger assets.
* `VLEN`: V extension vector length in bits (default 128). Override on the make line, e.g. `make VLEN=256`. See `EXT_V` above for enabling vector support.
