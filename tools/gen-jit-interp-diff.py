"""Generate tests/jit-interp-diff.S.

Applies every RV32IM arithmetic form to each ordered pair from a table of
awkward operands and folds the results into per-class accumulators, so that
tier-1 generated code can be compared against the interpreter.

The expected accumulator values below are what the interpreter produces. After
changing the operand table or the instruction lists they must be recomputed:

    python3 tools/gen-jit-interp-diff.py /tmp/sweep.S     # GOLD= to omit
    riscv32-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib -static \
        -Wl,-e,_start -o /tmp/sweep /tmp/sweep.S
    build/rv32emu -q -d - /tmp/sweep    # read x9, x18, x19, x24, x25

with an interpreter build, then paste them into DEFAULT_GOLD.
"""

import os
import sys

DEFAULT_GOLD = "0x2aff1ce5,0xe45a9e79,0xdd880af2,0x853ce706,0x0eb28e20"

VALUES = [
    0,
    1,
    -1,
    2,
    -2,
    0x7FFFFFFF,
    -0x80000000,
    0x5A5A5A5A,
    -0x5A5A5A5B,
    65536,
    -65536,
    31,
    32,
    33,
    0xDEADBEEF - (1 << 32),
    7,
]
REG_REG = ["add", "sub", "sll", "slt", "sltu", "xor", "srl", "sra", "or", "and"]
M_OPS = ["mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu"]
IMM_OPS = ["addi", "slti", "sltiu", "xori", "ori", "andi"]
SH_OPS = ["slli", "srli", "srai"]
LOADS = [("lb", 1), ("lbu", 1), ("lh", 2), ("lhu", 2), ("lw", 4)]

out = []
w = out.append
w("/* Generated differential sweep: interpreter vs tier-1 JIT.")
w(" * Kept as small hot blocks so the translator actually compiles them. */")
w("    .text")
w("    .global _start")


def mix(acc, res):
    """acc = rotl(acc, 1) + res. Addition does not self-invert, so repeating
    an identical iteration cannot cancel the accumulator back to zero."""
    assert acc != res
    w("    slli t5, %s, 1" % acc)
    w("    srli t6, %s, 31" % acc)
    w("    or %s, t5, t6" % acc)
    w("    add %s, %s, %s" % (acc, acc, res))


w("_start:")
for r in ("s1", "s2", "s3", "s8", "s9"):
    w("    li %s, 0" % r)
w("    li s7, %d" % len(VALUES))
w("    li s0, 120")
w("outer:")
w("    la s4, values")
w("    li s5, 0")
w("loop_i:")
w("    slli t3, s5, 2")
w("    add t4, s4, t3")
w("    lw t0, 0(t4)")
w("    li s6, 0")
w("loop_j:")
w("    slli t3, s6, 2")
w("    add t4, s4, t3")
w("    lw t1, 0(t4)")
for op in REG_REG:
    w("    %s t2, t0, t1" % op)
    mix("s1", "t2")
for op in M_OPS:
    w("    %s t2, t0, t1" % op)
    mix("s3", "t2")
for imm in (-2048, -1, 0, 1, 2047):
    for op in IMM_OPS:
        w("    %s t2, t0, %d" % (op, imm))
        mix("s2", "t2")
for sh in (0, 1, 31):
    for op in SH_OPS:
        w("    %s t2, t0, %d" % (op, sh))
        mix("s2", "t2")
# Memory: store both operands, reload through every width and sign.
w("    la t3, scratch")
w("    sw t0, 0(t3)")
w("    sw t1, 4(t3)")
w("    sh t1, 8(t3)")
w("    sb t0, 10(t3)")
for op, _ in LOADS:
    for off in (0, 4):
        w("    %s t2, %d(t3)" % (op, off))
        mix("s8", "t2")
for op, _ in LOADS:
    w("    %s t2, 8(t3)" % op)
    mix("s8", "t2")
# Branches: both directions on signed and unsigned comparisons.
for i, (br, inv) in enumerate(
    [("beq", "bne"), ("blt", "bge"), ("bltu", "bgeu")]
):
    w("    %s t0, t1, brA%d" % (br, i))
    w("    addi s9, s9, 1")
    w("brA%d:" % i)
    w("    %s t0, t1, brB%d" % (inv, i))
    w("    addi s9, s9, 3")
    w("brB%d:" % i)
    mix("s9", "t0")
w("    addi s6, s6, 1")
w("    blt s6, s7, loop_j")
w("    addi s5, s5, 1")
w("    blt s5, s7, loop_i")
w("    addi s0, s0, -1")
w("    bnez s0, outer")
import os

gold = os.environ.get("GOLD", DEFAULT_GOLD)
if gold:
    w("")
    w("    /* Compare against the interpreter's results. */")
    for reg, val in zip(("s1", "s2", "s3", "s8", "s9"), gold.split(",")):
        w("    li t0, %s" % val)
        w("    bne %s, t0, fail" % reg)
    w("    li a0, 1")
    w("    la a1, passed")
    w("    li a2, 31")
    w("    li a7, 64")
    w("    ecall")
w("    li a0, 0")
w("    j exit")
w("fail:")
w("    li a0, 1")
w("exit:")
w("    li a7, 93")
w("    ecall")
w("    .section .rodata")
w("passed:")
w('    .ascii "JIT matches the interpreter OK\\n"')
w("    .section .data")
w("    .balign 4")
w("scratch:")
w("    .space 32")
w("    .section .rodata")
w("    .balign 4")
w("values:")
for v in VALUES:
    w("    .word %d" % v)
open(sys.argv[1], "w").write("\n".join(out) + "\n")
body = sum(1 for l in out if l.startswith("    ") and not l.startswith("    ."))
print("instructions in source:", body)
