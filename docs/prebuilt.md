# Prebuilt Binaries

There are some prebuilt binaries placed in [rv32emu-prebuilt](https://github.com/sysprog21/rv32emu-prebuilt).
When invoking testing or benchmarking, the prebuilt binaries will be pulled into `build/linux-x64/` and `build/riscv32/` directory in default.
The RISC-V binaries are built from [xPack RISC-V GCC toolchain](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack) with `-march=rv32im -mabi=ilp32` options.

To fetch the prebuilt binaries manually, run:

```shell
$ make artifact
```

Or build the binaries from scratch (the RISC-V cross-compiler is required):

```shell
$ make artifact USE_PREBUILT=0 [CROSS_COMPILE=<COMPILER_PREFIX>]
```

The compiler prefix varies according to the used toolchain, such as `riscv-none-elf-`, `riscv32-unknwon-elf-`, etc.

The prebuilt binaries in `rv32emu-prebuilt` are built from the following repositories and resources:

- [ansibench](https://github.com/nfinit/ansibench)
    - coremark
    - stream
    - nbench
- [rv8-bench](https://github.com/michaeljclark/rv8-bench)
    - aes
    - dhrystone
    - miniz
    - norx
    - primes
    - qsort
    - sha512
- `captcha` : See [tests/captcha.c](tests/captcha.c)
- `donut` : See [donut.c](tests/donut.c)
- `fcalc` : See [fcalc.c](tests/fcalc.c)
- `hamilton` : See [hamilton.c](tests/hamilton.c)
- `jit` : See [tests/jit.c](tests/jit.c)
- `lena`: See [tests/lena.c](tests/lena.c)
- `line` : See [tests/line.c](tests/line.c)
- `maj2random` : See [tests/maj2random.c](tests/maj2random.c)
- `mandelbrot` : See [tests/mandelbrot.c](tests/mandelbrot.c)
- `nqueens` : See [tests/nqueens.c](tests/nqueens.c)
- `nyancat` : See [tests/nyancat.c](tests/nyancat.c)
- `pi` : See [tests/pi.c](tests/pi.c)
- `puzzle` : See [tests/puzzle.c](tests/puzzle.c)
- `qrcode` : See [tests/qrcode.c](tests/qrcode.c)
- `richards` : See [tests/richards.c](tests/richards.c)
- `rvsim` : See [tests/rvsim.c](tests/rvsim.c)
- `spirograph` : See [tests/spirograph.c](tests/spirograph.c)
- `uaes` : See [tests/uaes.c](tests/uaes.c)

---

There are still some prebuilt standalone RISC-V binaries under `build/` directory only for testing purpose:

- `hello.elf` : See [tests/asm-hello](tests/asm-hello)
- `cc.elf` : See [tests/cc](tests/cc)
- `chacha20.elf` : See [tests/chacha20](tests/chacha20)
- `doom.elf` : See [sysprog21/doom_riscv](https://github.com/sysprog21/doom_riscv) [RV32M]
- `ieee754.elf` : See [tests/ieee754.c](tests/ieee754.c) [RV32F]
- `jit-bf.elf` : See [ezaki-k/xkon_beta](https://github.com/ezaki-k/xkon_beta)
- `quake.elf` : See [sysprog21/quake-embedded](https://github.com/sysprog21/quake-embedded) [RV32F]
- `readelf.elf` : See [tests/readelf](tests/readelf)
- `scimark2.elf` : See [tests/scimark2](tests/scimark2) [RV32MF]
- `smolnes.elf` : See [tests/smolnes](tests/smolnes.c) [RV32M]
