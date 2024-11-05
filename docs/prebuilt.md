# Prebuilt Binaries

The prebuilt binaries for [rv32emu](https://github.com/sysprog21/rv32emu) are prepared primarily because the [RISC-V Sail Model](https://github.com/riscv/sail-riscv) executable is required for the [RISC-V Architecture Test](https://github.com/riscv-non-isa/riscv-arch-test), and selected RISC-V ELF files are useful for ISA simulation validation and testing.
Some of these prebuilt binaries are stored in [rv32emu-prebuilt](https://github.com/sysprog21/rv32emu-prebuilt).
During testing or benchmarking, these binaries are automatically downloaded into the `build/linux-x86-softfp/` and `build/riscv32/` directories by default.
The RISC-V binaries are compiled using the [xPack RISC-V GCC toolchain](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack) with the options `-march=rv32im -mabi=ilp32`.
The x86 binaries are compiled by GCC with `-m32 -mno-sse -mno-sse2 -msoft-float` options and use [ieeelib](https://github.com/sysprog21/ieeelib) as the soft-fp library.

To fetch the prebuilt binaries manually, run:

```shell
$ make artifact
```

Or build the binaries from scratch (the RISC-V cross-compiler is required):

```shell
$ make artifact ENABLE_PREBUILT=0 [CROSS_COMPILE=<COMPILER_PREFIX>]
```

The compiler prefix varies according to the used toolchain, such as `riscv-none-elf-`, `riscv32-unknown-elf-`, etc.

The prebuilt binaries in `rv32emu-prebuilt` are built from the following repositories and resources:

- [ansibench](https://github.com/sysprog21/ansibench)
    - coremark
    - stream
    - nbench
- [rv8-bench](https://github.com/sysprog21/rv8-bench)
    - aes
    - dhrystone
    - miniz
    - norx
    - primes
    - qsort
    - sha512
- `captcha` : See [tests/captcha.c](/tests/captcha.c)
- `donut` : See [tests/donut.c](/tests/donut.c)
- `doom` : See [sysprog21/doom_riscv](https://github.com/sysprog21/doom_riscv)
- `fcalc` : See [tests/fcalc.c](/tests/fcalc.c)
- `hamilton` : See [tests/hamilton.c](/tests/hamilton.c)
- `jit` : See [tests/jit.c](/tests/jit.c)
- `lena`: See [tests/lena.c](/tests/lena.c)
- `line` : See [tests/line.c](/tests/line.c)
- `maj2random` : See [tests/maj2random.c](/tests/maj2random.c)
- `mandelbrot` : See [tests/mandelbrot.c](/tests/mandelbrot.c)
- `nqueens` : See [tests/nqueens.c](/tests/nqueens.c)
- `nyancat` : See [tests/nyancat.c](/tests/nyancat.c)
- `pi` : See [tests/pi.c](/tests/pi.c)
- `puzzle` : See [tests/puzzle.c](/tests/puzzle.c)
- `qrcode` : See [tests/qrcode.c](/tests/qrcode.c)
- `richards` : See [tests/richards.c](/tests/richards.c)
- `rvsim` : See [tests/rvsim.c](/tests/rvsim.c)
- `spirograph` : See [tests/spirograph.c](/tests/spirograph.c)
- `uaes` : See [tests/uaes.c](/tests/uaes.c)

To determine performance of the floating point arithmetic, the following RISC-V binaries are built with option `-march=rv32imf`:
- `quake` : See [sysprog21/quake-embedded](https://github.com/sysprog21/quake-embedded)
- `scimark2` : See [SciMark 2.0](https://math.nist.gov/scimark2)

There are still some prebuilt standalone RISC-V binaries under `build/` directory only for testing purpose:

- `hello.elf` : See [tests/asm-hello](/tests/asm-hello)
- `cc.elf` : See [tests/cc](/tests/cc)
- `chacha20.elf` : See [tests/chacha20](/tests/chacha20)
- `ieee754.elf` : See [tests/ieee754.c](/tests/ieee754.c) [RV32F]
- `jit-bf.elf` : See [ezaki-k/xkon_beta](https://github.com/ezaki-k/xkon_beta)
- `readelf.elf` : See [tests/readelf](/tests/readelf)
- `smolnes.elf` : See [tests/smolnes](/tests/smolnes.c) [RV32M]
