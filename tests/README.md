# System regression checks

Run these commands from the repository root. See the
[Quick start](../README.md#quick-start) for general build dependencies.
The [build guide](../docs/build.md) explains LLVM setup for T2C. The
[system setup guide](../docs/system.md) covers `dtc`. These checks need GNU
Make, a supported host C compiler, `dtc`, and a RISC-V bare-metal GCC toolchain
with `objcopy`. Set `CROSS_COMPILE` to the toolchain prefix if the build does
not detect it.

## Pending interrupt and WFI

Run the regression with both the interpreter and JIT system builds. The ext4
rootfs device must be disabled because this test loads a raw guest binary.

```sh
make distclean
make system_interpreter_defconfig
make ENABLE_ROOTFS_EXT4=0 check-wfi-pending

make distclean
make system_defconfig
make ENABLE_ROOTFS_EXT4=0 check-wfi-pending
```

Each run should print `Running wfi ... [OK]`. The guest checks that a locally
enabled pending SSIP resumes WFI while global SIE is clear. It also checks
that a disabled SSIP does not prevent the timer fast-forward path, and that a
timer can still advance WFI when no interrupt is pending. It does not inject a
PLIC-backed SEIP.

## Fetch fault at a page boundary

Run with ELF loading enabled, testing block chaining both disabled and
enabled:

```sh
make distclean
make system_interpreter_defconfig
make ENABLE_ELF_LOADER=1 ENABLE_BLOCK_CHAINING=0 check-fetch-page-boundary

make distclean
make system_interpreter_defconfig
make ENABLE_ELF_LOADER=1 ENABLE_BLOCK_CHAINING=1 check-fetch-page-boundary
```

Each run should print `Running fetch-page-boundary ... [OK]`. The guest checks
that instructions on the mapped page retire before a fetch fault on the next
page is delivered.
This test does not cover a 32-bit instruction that straddles a page boundary
when the C extension is enabled.

## Tier-2 JIT build

LLVM 18–21 development libraries are required. The build detects a compatible
`llvm-config`; pass `LLVM_CONFIG=llvm-config-21` to pin one. The
`system_jit_defconfig` enables both JIT tiers. Use this configuration to check pending WFI:

```sh
make distclean
make system_jit_defconfig
make LLVM_CONFIG=llvm-config-21 ENABLE_ROOTFS_EXT4=0 check-wfi-pending
test -f build/t2c.o

make distclean
make system_jit_defconfig
make LLVM_CONFIG=llvm-config-21 ENABLE_ELF_LOADER=1 \
    ENABLE_BLOCK_CHAINING=0 check-fetch-page-boundary
test -f build/t2c.o

make distclean
make system_jit_defconfig
make LLVM_CONFIG=llvm-config-21 ENABLE_ELF_LOADER=1 \
    ENABLE_BLOCK_CHAINING=1 check-fetch-page-boundary
test -f build/t2c.o
```

These short guests do not prove that execution reached T2C tier-up.
