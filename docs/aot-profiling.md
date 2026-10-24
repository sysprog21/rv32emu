# Profiling T2C AOT execution

T2C collects LLVM bitcode during one run (`-C`), compiles it offline with
`tools/aot.py`, and binds the resulting native regions in later runs (`-A`).
[build.md](build.md) covers collection, compilation and deployment;
[codegen.md](codegen.md) covers the generated code. The reference results for
AOT against the interpreter, T1C and QEMU are in
[benchmark.md](benchmark.md); this document explains where AOT's time goes and
how to profile it.

## Where the time goes

In user mode, AOT doubles T1C on Dhrystone because a whole region compiles as
one LLVM function with guest registers in SSA values. In system mode, AOT
shortens the Linux boot by a tenth and stays within 4% of T1C on every
workload after it. Native sampling, hardware counters and the counters an
artifact prints at exit (`-A` reports them on stderr) explain the system
results:

* **Cold code.** Boot is where AOT pays: most kernel code runs only a few
  times, and AOT runs it natively from the first instruction instead of
  interpreting it until T1C compiles it. The Linux artifact holds about 1,060
  page graphs in 39 MiB; collection takes one boot, about 35 s, and
  compilation a few minutes with 32 jobs.
* **Instruction cache.** Kernel paths such as page faults and process
  creation enter a region about once every 18 guest instructions, and their
  cost is mostly instruction-cache misses: a page graph is one large
  function. Each memory access carries a slow path that publishes state and
  calls an MMU helper, and each back edge a deadline check. Marking those
  paths unlikely lets LLVM place them after the hot path, which cut
  instruction-cache misses by a third over a boot and 900 process launches.
* **Registers in memory.** System page graphs keep guest registers in
  `rv->X`. Promoting them to SSA forced a write-back at every exit, which
  doubled the artifact and made instruction-cache misses dominate boot, so
  hot loops run no faster than under T1C, whose register maps already hold
  them.
* **Binding.** Kernel code on global mappings is bound once for every
  address space, and a fence clears only the bindings of the address spaces
  it retires. About 1.5 million bindings remain over the same run, nearly all
  of them kernel entries displaced from the 4,096-slot binding cache; a
  bitmap of the pages the artifact covers turns away the 5.6 million lookups
  for code it does not cover.
* **The dispatcher.** Regions return to it at uncovered code, MMIO and helper
  exits, about 5.8 million times over the same run. Decoding user blocks,
  which every process maps at a new address, and freeing the decoded
  instructions of evicted blocks take another 10% of host time, as in T1C.

T1C's indirect-jump guards stay on during replay: turning them off while an
artifact was loaded sent every return in T1C code back to the dispatcher,
which doubled Dhrystone's time in Linux and added 29% to CoreMark's.

Approaches measured and not kept:

* Promoting guest registers to SSA in system regions, for the reasons above.
* Collecting every cold user block: user mappings differ between boots, so
  almost no collected PC was reused.
* LLVM's hot/cold splitting on top of the branch weights: it moved 44,000
  cold fragments out of the page graphs and changed no workload.
* The preserve_none convention in system regions, which speeds up user
  regions: every slow path calls into C, around which such code must save its
  live values, so the artifact grew 12% and ran no faster.
* A 16,384-slot binding cache, a larger cache of PCs absent from the
  artifact, a longer dispatch slice (`-s 4096`), and running T1C code before
  a bound AOT entry: none moved any workload beyond run-to-run noise.
* Select-based safe division: 3–4% slower than a rare exception branch.

## Profiling

Use function tracing to count transitions and isolate helpers, then native
sampling to inspect work inside compiled regions. Measure speed separately,
without either profiler. A region's time appears inside its caller unless the
region is also probed, so `rv_step` self time alone is not dispatcher cost.

```sh
uftrace record --force --no-libcall \
  -P 'rv_step|t2c_interpret_block|jit_translate' \
  -d build/aot/trace-runtime \
  build/rv32emu -A build/coremark.so build/riscv32/coremark
uftrace report -d build/aot/trace-runtime --sort self

perf record -e cycles:u -F 499 -o build/aot/native.perf -- \
  taskset -c 12 build/rv32emu -A build/coremark.so build/riscv32/coremark
perf report --stdio -i build/aot/native.perf --no-children --sort dso,symbol
```

Region symbols name their guest page and its hash; select them from the
library with `nm`, and keep the binaries unstripped so probes can attach.
High-frequency probes perturb execution: reduce the probed set rather than
reading traced wall time as production performance. Dynamic uftrace probes
did not survive a complete Linux boot here, so system hotspots come from
`perf` sampling. A `T1C_STATS=1` build adds dispatcher and decode counters,
which tell whether a slowdown comes from the native code or from around it.

To collect an artifact from a scripted guest session, the emulator must exit
through the guest's `poweroff`: leaving through the serial monitor (Ctrl-A x)
skips writing the collection manifest.

## Stable Linux images

A stable kernel image amortizes collection and optimization over many boots:
keep the artifact beside the kernel, run with `-A`, and use `-C` to refresh
the collection. The artifact must match the emulator, kernel image and memory
options; a rebuilt emulator needs a new artifact, though `tools/aot.py`'s
content-addressed object cache reuses identical compilation units. A stable
image does not imply stable code pages: Linux patches instructions and
replaces mappings, so page digests, FENCE.I and SFENCE.VMA checks stay on.
