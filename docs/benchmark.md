# Benchmarks

This document gives two reference tables, one per execution mode, explains
what drives each result, and describes how to measure a change. Every number
below comes from one AMD Ryzen Threadripper 2990WX running Ubuntu 24.04, with
each emulator pinned to one CPU, and from one source revision. Results depend
on the host, so judge a change only against a baseline measured on the same
host in the same session (see [Comparing two builds](#comparing-two-builds)).

The execution modes are:

* **Interpreter**: `interpreter_defconfig` (user) or
  `system_interpreter_defconfig` (system).
* **T1C**: the tier-1 JIT, `defconfig` or `system_defconfig`. It compiles a
  block once it is hot, at run time.
* **AOT**: tier 2, `jit_defconfig` or `system_jit_defconfig`. One run collects
  LLVM bitcode with `-C`, `tools/aot.py` compiles it offline at O3, and later
  runs load the artifact with `-A`. Code the artifact does not cover runs in
  T1C. See [build.md](build.md) for the workflow.

## User mode

Dhrystone and CoreMark guests from `make artifact`, three runs of
`tests/bench.py` each. Each AOT artifact was collected from a run of the same
program, which is the intended use: an artifact serves the program it was
trained on.

| Mode | Dhrystone (DMIPS) | vs. interpreter | CoreMark (iterations/s) | vs. interpreter |
| --- | ---: | ---: | ---: | ---: |
| Interpreter | 2,496 | 1.0x | 2,061 | 1.0x |
| T1C | 20,594 | 8.3x | 18,875 | 9.2x |
| AOT | 41,861 | 16.8x | 21,135 | 10.3x |

**Interpreter to T1C (8-9x).** The interpreter dispatches every guest
instruction through a handler, and its speed moves with code layout: on
Dhrystone this build and master execute the same host instructions, yet
master runs about 20% faster because its handlers' indirect jumps mispredict less. T1C replaces a hot block with host code, keeps
guest registers in host registers across a block and across loops and small
regions of blocks, links direct branches block to block, and guards the most
frequent targets of an indirect jump inline. CoreMark spends its time in a few
tight loops (list search, matrix multiply, a state machine), which those
register maps cover, so it gains more than Dhrystone.

**T1C to AOT (2x on Dhrystone, 12% on CoreMark).** AOT compiles each region,
a trace of blocks up to thousands of instructions long, as one LLVM function,
so optimization works across what T1C translates block by block: guest
registers live in SSA values across the region, stack displacements fold into
host addressing modes, and an indirect jump with a dominant target continues
into that target without leaving the region. Dhrystone, which is call- and
copy-heavy, gains most. CoreMark's loops already run close to T1C's limit and
leave their region about once every 130 instructions; regions jump to each
other without saving or restoring host registers, which made CoreMark 3%
faster. Against the runtime tier-2 compiler that AOT
replaces, measured in paired runs on this host, AOT is 14% faster on
Dhrystone and 51% faster on CoreMark, and it spends no host time compiling
during the run.

Two programs show how the tiers divide the work in a wider sample, twelve
programs from `build/riscv32` run once each per tier (median of three):

| Program | Interpreter (s) | T1C (s) | AOT (s) |
| --- | ---: | ---: | ---: |
| sha512 | 204 | 18 | 11 |
| qsort | 80 | 14 | 6.4 |

Across the twelve, T1C runs 3.4x to 13x faster than the interpreter, and AOT
1.2x to 2.2x faster than T1C. T1C spends 85% to 99% of its time in generated
code and under 8% compiling, so the tier pays for itself within the first
second of a program. AOT gains least on short loops that T1C already holds in
host registers, and most where regions span calls: qsort's comparator
returns to several call sites, and each return used to leave the region. A
region now ends a return in a switch over the return points it traced.
Regions also follow the edges T1C ran natively, so a loop T1C compiled early
is not cut out of the region around it.

## System emulation

In system mode the guest's clock derives from the emulated cycle count, so
the scores a guest prints for itself come out the same under every engine.
`tests/system-bench.sh` instead measures host wall-clock time: booting the
prebuilt Linux image to the login prompt, then four workloads in the guest:
Dhrystone and CoreMark from the root file system, 300 runs of `/bin/true`
(process creation, with the page-table fences that come with it), and a
`dd | gzip | md5sum` pipeline. Medians of five interleaved rounds, in
milliseconds. QEMU 11.0.2 runs the same kernel and initramfs on `virt` with
OpenSBI (`-M virt -accel tcg -m 512M`), so its boot also covers firmware. The
AOT artifact was collected from one run of the same workloads.

| Engine | Boot | Dhrystone | CoreMark | fork+exec | gzip |
| --- | ---: | ---: | ---: | ---: | ---: |
| QEMU 11.0.2 | 2,486 | 12,155 | 2,823 | 3,799 | 736 |
| rv32emu T1C | 1,218 | 6,021 | 2,283 | 2,995 | 600 |
| rv32emu AOT | 1,106 | 6,016 | 2,318 | 3,162 | 606 |

**T1C against QEMU.** T1C takes less time on every workload: half the time
to boot and on Dhrystone, 19% less on CoreMark, 21% less on process creation
and 18% less on gzip. Five properties of its system mode carry these results:

* Compiled code survives page-table fences. A fence retires only the address
  spaces in which a page-table entry of a code page changed, and kernel code
  reached through global mappings is shared by every address space. Linux
  fences mappings about ten times per process, which would otherwise retire
  all compiled code each time.
* Kernel code survives instruction fences too. Linux issues FENCE.I for every
  program it starts, about 600 times in a boot, while its own text never
  changes: T1C keeps the kernel's code unless a store reached one of its
  pages (see [fencei.md](fencei.md)). Discarding it at each FENCE.I made the
  boot 45% longer.
* Memory accesses stay short. Each dTLB entry has RAM tags that fold the
  privilege context, permission and dirty state into one comparison; guest
  registers stay in host registers across a hit, and slow paths sit after
  the block, so a hit falls through to the next instruction.
* Little stays interpreted. Page-fault handlers and interrupts run through the
  dispatcher as compiled code, AMOs and CSR instructions compile as calls
  into C, and WFI skips to the next timer deadline.
* The kernel's working set stays compiled: the block cache holds 32,768
  blocks with a 64 MiB code buffer, and blocks compile after 16 dispatches,
  or 2 for a loop.

**AOT against T1C.** AOT boots in 9% less time than T1C. A boot runs a
large amount of kernel code only a few times: T1C interprets such code until
it turns hot and then pays to compile it, while AOT has native code for it
from the first instruction. After boot the two tiers stay within 6% of each
other on every workload:

* Dhrystone and CoreMark spend their time in hot user loops, which T1C
  compiles early. System AOT regions keep guest registers in memory rather
  than in SSA values: promoting them required a write-back at every exit of a
  page graph, which doubled the Linux artifact and made instruction-cache
  misses dominate boot.
* Process creation runs branchy kernel paths, entering a region about once
  every 18 guest instructions, and its cost is mostly instruction-cache
  misses. Two changes brought AOT close to T1C there. Every slow path of a
  page graph is marked unlikely, so LLVM lays it out after the hot path: that
  cut instruction-cache misses by a third and the time by 14%. Kernel code is
  bound once for every address space instead of again in each new process,
  which removed another 8%.

AOT therefore suits deployments that boot one image many times and care
about startup; after boot either tier serves.

## Measuring

### Running the suites

`tests/bench.py` runs the prebuilt Dhrystone and CoreMark guests against
whatever `build/rv32emu` was built as, and rejects CoreMark results whose
checksum fails, so a miscompiling JIT cannot post a score:

```sh
make interpreter_defconfig && make && python3 tests/bench.py
make defconfig && make && python3 tests/bench.py --label T1C
```

For AOT, `.ci/prepare-aot-bench.py --emulator build/rv32emu --output
build/aot-launcher` collects and compiles an artifact per benchmark and writes
a launcher to pass as `--emu`. In system mode, run `tests/system-bench.sh
build/rv32emu 5` after `make artifact`. The script starts the emulator it is
given with only `-k` and `-i`, so measure AOT or QEMU through a wrapper script
that adds `-A <library>` or maps those two options onto QEMU's. Collect the
artifact with `-C` in a session that ends with the guest's `poweroff`: leaving
through the serial monitor, as the script does, skips writing the collection
manifest. `tests/system-boot.py` measures boot to login alone and accepts a
QEMU binary for comparison.
`tests/interpreter_bench.py` compares the interpreter with libriscv on the
BYTEmark (nbench) workloads.

### Comparing two builds

Scores from different machines, or from one shared machine at different times,
differ by more than most changes do. Build both revisions in the same mode and
let `--baseline` measure them side by side:

```sh
git worktree add ../rv32emu-base origin/master
make -C ../rv32emu-base defconfig && make -C ../rv32emu-base
python3 tests/bench.py --label T1C --runs 6 --parallel 2 \
    --baseline ../rv32emu-base/build/rv32emu --emu build/rv32emu
```

Each pair runs the baseline and the candidate at the same time, so both see
the same interference, and alternates which starts first. The report gives the
geometric mean of the per-pair ratios with a 95% confidence interval, and
calls a change faster or slower only when the interval excludes zero and the
change exceeds `--threshold` (2% by default); a noisy host yields "no
significant change" rather than a false verdict.

When reading results:

* Address space layout randomization moves the emulator and its JIT buffer,
  and two builds of the same source can differ by several percent through
  code layout alone. Repeat pairs rather than trust one run, and prefer host
  instruction counts (`perf stat -e instructions:u`) for small changes.
* An identical-binary (A/A) comparison should report no significant change; if
  it does not, the host is too noisy.
* CoreMark's fixed 80,000-iteration guest finishes in under ten seconds on the
  JIT tiers and prints a minimum-duration warning; it is compared by elapsed
  time, not reported as an official score.

`T1C_STATS=1` builds print counters of block lookups, compilations, native
entries, fences and retirements at exit, and attribute interpreted block
entries to the first opcode that blocked translation.

## Continuous benchmarking

`.github/workflows/benchmark.yml` measures the interpreter, T1C and AOT. For a
pull request, each mode is built twice on one runner, from the pull request
merged into master and from that master commit, and the comparison above posts
its table as a pull request comment. Each push to master records all three
modes on the [dashboard](https://sysprog21.github.io/rv32emu-bench/), which
shows long-term trends only: GitHub assigns runners of different speeds.
