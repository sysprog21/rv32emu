# RISC-V architectural tests

`make arch-test` runs the ACT4 suite from the `tests/riscv-arch-test`
submodule against a user-mode build. Select another DUT description with
`ARCH_TEST_CONFIG`, and limit generation and execution to some extensions
with `ARCH_TEST_EXTENSIONS`. The
[ACT4 setup and test guide](../tests/arch-test-target/README.md) lists the
required tools and describes the CI matrix.

ACT4 replaces the earlier RISCOF flow; its `RISCV_DEVICE` and `SKIP_PREREQ`
options no longer exist.
