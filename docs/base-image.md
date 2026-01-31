# Base images

The goal is to support both `x86-64` and `aarch64` docker images for development and testing. 

The base images include `gcc` and `sail` toolchains. We compile the toolchains from source and use specific versions.

The build process should be used when an update on the toolchain is required. When a new build is made by running `build.sh`, build and push to Docker Hub are automated. 

## Details

For `gcc`, we are using the compiler version `tags/2023.10.06`, as using the binaries from `apt install gcc-riscv64-unknown-elf` will cause `"unsupported ISA subset 'z'"` error during the compilation of the emulator on M1.

For `sail`, we set up the toolchain to use `sail-0.16`, as the latest version `0.17.x` series will cause compilation errors.

We are using the commit `9547a30bf84572c458476591b569a95f5232c1c7` from `sail-riscv` for the reference simulator, as this is the commit closest to the time when the [x86-64 reference sail emulator](https://github.com/sysprog21/rv32emu/commit/01b00b6f175f57ef39ffd1f4fa6a611891e36df3#diff-3b436c5e32c40ecca4095bdacc1fb69c0759096f86e029238ce34bbe73c6e68f) was added to the `rv32emu` Github repo.
