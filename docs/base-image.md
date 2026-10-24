# Base images

`rv32emu` ships two reproducible Docker images that bundle the toolchains
needed for development and architectural testing across `x86-64` and
`aarch64` hosts. The build script `docker/build.sh` pushes both images to
Docker Hub via `docker buildx`, producing `linux/amd64` and `linux/arm64/v8`
manifests for each image.

| Image | Purpose | Recipe |
| ----- | ------- | ------ |
| `sysprog21/rv32emu-gcc` | RISC-V cross-compiler for building target binaries | [`docker/Dockerfile-gcc`](../docker/Dockerfile-gcc) |
| `sysprog21/rv32emu-sail` | RISC-V Sail reference simulator used by ACT4 | [`docker/Dockerfile-sail`](../docker/Dockerfile-sail) |

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

`docker/Dockerfile-sail` installs the official Sail RISC-V 0.16 binary for
x86-64 or aarch64, matching the pinned ACT4 framework. It exports
`/opt/sail-riscv/bin` on `PATH`; the executable is `sail_riscv_sim`.
The build verifies the executable version. See the
[architecture-test guide](arch-test.md) for the complete ACT4 environment.

## Rebuilding and publishing

```shell
$ cd docker
$ ./build.sh
```

`docker/build.sh` creates a `buildx` builder named `cross-platform-builder`
and pushes multi-arch manifests for `sysprog21/rv32emu-gcc` and
`sysprog21/rv32emu-sail`. Run it whenever a pinned version above changes.
