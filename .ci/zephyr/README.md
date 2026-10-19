# Zephyr boot checks

`boot-zephyr.yml` builds `hello_world` and `synchronization` for
`qemu_riscv32`, then runs them on the system interpreter and tier-1 JIT in S-mode using
rv32emu's built-in SBI. The overlay supplies the UART/PLIC addresses,
RAM at zero, a 65 MHz timer, and RV32IMAC without the D extension.
No Zephyr source changes or OpenSBI image are required.

The workflow follows the Linux boot jobs' setup action, Bash/common.sh
helpers, Expect PTY checks, Ubuntu 24.04 runner, bounded execution, and
read-only repository permissions. It builds guests from pinned Zephyr
and picolibc revisions. Picolibc is an
explicit module override rather than Zephyr's manifest-selected version.
Only build dependencies are installed; these samples need no other modules.

## TLS and the Linux build

The Linux image workflow calls `make build-linux-image`.
`tools/build-linux-image.sh` first builds Buildroot using
`assets/system/configs/buildroot.config`, which selects RV32/ILP32 and
glibc. It then builds Linux with Buildroot's
`riscv32-buildroot-linux-gnu-` compiler. It does not use the xPack
bare-metal compiler installed by `.ci/riscv-toolchain-install.sh`.

Buildroot 2025.11's [GCC configuration](https://github.com/buildroot/buildroot/blob/2025.11/package/gcc/gcc.mk)
enables compiler TLS for glibc. The Linux root filesystem therefore uses
a toolchain and libc with native thread-local storage. Linux kernel
compilation itself does not depend on Zephyr's TLS configuration.
The Linux config's disabled `CONFIG_TLS` refers to kernel Transport Layer
Security, a separate feature from thread-local storage.

The Zephyr workflow tests two configurations:

| Compiler | Zephyr TLS | Purpose |
| --- | --- | --- |
| Distribution `riscv64-unknown-elf-gcc` | Enabled | Exercise native TLS with picolibc |
| CI xPack `riscv-none-elf-gcc` | Disabled | Exercise the existing CI compiler, built with `--disable-tls` |

`no-tls.conf` exposes the otherwise hidden TLS setting so the board default
can be overridden, then disables it. The support flag is a Kconfig
visibility workaround; it does not make xPack support native TLS.
Both builds reject emulated-TLS symbols and verify the generated S-mode,
SBI, timer, libc, clock, and TLS settings before booting.

## Run locally

Install the usual system-build dependencies plus CMake, Ninja, a RISC-V
compiler, Python build dependencies from Zephyr's
`scripts/requirements-base.txt`, and Expect. Activate the Python environment.
From the rv32emu checkout, with Zephyr and picolibc checkouts available:

```sh
unset CROSS_COMPILE
make system_interpreter_defconfig
make CONFIG_VIRTIO_NET=n -j8

export ZEPHYR_BASE=/absolute/path/to/zephyr
export PICOLIBC_DIR=/absolute/path/to/picolibc
export CROSS_COMPILE=/usr/bin/riscv64-unknown-elf-
export ZEPHYR_TLS=y
.ci/build-zephyr.sh
.ci/boot-zephyr.sh
```

For xPack, set `CROSS_COMPILE` to its absolute `riscv-none-elf-` prefix
and `ZEPHYR_TLS=n`. Use a separate `ZEPHYR_OUT` directory when switching
compilers, because CMake caches the compiler selection.

The boot check requires the hello greeting and at least 38 alternating
complete `thread_a`/`thread_b` messages within 60 seconds. It terminates
and reaps only its spawned guests. `BOOT_TIMEOUT`, `SYNC_LINES` (minimum 2),
`RV32EMU`, and `ZEPHYR_OUT` can be overridden for local checks.
Logs are retained under `$ZEPHYR_OUT/logs` and uploaded by Actions, including
on failure.

The workflow covers the system interpreter and tier-1 JIT with both
toolchains. T2C, Twister, and isolated busy-loop timer delivery are outside
these checks.
