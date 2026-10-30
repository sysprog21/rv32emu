# Public API tests

These host tests include only `include/rv32emu.h` and standard headers. Their
build rules deliberately omit `-Isrc` and the core's forced `common.h` include,
so accidental dependencies on private configuration or structures fail to build.

- `test-api.c` builds a small ELF without a cross compiler and checks creation,
  load failure/retry, options and argument limits, guest argument ownership,
  register/PC access, memory boundaries and failure atomicity, halted states,
  repeated lifecycle operations, and yielding/redirecting a hot loop.
- `test-cxx.cc` checks standalone/repeated inclusion and C linkage from C++11.
- `check-symbols.py` checks Linux hosts linked with `--export-dynamic`: all
  public entry points remain exported while core and SoftFloat symbols stay
  hidden. T2C builds also verify the collector's four-symbol interface.
- `run-guest.c` runs established assembly regressions through the public API.
  `make check` compares their output with the same expectations as the CLI:
  integer arithmetic, shifts, register loops, loads into x0, multiply/divide,
  LR/SC, cycle accounting, and FENCE.I, subject to ISA/toolchain availability.

Run `make check-api` for the self-contained C/C++ contract tests. They also run
under `make tests` and `make check` in native user-mode configurations. Run
`make check` for the additional guest regressions. Exercise both backends with:

```sh
make OUT=build/api check-api
make OUT=build/api-interp ENABLE_JIT=0 check-api
```

The guest runner caps the number of execution slices and fails if the guest
never halts. A regression that prevents a slice itself from returning still
requires the enclosing CI job's timeout.

Component tests such as cache, decoder, ELF parser, and MMU checks intentionally
retain private headers: their internal invariants are not exposed by this API.
