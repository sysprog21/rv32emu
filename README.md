# RISC-V RV32I[MA] emulator with ELF support

`rv32emu` is an instruction set architecture (ISA) emulator implementing the 32 bit RISC-V processor model.

This repository is intended to replace [sysprog21/rv32emu](https://github.com/sysprog21/rv32emu), which is used in [Computer Architecture course](http://wiki.csie.ncku.edu.tw/arch/schedule). At the moment, the suffix `-next` is used to distinguish between the two repositories.

## Build and Verify

`rv32emu` relies on some 3rd party packages to be fully usable and to provide you full
access to all of its features. You need to have a working [SDL2 library](https://www.libsdl.org/)
on your target system.
* macOS: `brew install sdl2`
* Ubuntu Linux / Debian: `sudo apt install libsdl2-dev`

Build the emulator.
```shell
make
```

Run sample RV32I[MA] programs:
```shell
make check
```

Run [Doom](https://en.wikipedia.org/wiki/Doom_(1993_video_game)), the classical video game, via `rv32emu`:
```shell
make demo
```

The build script will then download data file for Doom automatically. SDL2 based window
should appear when Doom is loaded and executed.

## Customization

`rv32emu` is configurable, and you can modify `Makefile` to fit your expectations:
* `ENABLE_RV32M`: Standard Extension for Integer Multiplication and Division
* `ENABLE_RV32A`: Standard Extension for Atomic Instructions
* `Zicsr`: Control and Status Register (CSR)
* `Zifencei`: Instruction-Fetch Fence

Add `-D` to enable and `-U` to disable the specific ISA extensions.


## License
`rv32emu` is released under the MIT License.
Use of this source code is governed by a MIT license that can be found in the LICENSE file.
