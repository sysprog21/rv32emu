# WebAssembly build

`rv32emu` relies on
[Emscripten](https://emscripten.org/docs/getting_started/downloads.html) to
be compiled to WebAssembly. The target system should have Emscripten 3.1.51
installed.

`rv32emu` leverages the tail call optimization (TCO). WebAssembly execution
has been tested in Chrome with at least MAJOR 112, Firefox with at least
MAJOR 121 and Safari with at least version 18.2 since they support the tail
call feature. Check your browser version and update if necessary, or
install a compatible browser before proceeding.

Source your Emscripten SDK environment before make. For macOS and Linux users:
```shell
$ source ~/emsdk/emsdk_env.sh
```
Change the Emscripten SDK environment path if necessary.

At this point, you can build and start a web server service to serve
WebAssembly by running:

- User-mode emulation:
```shell
$ make CC=emcc start-web -j8
```

- System emulation:
```shell
$ make CC=emcc start-web ENABLE_SYSTEM=1 INITRD_SIZE=32 -j8
```

You would see the server's IP:PORT in your terminal. Copy and paste it to
the browser and you just access the index page of `rv32emu`.

You would see a dropdown menu which you can use to select the ELF
executable for user-mode emulation, select one and click the 'Run' button
to run it. For system emulation, click the 'Run Linux' button to boot
Linux.

## Hosted demo

Alternatively, you may want to view a hosted `rv32emu` since building takes
some time. The [landing page](https://sysprog21.github.io/rv32emu-demo/)
links to both modes:
- [User-mode emulation demo page](https://sysprog21.github.io/rv32emu-demo/user/)
- [System emulation demo page](https://sysprog21.github.io/rv32emu-demo/system/)

Each mode page has a navigation button that switches directly to the
other.
