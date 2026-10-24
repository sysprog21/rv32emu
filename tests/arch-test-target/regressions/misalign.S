/* Check floating-point misalignment fallback, including f0 and RVC forms,
 * and ensure discarded integer loads leave x0 zero.
 */
.global _start, tohost, begin_signature, end_signature
.option norvc
_start:
    la t0, scratch
    li t1, 0x89abcdef
    fmv.w.x f0, t1
    fsw f0, 1(t0)
    flw f1, 1(t0)
    fmv.x.w t2, f1
    bne t1, t2, fail

    /* A two-byte-aligned word uses the halfword fallback. */
    fsw f0, 2(t0)
    flw f0, 2(t0)
    fmv.x.w t2, f0
    bne t1, t2, fail

    addi s0, t0, 1
    mv sp, s0
    fmv.w.x f8, t1
.option rvc
    /* Isolate each instruction in a block so JIT eligibility is exercised. */
    j .Lstore
.Lstore:
    c.fsw f8, 0(s0)
    j .Lload
.Lload:
    c.flw f9, 0(s0)
    j .Lstore_sp
.Lstore_sp:
    c.fswsp f9, 0(sp)
    j .Lload_sp
.Lload_sp:
    c.flwsp f0, 0(sp)
    j .Lverify
.option norvc
.Lverify:
    fmv.x.w t2, f0
    bne t1, t2, fail

    lw zero, 1(t0)
    sw zero, 8(t0)
    lw t2, 8(t0)
    bnez t2, fail
    lh zero, 1(t0)
    sw zero, 8(t0)
    lw t2, 8(t0)
    bnez t2, fail
    lhu zero, 1(t0)
    sw zero, 8(t0)
    lw t2, 8(t0)
    bnez t2, fail

    li t1, 1
    j done
fail:
    li t1, 3
done:
    la t0, tohost
    sw t1, 0(t0)
1:  j 1b

.data
.balign 8
tohost: .word 0, 0
begin_signature: .word 0
end_signature:
.balign 4
scratch: .space 16
