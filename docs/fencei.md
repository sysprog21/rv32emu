# FENCE.I and self-modifying code

User-mode and system-mode interpreters invalidate decoded instructions after
FENCE.I. System JIT builds also discard compiled code in every tier, with one
exception: kernel code reached through global mappings survives a FENCE.I
unless a write reached one of its physical pages since the previous one.
Linux issues FENCE.I for every program it starts while its own text stays
unchanged, and discarding the kernel's decoded, compiled and bound code each
time made T1C's boot 45% longer.

Every write to such a page is seen. Its dTLB entries are never dirty and get
no write tag, so every native store path, which needs one or the other, falls
back to the C store path, which notes the write along with stores from the
interpreter. Page-table accessed and dirty updates, device DMA (payloads,
status bytes and used rings) and debugger writes report the ranges they
write. A noted write makes the next FENCE.I discard everything, as before.
`tests/system-fence-shared.S` rewrites a compiled global page through its own
mapping and an alias, from compiled and cold stores, and requires each
FENCE.I to expose the new instruction. AOT collection keeps the full FENCE.I:
it yields page graphs that replay faster.

User-mode JIT builds currently do not invalidate decoded or compiled code
on FENCE.I. Run self-modifying guest programs with an interpreter build
(`make interpreter_defconfig && make`). The guest regression suite explicitly
skips `jit-fencei` for user JIT builds and reports this limitation.

`tests/jit-fencei.S` warms a function, rewrites its first instruction, issues
FENCE.I and requires the new result. System builds need ELF loading enabled
to run this directly loaded guest regression.
