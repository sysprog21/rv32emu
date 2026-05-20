# Base images

`rv32emu` ships two reproducible Docker images that bundle the toolchains
needed for development and architectural testing across `x86-64` and
`aarch64` hosts. The build script `docker/build.sh` pushes both images to
Docker Hub via `docker buildx`, producing `linux/amd64` and `linux/arm64/v8`
manifests for each image.

| Image | Purpose | Recipe |
| ----- | ------- | ------ |
| `sysprog21/rv32emu-gcc` | RISC-V cross-compiler for building target binaries | [`docker/Dockerfile-gcc`](../docker/Dockerfile-gcc) |
| `sysprog21/rv32emu-sail` | RISC-V SAIL reference simulator used by RISCOF | [`docker/Dockerfile-sail`](../docker/Dockerfile-sail) |

Both images share `ubuntu:24.04` as the base layer.

## Pinned versions

### GCC toolchain

`docker/Dockerfile-gcc` builds the cross-compiler from source against
[`riscv/riscv-gnu-toolchain`](https://github.com/riscv/riscv-gnu-toolchain)
at tag `2024.04.12`. Building from source rather than installing
`gcc-riscv64-unknown-elf` from `apt` avoids the
`"unsupported ISA subset 'z'"` error that the Ubuntu-packaged toolchain
produces on aarch64 hosts (Apple silicon in particular), and building from
source rather than fetching the upstream nightly tarball is required
because that nightly only ships an x86-64 binary.

Build flags used by the image:

* `--prefix=/opt/riscv`
* `--with-arch=rv32gc`
* `--with-abi=ilp32d`
* `make newlib` (newlib-based bare-metal toolchain, triple
  `riscv32-unknown-elf-`)

To bump GCC, change the `git checkout tags/2024.04.12` line in
[`docker/Dockerfile-gcc`](../docker/Dockerfile-gcc) and rerun
`docker/build.sh`.

> Host builds outside Docker use `.ci/riscv-toolchain-install.sh` instead,
> which downloads a prebuilt xpack `riscv-none-elf-gcc` release (or, with
> `riscv-collab` as the first argument, the upstream nightly). That script
> is not invoked by the Docker images described above.

### SAIL reference simulator

`docker/Dockerfile-sail` installs SAIL via opam:

* OCaml base compiler: `5.2.0`
* `sail`: `0.17.1`
* `sail-riscv`: cloned from
  [`riscv/sail-riscv`](https://github.com/riscv/sail-riscv) and pinned to
  commit `0e9850fed5bee44346e583f334c6e2a6a25d5cd3`, then built against
  the installed SAIL with `ARCH=RV32 make`.

System dependencies installed in the image: `opam`, `zlib1g-dev`,
`pkg-config`, `libgmp-dev`, `z3`, and `device-tree-compiler`.

The final stage copies `sail-riscv/c_emulator/riscv_sim_RV32` into the
output image. That binary is the reference signature generator used by
RISCOF (see [riscof.md](riscof.md)).

## Rebuilding and publishing

```shell
$ cd docker
$ ./build.sh
```

`docker/build.sh` creates a `buildx` builder named `cross-platform-builder`
and pushes multi-arch manifests for `sysprog21/rv32emu-gcc` and
`sysprog21/rv32emu-sail`. Run it whenever a pinned version above changes.
