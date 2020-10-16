# RISC-V RV32I[MA] emulator with ELF support

`rv32emu` is an instruction set emulator implementing the 32 bit RISCV-V processor model.

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

Run Doom, the classical video game, via `rv32emu`:
```shell
make demo
```

The build script will then download data file for Doom automatically. SDL2 based window
should appear when Doom is loaded and executed.

## License
`rv32emu` is released under the MIT License.
Use of this source code is governed by a MIT license that can be found in the LICENSE file.
