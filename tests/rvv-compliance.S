/*
 * Regression checks for RVV behavior that a differential run against a
 * reference implementation found wrong.
 *
 * Every expected value below comes from the definition in the V 1.0
 * specification, worked out by hand, not from any implementation's output.
 * Like tests/rvv-smoke.S the program assumes VLEN=128 (sixteen SEW=8 lanes).
 *
 * A failing case exits with its own status and prints "RVV compliance FAIL".
 */
    /* Nothing sets gp in this freestanding program, so keep la PC-relative. */
    .option norelax

    .equ SYSWRITE, 64
    .equ SYSEXIT, 93

/* Fail with status id unless reg equals imm. */
.macro check id, reg, imm
    li   s1, \id
    li   t6, \imm
    bne  \reg, t6, fail
.endm

/* Load a whole vector register from a label, independent of vl and vtype. */
.macro ldv vreg, label
    la   a1, \label
    vl1re8.v \vreg, (a1)
.endm

/* Splat a byte over a whole register through SEW=8, VLMAX, then restore vl. */
.macro fillb vreg, byte
    vsetvli t0, zero, e8, m1, tu, mu
    li   t0, \byte
    vmv.v.x \vreg, t0
.endm

    .data
    .balign 16
bytes16:                         /* 0, 1, ..., 15 */
    .byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
lo_a:                            /* vasubu lhs: 0x01, then 0x00 */
    .byte 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00
    .byte 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00
hi_b:                            /* vasubu rhs: 0x80 everywhere */
    .fill 16, 1, 0x80
three:                           /* vwmaccsu vs2 */
    .fill 16, 1, 0x03
neg3:                            /* 0xfd = -3 as a signed byte */
    .fill 16, 1, 0xfd
neg2:                            /* 0xfe = -2 as a signed byte */
    .fill 16, 1, 0xfe
max_i8:                          /* 0x7f */
    .fill 16, 1, 0x7f
wide2:                           /* two 64-bit elements, hi:lo */
    .word 0x28f9ea12, 0x7f800000, 0x7b4a65eb, 0x00485e3b
ones:
    .fill 16, 1, 0xff
scal_wide:                       /* 64-bit scalar 0x8fc3372b_00000080 */
    .word 0x00000080, 0x8fc3372b, 0, 0
carry_src:                       /* e32 lanes 0xffffffff, 1 */
    .word 0xffffffff, 0x00000001, 0, 0
seg_src:                         /* 0..47 for the nf=3 segment load */
    .byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    .byte 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    .byte 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47
zero_mem:
    .fill 16, 1, 0
    .balign 16
out:
    .space 64
ok_msg:
    .ascii "RVV compliance OK\n"
    .equ ok_len, . - ok_msg
fail_msg:
    .ascii "RVV compliance FAIL\n"
    .equ fail_len, . - fail_msg

    .text
    .global _start
_start:
    csrw vstart, zero

    /* vasubu subtracts at SEW+1 bits before the halving shift. With round-
     * to-nearest-up, 1 - 128 = -127 averages to 0xc1 and 0 - 128 to 0xc0;
     * dropping the borrow gave 0x41 and 0x40.
     */
    vsetivli zero, 16, e8, m1, tu, mu
    csrwi vxrm, 0
    ldv  v16, lo_a
    ldv  v24, hi_b
    vasubu.vv v8, v16, v24
    la   a2, out
    vse8.v v8, (a2)
    lbu  t1, 0(a2)
    check 1, t1, 0xc1
    lbu  t1, 1(a2)
    check 2, t1, 0xc0
    li   t0, 0x80
    vasubu.vx v8, v16, t0
    vse8.v v8, (a2)
    lbu  t1, 0(a2)
    check 3, t1, 0xc1

    /* vwmaccsu is signed(vs1|rs1) * unsigned(vs2); vwmaccus is unsigned(rs1)
     * * signed(vs2). The operands were swapped.
     */
    vsetivli zero, 4, e16, m2, tu, mu
    vmv.v.i v8, 0
    vsetivli zero, 4, e8, m1, tu, mu
    ldv  v16, three
    ldv  v24, neg2
    vwmaccsu.vv v8, v24, v16     /* -2 * 3 */
    vsetivli zero, 4, e16, m2, tu, mu
    vse16.v v8, (a2)
    lhu  t1, 0(a2)
    check 4, t1, 0xfffa
    vmv.v.i v8, 0
    vsetivli zero, 4, e8, m1, tu, mu
    li   t0, 0xfe
    vwmaccsu.vx v8, t0, v16      /* signed(0xfe) * 3 */
    vsetivli zero, 4, e16, m2, tu, mu
    vse16.v v8, (a2)
    lhu  t1, 0(a2)
    check 5, t1, 0xfffa
    vmv.v.i v8, 0
    vsetivli zero, 4, e8, m1, tu, mu
    ldv  v16, neg3
    vwmaccus.vx v8, t0, v16      /* unsigned(0xfe) * signed(0xfd) */
    vsetivli zero, 4, e16, m2, tu, mu
    vse16.v v8, (a2)
    lhu  t1, 0(a2)
    check 6, t1, 0xfd06

    /* Rounding must not wrap inside the element width: 0x7f >> 2 under
     * round-to-nearest-even is 32, not -32.
     */
    vsetivli zero, 16, e8, m1, tu, mu
    csrwi vxrm, 1
    ldv  v16, max_i8
    vssra.vi v8, v16, 2
    vse8.v v8, (a2)
    lbu  t1, 0(a2)
    check 7, t1, 0x20

    /* Narrowing shifts by a zero-extended 5-bit immediate: 18 stayed 18
     * rather than becoming -14 (50 after masking to six bits at SEW=32).
     */
    vsetivli zero, 2, e32, m1, tu, mu
    csrwi vxrm, 0
    ldv  v16, wide2
    vnsra.wi v8, v16, 18
    vse32.v v8, (a2)
    lw   t1, 0(a2)
    check 8, t1, 0x0a3e
    lw   t1, 4(a2)
    check 9, t1, 0x178eded2

    /* A wide value above the narrow maximum saturates and sets vxsat. */
    csrw vxsat, zero
    vnclip.wi v8, v16, 0         /* 0x7f800000_28f9ea12 > INT32_MAX */
    vse32.v v8, (a2)
    lw   t1, 0(a2)
    check 10, t1, 0x7fffffff
    csrr t1, vxsat
    check 11, t1, 1

    /* The rounding increment must not wrap at 64 bits: all-ones shifted by 1
     * with round-to-nearest-up is 2^63, which saturates, not zero.
     */
    csrw vxsat, zero
    ldv  v16, ones
    vnclipu.wi v8, v16, 1
    vse32.v v8, (a2)
    lw   t1, 0(a2)
    check 12, t1, 0xffffffff
    csrr t1, vxsat
    check 13, t1, 1

    /* Widening reduction at SEW=32 has a 64-bit result, and with no active
     * element the result is still vs1[0].
     */
    vsetivli zero, 2, e32, m1, tu, mu
    fillb v8, 0xaa
    vsetivli zero, 2, e32, m1, tu, mu
    vmv.v.i v0, 0
    ldv  v16, ones
    ldv  v24, scal_wide
    vwredsum.vs v8, v16, v24, v0.t
    vs1r.v v8, (a2)
    lw   t1, 0(a2)
    check 14, t1, 0x80
    lw   t1, 4(a2)
    check 15, t1, 0x8fc3372b
    /* Unmasked: 0xffffffff + 1 carries into the high word. */
    vmv.v.i v24, 0
    ldv  v16, carry_src
    vwredsumu.vs v8, v16, v24
    vs1r.v v8, (a2)
    lw   t1, 0(a2)
    check 16, t1, 0
    lw   t1, 4(a2)
    check 17, t1, 1

    /* A reduction with vl=0 performs no update, floating point included. */
    fillb v8, 0xaa
    vsetivli zero, 0, e32, m1, tu, mu
    vfredmax.vs v8, v16, v24
    vfredosum.vs v8, v16, v24
    vsetivli zero, 4, e32, m1, tu, mu
    vse32.v v8, (a2)
    lw   t1, 0(a2)
    check 18, t1, 0xaaaaaaaa

    /* vslidedown by an offset near UINT32_MAX reads past VLMAX: zero, not a
     * wrapped low element.
     */
    vsetivli zero, 4, e16, m1, tu, mu
    ldv  v16, bytes16
    fillb v8, 0xaa
    vsetivli zero, 4, e16, m1, tu, mu
    li   t0, -1
    vslidedown.vx v8, v16, t0
    vse16.v v8, (a2)
    lhu  t1, 0(a2)
    check 19, t1, 0
    lhu  t1, 2(a2)
    check 20, t1, 0

    /* A resumed vslideup keeps the architectural offset: with vstart 3 and
     * offset 1, element 3 takes source element 2.
     */
    vsetivli zero, 8, e8, m1, tu, mu
    ldv  v16, bytes16
    fillb v8, 0xaa
    vsetivli zero, 8, e8, m1, tu, mu
    li   t0, 3
    csrw vstart, t0
    vslideup.vi v8, v16, 1
    vse8.v v8, (a2)
    lbu  t1, 0(a2)
    check 21, t1, 0xaa
    lbu  t1, 3(a2)
    check 22, t1, 2

    /* Elements before vstart are untouched and vstart ends up zero. */
    fillb v8, 0xaa
    vsetivli zero, 8, e8, m1, tu, mu
    csrwi vstart, 2
    vadd.vi v8, v16, 1
    csrr t1, vstart
    check 23, t1, 0
    vse8.v v8, (a2)
    lbu  t1, 1(a2)
    check 24, t1, 0xaa
    lbu  t1, 2(a2)
    check 25, t1, 3

    /* With vstart >= vl nothing is updated: not the body, the prestart
     * elements, or the tail, even with tail-agnostic set.
     */
    vsetivli zero, 16, e8, m1, tu, mu
    fillb v8, 0xaa
    vsetivli zero, 4, e8, m1, ta, ma
    csrwi vstart, 6
    vadd.vi v8, v16, 1
    csrr t1, vstart
    check 38, t1, 0
    vsetivli zero, 16, e8, m1, tu, mu
    vse8.v v8, (a2)
    lbu  t1, 4(a2)
    check 39, t1, 0xaa
    lbu  t1, 15(a2)
    check 40, t1, 0xaa

    /* vmv.s.x writes element 0 whenever vstart < vl, not only at vstart 0. */
    vsetivli zero, 4, e32, m1, tu, mu
    fillb v8, 0xaa
    vsetivli zero, 4, e32, m1, tu, mu
    li   t0, 0x12345678
    csrwi vstart, 1
    vmv.s.x v8, t0
    vse32.v v8, (a2)
    lw   t1, 0(a2)
    check 26, t1, 0x12345678

    /* A segment of three fields is legal at any aligned field register. */
    vsetivli zero, 16, e8, m1, tu, mu
    la   a1, seg_src
    vlseg3e8.v v8, (a1)
    vse8.v v8, (a2)
    lbu  t1, 5(a2)
    check 27, t1, 15
    vse8.v v9, (a2)
    lbu  t1, 5(a2)
    check 28, t1, 16
    vse8.v v10, (a2)
    lbu  t1, 5(a2)
    check 29, t1, 17

    /* Whole-register loads honor vstart in EEW elements: three 16-bit
     * elements are bytes 0-5, which must keep their old contents.
     */
    fillb v8, 0xaa
    fillb v9, 0xaa
    la   a1, bytes16
    csrwi vstart, 3
    vl2re16.v v8, (a1)
    csrr t1, vstart
    check 30, t1, 0
    vsetivli zero, 16, e8, m1, tu, mu
    vse8.v v8, (a2)
    lbu  t1, 5(a2)
    check 31, t1, 0xaa
    lbu  t1, 6(a2)
    check 32, t1, 6

    /* vlm.v loads ceil(vl/8) whole bytes. With vl=9 both bytes come from
     * memory, so the high bits of the second stay as loaded (zero here);
     * only the bytes past them are tail.
     */
    vsetivli zero, 9, e8, m1, tu, mu
    fillb v8, 0x55
    vsetivli zero, 9, e8, m1, tu, mu
    la   a1, zero_mem
    vlm.v v8, (a1)
    vsetivli zero, 16, e8, m1, tu, mu
    vse8.v v8, (a2)
    lbu  t1, 1(a2)
    check 33, t1, 0
    lbu  t1, 2(a2)
    check 34, t1, 0xff

    /* Fractional LMUL below SEW/ELEN is not supported and sets vill. */
    vsetvli t1, zero, e16, mf8, ta, ma
    csrr t1, vtype
    check 35, t1, 0x80000000
    csrr t1, vl
    check 36, t1, 0
    vsetvli t1, zero, e16, mf4, ta, ma
    csrr t1, vtype
    check 37, t1, 0xce

    li   a0, 1
    la   a1, ok_msg
    li   a2, ok_len
    li   a7, SYSWRITE
    ecall
    li   a0, 0
    j    exit

fail:
    li   a0, 1
    la   a1, fail_msg
    li   a2, fail_len
    li   a7, SYSWRITE
    ecall
    mv   a0, s1
exit:
    li   a7, SYSEXIT
    ecall
