# Vector Extension (RVV)

`make config` enables the RISC-V "V" extension, version 1.0, as `EXT_V`.
Selecting it also selects `EXT_F`, because vector loads and stores share
their opcode space with the floating-point ones.

## Scope

Implemented:

* configuration (`vsetvli`, `vsetivli`, `vsetvl`) and the `vl`, `vtype`,
  `vlenb`, `vstart`, `vxrm`, `vxsat`, and `vcsr` registers
* unit-stride, strided, indexed, segment, fault-only-first, whole-register,
  and mask loads and stores
* integer, fixed-point, multiply-accumulate, widening, narrowing, compare,
  mask, reduction, and permutation operations
* single-precision floating point, including the widening and narrowing
  conversions, computed with Berkeley SoftFloat
* masking and the tail-agnostic and mask-agnostic policies; agnostic
  elements are filled with ones
* `misa.V`

Not implemented:

* `SEW=64`. `vsetvli` rejects it with `vill`, so this is an ELEN=32 machine,
  comparable to `Zve32f`. Widening results and 64-bit load and store element
  widths are supported, and fractional LMUL follows the ELEN=64 limit
  (`LMUL >= SEW/64`).
* vector double-precision arithmetic, because the base ISA has no D
* `Zvfh` and `Zvfhmin`

`VLEN` is a power of two from 128 to 65536, set by `CONFIG_VLEN` in
`make config` or on the make line (`make VLEN=256`). It defaults to 128.

## Register rules

V 1.0 section 5 restricts how an instruction's register groups may overlap.
Most of those rules apply to many opcodes, so they are checked once, in the
prologue every vector handler already runs, rather than in each handler:

* a masked instruction cannot use `v0` as a data source or destination
* a mask destination overlaps a source group only at its lowest register
* one register cannot be read at two element widths (`.wv` forms, widening
  reductions, `vrgatherei16`)
* `vmsbf`, `vmsif`, `vmsof`, `viota`, and `vfslide1up` cannot overlap their
  source, and `vcpop`, `vfirst`, and the mask generators need `vstart = 0`
* the accumulating widening forms (`vwmacc*`, `vfwmacc*`, ...) read `vd` at
  2*SEW, so a narrow source cannot share a register with it

`scripts/gen-rvv-classes.py` derives which registers each opcode treats as
data or mask from `src/instructions.in` and writes `src/rv32_v_class.inc`.
`make` regenerates it, and `make check-decoder` fails when the committed copy
is stale.

A masked compare, carry form, reduction, or store cannot use `v0` as a data
source either: it reads `v0` as a mask and as data. QEMU accepts most of those
encodings; the specification reserves them, so the emulator raises
illegal-instruction. A masked load cannot write `v0`.

Where an encoding is reserved the emulator raises an illegal-instruction
exception, and it does not reject encodings the specification allows: a plain
widening result may overlap a source in the highest-numbered part of its group.

With `vstart >= vl` an instruction updates nothing, agnostic tail included.

## Tests

| Target | Covers | VLEN |
|--------|--------|------|
| `check-rvv-smoke` | instruction semantics | 128 |
| `check-rvv-compliance` | behavior a differential run found wrong, with hand-derived expectations | 128 |
| `check-rvv-trap` | illegal-instruction cases and the legal neighbors of each | any |
| `check-rvv-vlen` | `vlenb`, VLMAX, whole-register moves, last-lane accesses | any |

## Differential testing

The semantics were checked against QEMU 11 (`qemu-riscv32`, with
`rvv_ta_all_1s=true,rvv_ma_all_1s=true,rvv_vsetvl_x0_vill=true`) with
generated programs. Each case runs one instruction under a chosen SEW, LMUL,
`vl`, `vstart`, policy, rounding mode, and random register contents (including
NaNs, infinities, and denormals), then compares every vector register, `vl`,
`vtype`, `vxsat`, `fflags`, and the scalar result. Illegal encodings are
compared by whether each model traps. The comparison treats agnostic
elements and the unused tail of mask results as either undisturbed or all
ones, except when `vstart >= vl`, where nothing may change. The only
intended difference in which encodings trap is the `v0` rule above.
