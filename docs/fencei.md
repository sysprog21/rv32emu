# FENCE.I and self-modifying code

User-mode and system-mode interpreters invalidate decoded instructions after
FENCE.I. System JIT builds also discard compiled code in every tier.

User-mode JIT builds currently do not invalidate decoded or compiled code
on FENCE.I. Run self-modifying guest programs with an interpreter build
(`make interpreter_defconfig && make`). The guest regression suite explicitly
skips `jit-fencei` for user JIT builds and reports this limitation.

`tests/jit-fencei.S` warms a function, rewrites its first instruction, issues
FENCE.I and requires the new result. System builds need ELF loading enabled
to run this directly loaded guest regression.
