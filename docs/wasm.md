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
$ make wasm_defconfig
$ make CC=emcc start-web -j8
```

- System emulation:
```shell
$ make wasm_system_defconfig
$ make CC=emcc start-web -j8
```

You would see the server's IP:PORT in your terminal. Copy and paste it to
the browser and you just access the index page of `rv32emu`.

You would see a dropdown menu which you can use to select the ELF
executable for user-mode emulation, select one and click the 'Run' button
to run it. For system emulation, click the 'Run Linux' button to boot
Linux.

## T1C compilation

The Wasm presets enable T1C. Hot blocks become small WebAssembly modules
that share the emulator's linear memory. Integer arithmetic, multiply/divide,
and branches use Wasm instructions. User-mode RAM accesses and system-mode
dTLB hits use inline loads/stores, including fused runs of word loads and
stores and post-increment pairs. A fused run is guarded as a whole
before any member executes, so a miss replays it through the interpreter.
Misses, MMIO, alignment exceptions, and other operations retain the existing
interpreter handlers.

Blocks whose hot path needs no helpers keep loop-carried registers and the
cycle counter in Wasm locals. Such a block also absorbs up to 15 of its
cached successors that need no helpers either (in system mode, those on the
same page): a dispatch loop runs whichever of them the next PC selects, so
loops spanning several blocks keep their registers in locals instead of
crossing a module boundary at every edge. The module keeps copies of the
absorbed blocks' records, since the block cache may free those blocks at
any time. A module runs at most 256 blocks per function call, then
tail-calls its compiled successor directly; the chain returns to the
dispatcher every 64K guest cycles, or when the hart halts, so interrupts
and browser yields still run. Guard failures materialize the precise
register and cycle state before executing the original instruction.

The browser uses cooperative message tasks with approximately 8 ms host
time slices, avoiding nested-timer delays. The clock is sampled after 32K
guest cycles, after 256 block dispatches (for loops that retire few cycles),
and after each module compilation, so its cost follows the work done rather
than the number of blocks dispatched. A larger guest-cycle cap lets
faster backends do more work per slice instead of stopping at a fixed
display-frame throughput ceiling. While the page is hidden, the loop falls
back to animation frames, which the browser pauses, so a background tab does
not keep a core busy.

Generated functions occupy a bounded cache. Cache flushes and VM cleanup
release their function-table slots; system-mode address-space invalidation
retires the corresponding modules, whose slots and code budget are reused.
Runtime compilation blocked by browser policy falls back to interpretation.
LLVM tier-2 compilation remains native only. Select **Interpreter Only**
with `make config` to disable T1C.

Guest RAM comes from Wasm memory, which grows zero-filled, so a VM's first
run keeps the pages its guest never touches out of RAM. A reboot, or a
later program in the same page, clears guest RAM in full and makes it
resident. The pthread pool holds four workers, enough for the audio
threads, since each idle worker costs several megabytes.

Run the backend fixture with an activated Emscripten SDK and Node.js:

```shell
make check-jit-wasm
```

This opt-in check compiles and executes generated modules in user and system
layouts, including shared memory, RAM bounds, dTLB permissions and dirty
bits, and builds without the compressed instruction extension. Every
lowered register operation, compressed form included, is compared with an
independent model over boundary operands, and fused groups are checked on
both their inline and replay paths.

## Profiling

Build with `JIT_WASM_PROFILE=1` to retain readable function names and collect
per-VM T1C statistics. The flag participates in the effective build stamp;
normal builds have no profiling counters. Generated block names include the
guest PC and address-space context.

```shell
make CC=emcc JIT_WASM_PROFILE=1 -j8
make CC=emcc JIT_WASM_PROFILE=1 prepare-web
python3 tools/profile-wasm.py --guest build/riscv32/coremark \
    --seconds 10 --output build/coremark-profile
```

The tool serves `demo/` with isolation headers and captures Chromium CPU
samples using Playwright. It writes `.json` and `.log` files, plus a
`.cpuprofile` unless `--benchmark` disables sampling.
The JSON lists exclusive sampled time per function, whether the guest
finished, and T1C counters emitted at VM cleanup: compiled blocks/bytes,
translation and module compilation time, executed guest cycles, hot-edge
lookups, and helper calls by opcode. Fused helper counts measure calls, not
individual guest instructions. A timed-out capture stops the VM cleanly.

Use `--mode system` to profile a running Linux VM, or `--url` for an already
served demo page. Install Playwright as shown in the browser-check section
below. CPU samples cover the selected capture interval; T1C counters cover
the VM lifetime. Compare elapsed timings with profiling disabled, using the
same guest for both builds. Pass `--benchmark` to disable CPU sampling and
record elapsed time; only compare completed runs (`completed: true` in the
JSON). The JSON also preserves terminal output for result validation.

## Hosted demo

Alternatively, you may want to view a hosted `rv32emu` since building takes
some time. The [landing page](https://sysprog21.github.io/rv32emu-demo/)
links to both modes:
- [User-mode emulation demo page](https://sysprog21.github.io/rv32emu-demo/user/)
- [System emulation demo page](https://sysprog21.github.io/rv32emu-demo/system/)

Each mode page has a navigation button that switches directly to the
other.

## Browser regression checks

After `make prepare-web`, the browser test serves `demo/` without isolation
headers to exercise the service-worker bootstrap used by the hosted demo:

```shell
python3 -m venv build/venv
. build/venv/bin/activate
python3 -m pip install playwright
python3 -m playwright install chromium
python3 tests/test-wasm-demo.py --mode user --expect-jit
```

Use `--mode system` for a system build. User checks run `hello.elf` and verify
that jump, countdown (full-size and compressed branch), and indirect-jump
loops yield and stop. System checks boot Linux to its login prompt and check
for startup crashes. Use `--mode both` when both modes have been staged. CI
runs these checks with Chromium.
