# GDB remote debugging

`rv32emu` supports a subset of the
[GDB Remote Serial Protocol](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html)
(GDBRSP). The shipped defconfigs do not enable GDB stub support; turn it on
via the configuration system — run `make config` and enable
`ENABLE_GDBSTUB`, or pass `ENABLE_GDBSTUB=1` on the command line. Then
launch with:
```shell
$ build/rv32emu -g <binary>
```

The `<binary>` should be the ELF file in RISC-V 32 bit format. Additionally,
it is advised that you compile programs with the `-g` option in order to
produce debug information in your ELF files.

You can run the RISC-V GDB build that ships with your cross-toolchain if
the emulator starts up correctly without an error. The exact binary name
depends on the toolchain prefix:

* `riscv32-unknown-elf-gdb` — from the upstream `riscv-gnu-toolchain`
  build (also what the Docker image installs).
* `riscv-none-elf-gdb` — from the xpack `riscv-none-elf-gcc` release
  that `.ci/riscv-toolchain-install.sh` fetches by default.

The `.ci/gdbstub-test.sh` harness probes both prefixes, so either works.
Replace the command below with the prefix that matches your toolchain.

It takes two GDB commands to connect to the emulator after giving
GDB the supported architecture of the emulator and any debugging symbols it
may have:
```shell
$ riscv32-unknown-elf-gdb
(gdb) file <binary>
(gdb) target remote :1234
```

Congratulate yourself if GDB does not produce an error message.
Now that the GDB command line is available, you can communicate with
`rv32emu`.

## Dump registers as JSON

If the `-d [filename]` option is provided, the emulator will output
registers in JSON format. This feature can be utilized for tests involving
the emulator, such as compiler tests.

You can also combine this option with `-q` to directly use the output. For
example, if you want to read the register x10 (a0), then run the following
command:
```shell
$ build/rv32emu -d - -q out.elf | jq .x10
```
