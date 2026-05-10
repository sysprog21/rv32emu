# rv32emu ACT4 experiments

This directory contains rv32emu-specific configuration for the current
`riscv/riscv-arch-test` ACT4 framework, checked out as `tests/riscv-act`.

The initial configuration targets user-mode RV32 tests only:

- config: `tests/act-rv32emu/rv32emu-rv32imafc/test_config.yaml`
- generated ELFs: `build/act/rv32emu-rv32imafc/elfs`
- runner: `tools/run-act-elfs.sh`

Required external tools:

- `riscv64-elf-gcc`
- `riscv64-elf-objdump`
- `uv`
- Ruby `bundle`
- `sail_riscv_sim`

Build rv32emu for architectural test mode:

```sh
PKG_CONFIG=/usr/bin/pkg-config make cleanconfig
PKG_CONFIG=/usr/bin/pkg-config make ci_defconfig
PKG_CONFIG=/usr/bin/pkg-config make -j$(nproc)
```

Generate a small first batch of ACT4 ELFs:

```sh
PKG_CONFIG=/usr/bin/pkg-config make act-user-elfs ACT_EXTENSIONS=I
```

Run generated ELFs through rv32emu:

```sh
PKG_CONFIG=/usr/bin/pkg-config make act-user-run ACT_EXTENSIONS=I
```

