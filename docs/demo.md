# Demos

## Doom
**source**: [doom_riscv](https://github.com/sysprog21/doom_riscv)

**commmad**: `make doom`

[Doom](https://en.wikipedia.org/wiki/Doom_(franchise)), a pioneering first-person shooter game developed by [id Software](https://en.wikipedia.org/wiki/Id_Software) in 1993, is known for its open-source code and vibrant community, it debuted innovations like genuine 3D graphics, networked multiplayer gameplay and the ability for players to create custom expansions. 

![Doom Gameplay](https://imgur.com/bLc5LG8.gif)

### Main Key Bindings (All key bindings are listed in the "READ THIS!" menu)
* Move Forward/Backward: Up Arrow Key/Down Arrow Key
* Move Left/Right: Comma(,) Key/Period(.) Key
* Turn Left/Right: Left Arrow Key/Right Arrow Key
* Shoot: Left Mouse Button or CTRL Key
* Sprint: Shift Key
* 1: Fist
* 3: Shotgun

### Music and sound effect(sfx)
Support music and sound effects.

## Quake
**source**: [quake-embedded](https://github.com/sysprog21/quake-embedded/)

**command**: `make quake`

[Quake](https://en.wikipedia.org/wiki/Quake_(series)) was created in 1996 as a successor of the highly successful first-person shooter game Doom, it is based on Doom's game engine and 3D graphics compatibility, and it enhanced the fast-paced gameplay and online multiplayer over the Internet. 

Build Instruction:
```shell
git clone https://github.com/sysprog21/quake-embedded.git && cd quake-embedded
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../port/boards/rv32emu/toolchain.cmake \
      -DCMAKE_BUILD_TYPE=RELEASE -DBOARD_NAME=rv32emu ..
make
```

![Quake Gameplay](https://imgur.com/gXKb7D0.gif)

### Default Key Bindings
* Move Forward/Backward: Up Arrow Key/Down Arrow Key
* Move Left/Right: Comma(,) Key/Period(.) Key
* Turn Left/Right: Left Arrow Key/Right Arrow Key
* Swim Up/Down: D Key/C Key
* Shoot: Left Mouse Button or CTRL Key
* Switch Weapon: Slash(/) Key
* Sprint: Shift Key

You may use the mouse to adjust the pitch and yaw angle

### Music and sound effect(sfx)
Support sound effects but not music currently because Quake needs a CD-ROM and the extracted pak file doesn't contain any music or bgm-related files.

### Limitations
* Mouse wheel input is not supported
* Music related functions in Quake are not implemented
