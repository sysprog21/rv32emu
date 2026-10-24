# rv32emu ACT4 user tests

This configuration runs unprivileged RV32IMAFC, Zba, Zbb, Zbc, Zbs, Zbkc,
Zmmul, Zicsr, Zicntr, and Zifencei tests from the pinned
`tests/riscv-arch-test` submodule. ACT4 generates self-checking ELFs using
the Sail reference model. Passing this subset does
not establish full architectural certification: privileged tests are disabled,
and extensions outside the configuration are not tested.

Required tools for the pinned ACT4 revision:

- GCC 15 or later, named `riscv-none-elf-gcc`, and matching `riscv-none-elf-objdump`
  (other tool names can be set in a copy of `test_config.yaml`)
- `uv` 0.13.4
- Ruby and Bundler (`bundle`); ACT4 recommends Ruby 3.4.11
- `sail_riscv_sim` version **0.16**; other versions are rejected by this revision
- GNU `timeout` for bounded DUT execution

The setup script and the default Sail path assume a Linux host (x86-64 or
aarch64), as CI uses; on other hosts, install the tools yourself and set
`ARCH_TEST_SAIL_BIN_DIR`.

Sail is searched first in
`build/act-tools/sail-riscv-Linux-$(uname -m)/bin`, then on `PATH`. Override
`ARCH_TEST_SAIL_BIN_DIR` for another installation. `.ci/arch-test-setup.sh`
installs Sail there after checking the release tarball against SHA-256
digests it pins per Sail version, so moving the submodule to a new Sail
release means adding that release's digests there and in
`docker/Dockerfile-sail`. Follow the pinned ACT4 README
for tool installation and its UDB gem dependencies.

Build rv32emu in architectural test mode:

```sh
make cleanconfig
make ci_defconfig
make -j8
```

Generate and run the default user test set:

```sh
make arch-test JOBS=8
```

By default, ACT4 selects every test applicable to the DUT configuration,
including LR/SC and floating-point tests. A comma-separated
selection limits both generation and execution:

```sh
make arch-test ARCH_TEST_EXTENSIONS=F JOBS=8
```

Use `make arch-test-elfs` to generate without running. ELFs are written to
`build/arch-test/rv32emu-rv32imafc/elfs`. To rerun existing ELFs directly:

```sh
tools/run-arch-test-elfs.sh build/rv32emu build/arch-test/rv32emu-rv32imafc/elfs
```

Each ELF has a 60-second timeout, configurable with `ACT_TIMEOUT` (GNU
`timeout` duration syntax). Failures print the emulator output and exit status;
status 124 indicates a timeout. Empty test sets and missing or empty requested
extension directories are errors. The runner continues after failing ELFs and
returns failure if any test fails.

Run integration checks without generating the ACT suite:

```sh
make check-arch-test-runner
make check-arch-test-tohost CC=gcc CROSS_COMPILE=riscv64-unknown-elf-
make check-arch-test-misalign CC=gcc CROSS_COMPILE=riscv64-unknown-elf-
```

The tohost check needs an architectural-test build and a RISC-V compiler. It checks
that only `tohost=1` passes, including a failure number that would otherwise
wrap to shell exit status zero.
The misalignment check covers floating-point loads and stores, compressed
forms, f0, and discarded integer loads into x0; it requires F and C enabled.

The GitHub build action accepts `arch-test: 'true'` to install GCC, Python/uv,
Ruby/UDB, and the pinned Sail version. `.ci/riscv-tests.sh` runs the same ACT4
configuration across five execution modes: interpreter and tier-1 JIT, each
with fusion/chaining disabled or enabled, plus an LLVM tiered build. It
generates the self-checking ELFs once and reuses them for every mode. The
focused regressions run for every mode too. LLVM is required for this matrix;
the workflow installs the version pinned in `.ci/llvm-version`.
Per-mode results and the ACT4 revision are written to
`build/arch-test/results/`; CI uploads them with the selected test list.

RV32E cannot currently be validated through ACT4: its pinned UDB dependency
rejects `E` as an unknown extension. Privileged execution and experimental V
are outside this user-mode configuration; passing these tests does not
establish compatibility for those features.

At ACT4 revision `70907fc6710829794739d48ae7a8be861360263f`, all 216
selected ELFs pass in all five execution modes with GCC 15.2, LLVM 23 (the
suggested version; LLVM 20 is also supported), and Sail 0.16. A passing
tiered build does not prove every test reached LLVM
code. The focused regressions also cover floating-point misalignment and
failure exit reporting. Generated test selection is recorded in
`build/arch-test/rv32emu-rv32imafc/test_list.md`.

The bundled profile rejects emulator builds with required ISA features
disabled. For a different implemented ISA, provide a matching
`ARCH_TEST_CONFIG` (and `ARCH_TEST_NAME` if it differs from the config directory
name). Custom configurations must describe the emulator under test.
