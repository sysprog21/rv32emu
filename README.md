# RISC-V RV32I[MAFC] emulator
![GitHub Actions](https://github.com/sysprog21/rv32emu/actions/workflows/main.yml/badge.svg)
```
                       /--===============------\
      ______     __    | |⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺⎺|     |
     |  _ \ \   / /    | |               |     |
     | |_) \ \ / /     | |   Emulator!   |     |
     |  _ < \ V /      | |               |     |
     |_| \_\ \_/       | |_______________|     |
      _________        |                   ::::|
     |___ /___ \       '======================='
       |_ \ __) |      //-'-'-'-'-'-'-'-'-'-'-\\
      ___) / __/      //_'_'_'_'_'_'_'_'_'_'_'_\\
     |____/_____|     [-------------------------]
```

`rv32emu` is an emulator for the 32 bit [RISC-V processor model](https://riscv.org/technical/specifications/) (RV32),
faithfully implementing the RISC-V instruction set architecture (ISA).
It serves as an exercise in modeling a modern RISC-based processor, demonstrating
the device's operations without the complexities of a hardware implementation.
The code is designed to be accessible and expandable, making it an ideal educational
tool and starting point for customization. It is primarily written in C99, utilizing
C11 atomics for memory management, with a focus on efficiency and readability.

Features:
* Fast interpreter that faithfully executes the complete RV32 instruction set
* Full coverage of RV32I / RV32E plus the M (integer multiply–divide), A (atomics), F (single-precision floating-point), C (compressed), and Zba/Zbb/Zbc/Zbs bit-manipulation extensions
* Partial support for the V (vector) extension — decode plus partial execution at `VLEN=128`; opt-in via `make config` (select "V — Vector Extension")
* Built-in ELF loader for user-mode emulation
* Newlib-compatible system-call layer for standalone programs
* Minimal system emulation capable of booting an RV32 Linux kernel and running user-space binaries
* SDL-based display/event/audio system calls for running video games
* WebAssembly build for user-mode and system emulation with SDL graphics and audio in modern browsers
* Remote debugging through the GDB Remote Serial Protocol
* Tiered JIT compilation for performance boost while maintaining a small footprint

## Quick start

`rv32emu` relies on the [SDL2 library](https://www.libsdl.org/) and
[SDL2_Mixer library](https://wiki.libsdl.org/SDL2_mixer) for full
functionality:
* macOS: `brew install sdl2 sdl2_mixer`
* Ubuntu Linux / Debian: `sudo apt install libsdl2-dev libsdl2-mixer-dev`

Build and verify:
```shell
$ make defconfig      # Apply default configuration
$ make                # Build rv32emu
$ make check          # Run tests
```

Run the included demos:
```shell
$ make doom           # Doom (1993)
$ make quake          # Quake (requires RV32F, on by default)
```

For interactive build configuration, use `make config`. For predefined
configurations, Kconfig options, and tiered JIT compilation setup
(LLVM toolchain), see [docs/build.md](docs/build.md).

## Online demo

A hosted WebAssembly build of `rv32emu` runs entirely in the browser, so
you can try it without building locally:
* [User-mode emulation demo page](https://sysprog21.github.io/rv32emu-demo/user/)
* [System emulation demo page](https://sysprog21.github.io/rv32emu-demo/system/)

The landing page links to both modes, and each mode page has a navigation
button that switches directly to the other.

## Documentation

| Topic | Document |
| ----- | -------- |
| Build options, Kconfig, and tiered JIT setup | [docs/build.md](docs/build.md) |
| System emulation: boot Linux, virtio block devices, bootargs | [docs/system.md](docs/system.md) |
| WebAssembly build for the browser | [docs/wasm.md](docs/wasm.md) |
| GDB remote debugging and register JSON dump | [docs/gdbstub.md](docs/gdbstub.md) |
| RISCOF / RISC-V architecture tests | [docs/riscof.md](docs/riscof.md) |
| Benchmarks and continuous benchmarking | [docs/benchmark.md](docs/benchmark.md) |
| Static analysis tools (rv_histogram, rv_profiler) | [docs/tools.md](docs/tools.md) |
| Docker image | [docs/docker.md](docs/docker.md) |
| Demo applications (Doom, Quake) | [docs/demo.md](docs/demo.md) |
| Code generation and JIT internals | [docs/codegen.md](docs/codegen.md) |
| RISC-V instruction reference | [docs/instruction.md](docs/instruction.md) |
| Newlib system calls | [docs/syscall.md](docs/syscall.md) |
| Prebuilt binaries | [docs/prebuilt.md](docs/prebuilt.md) |
| Base image preparation | [docs/base-image.md](docs/base-image.md) |

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## Citation
Please see our [VMIL'24](https://2024.splashcon.org/home/vmil-2024) paper, available in the [ACM digital library](https://dl.acm.org/doi/10.1145/3689490.3690399).
```
@inproceedings{ncku2024accelerate,
  title={Accelerate {RISC-V} Instruction Set Simulation by Tiered {JIT} Compilation},
  author={Chen, Yen-Fu and Chen, Meng-Hung and Huang, Ching-Chun and Tu, Chia-Heng},
  booktitle={Proceedings of the 16th ACM SIGPLAN International Workshop on Virtual Machines and Intermediate Languages},
  pages={12--22},
  year={2024}
}
```

## License
`rv32emu` is available under a permissive MIT-style license.
Use of this source code is governed by a MIT license that can be found in the [LICENSE](LICENSE) file.

## External sources
See [docs/prebuilt.md](docs/prebuilt.md).

## Reference
* [Writing a simple RISC-V emulator in plain C](https://fmash16.github.io/content/posts/riscv-emulator-in-c.html)
* [Writing a RISC-V Emulator in Rust](https://book.rvemu.app/)
* [Bare metal C on my RISC-V toy CPU](https://florian.noeding.com/posts/risc-v-toy-cpu/cpu-from-scratch/)
* [Juraj's RISC-V note](https://jborza.com/tags/riscv/)
* [GUI-VP: RISC-V based Virtual Prototype (VP) for graphical application development](https://github.com/ics-jku/GUI-VP)
* [LupV: an education-friendly RISC-V based system emulator](https://gitlab.com/luplab/lupv)
* [mini-rv32ima](https://github.com/cnlohr/mini-rv32ima) / [video: Writing a Really Tiny RISC-V Emulator](https://youtu.be/YT5vB3UqU_E)
* [RVVM](https://github.com/LekKit/RVVM)
* [RISCVBox](https://github.com/bane9/RISCVBox)
