# RISC-V RVV smoke test for rv32emu.

.org 0
.global _start

.set SYSEXIT, 93
.set SYSWRITE, 64

.macro fail_with label
    la a1, \label
    li a2, \label\()_len
    j write_and_exit_fail
.endm

.macro assert_reg_imm reg, imm, label
    li t6, \imm
    bne \reg, t6, \label
.endm

.section .rodata
ok_msg:
    .ascii "RVV smoke OK\n"
    .set ok_msg_len, . - ok_msg
fail_msg:
    .ascii "RVV smoke FAIL\n"
    .set fail_msg_len, . - fail_msg

bytes_src:
    .byte 0x11, 0x22, 0x33
    .balign 2

half_src:
    .short 0x1234, 0x5678, 0x9abc

mask_bits:
    .byte 0x05, 0x00, 0x00, 0x00

store_src:
    .byte 0x01, 0x02, 0x03, 0x04

cmp_src:
    .byte 0x07, 0x07, 0x05, 0x07

cmp_init:
    .byte 0x0a, 0x00, 0x00, 0x00

mask_prefix_src:
    .byte 0x14, 0x00, 0x00, 0x00

strided_src:
    .byte 0x10, 0xaa, 0x20, 0xbb, 0x30, 0xcc

alu_div_lhs:
    .byte 0x09, 0x80, 0x07, 0xff

alu_div_rhs:
    .byte 0x02, 0xff, 0x00, 0x02

alu_mulh_lhs:
    .byte 0x80, 0xff, 0x10, 0x03

alu_mulh_rhs:
    .byte 0x02, 0x02, 0x10, 0x04

alu_min_lhs:
    .byte 0x05, 0xf0, 0x80, 0x07

alu_min_rhs:
    .byte 0x03, 0x0f, 0xff, 0x02

carry_lhs:
    .byte 0xff, 0x00, 0x7f, 0x10

carry_rhs:
    .byte 0x01, 0x02, 0x80, 0x20

borrow_lhs:
    .byte 0x00, 0x05, 0x80, 0x10

borrow_rhs:
    .byte 0x01, 0x02, 0x7f, 0x20

shift_src:
    .byte 0x80, 0x7f, 0xf0, 0x03

shift_amt:
    .byte 0x01, 0x02, 0x04, 0x01

fixed_lhs:
    .byte 0xff, 0x70, 0x80, 0x05

fixed_rhs:
    .byte 0x02, 0x20, 0xff, 0x03

mulfix_lhs:
    .byte 0x40, 0x80, 0x20, 0x10

mulfix_rhs:
    .byte 0x40, 0x80, 0x40, 0x20

narrow16_a:
    .short 0x8000, 0x0002

narrow16_b:
    .short 0x0001, 0xffff

wide32_src:
    .word 0x00008010, 0x0001ff00

mac_src_a:
    .byte 0x02, 0x03, 0x04, 0x05

mac_src_b:
    .byte 0x05, 0x06, 0x07, 0x08

mac_acc:
    .byte 0x01, 0x02, 0x03, 0x04

wmacc_seed:
    .word 1000, 2000

vnsra_src:
    .short 0xF000, 0x8001

red_src:
    .byte 0x01, 0x02, 0x03, 0x04

red_acc:
    .byte 0x05, 0x00, 0x00, 0x00

wide_red_acc:
    .short 100

perm_src:
    .byte 0x10, 0x20, 0x30, 0x40

perm_idx:
    .byte 0x03, 0x00, 0x05, 0x01

perm_idx16:
    .short 0x0003, 0x0001, 0x0014, 0x0000

slide_init:
    .byte 0xaa, 0xbb, 0xcc, 0xdd

mask_mem_src:
    .byte 0xa5, 0x00, 0x00, 0x00

whole_src:
    .byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
    .byte 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    .byte 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
    .byte 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f

indexed_base:
    .byte 0x21, 0x99, 0x24, 0x99, 0x27, 0x99, 0x2a, 0x99
    .byte 0x2d, 0x99, 0x30, 0x99, 0x33, 0x99, 0x36, 0x99

indexed_offsets:
    .byte 0x04, 0x00, 0x08, 0x02

seg_unit_src:
    .byte 0x11, 0x21, 0x12, 0x22, 0x13, 0x23

seg_strided_src:
    .byte 0x31, 0x41, 0xaa, 0xbb
    .byte 0x32, 0x42, 0xaa, 0xbb
    .byte 0x33, 0x43, 0xaa, 0xbb

seg_indexed_base:
    .byte 0x52, 0x62, 0x53, 0x63, 0x51, 0x61

seg_indexed_offsets:
    .byte 0x04, 0x00, 0x02

# nf=3 e16 unit-stride source. Layout in memory: segment[i] = (f0, f1, f2)
# at offset i*6 bytes (3 halfwords per segment). 3 segments.
seg3_e16_src:
    .short 0x0011, 0x0021, 0x0031   # segment 0: f0=0x0011, f1=0x0021, f2=0x0031
    .short 0x0012, 0x0022, 0x0032   # segment 1
    .short 0x0013, 0x0023, 0x0033   # segment 2

# Mask byte for nf=2 strided masked test: bit 0 set, bit 1 clear,
# bit 2 set => segments 0 and 2 active, segment 1 masked off.
seg_strided_mask:
    .byte 0x05, 0x00, 0x00, 0x00

fp_vec_a:
    .word 0x3f800000, 0x40000000, 0x40800000, 0x41000000

fp_vec_b:
    .word 0x3f000000, 0x40000000, 0xbf800000, 0x3f800000

fp_acc:
    .word 0x41200000, 0x41a00000, 0x41f00000, 0x42200000

fp_scalar_1:
    .word 0x3f800000

fp_scalar_2:
    .word 0x40000000

fp_scalar_3:
    .word 0x40400000

fp_scalar_9:
    .word 0x41100000

fp_scalar_16:
    .word 0x41800000

fp_wide_seed:
    .word 0x00000000, 0x3ff00000
    .word 0x00000000, 0x40000000
    .word 0x00000000, 0x40080000
    .word 0x00000000, 0x40100000

# 8 bytes for vle16.v with SEW=8 + EMUL=2 test (4 halfwords).
eew_wide_src:
    .short 0x1111, 0x2222, 0x3333, 0x4444

# Source for vstart-resume test: vmseq.vx body produces a known mask
# pattern for elements [vstart, vl); prestart elements [0, vstart) must
# stay at the destination's pre-existing values.
vstart_src:
    .byte 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05

# Pre-fill bytes for v0 prior to the vmseq run; bits 0..7 = 0xC3 so the
# prestart bits can be seen later (the body should overwrite bits 2..7
# but leave bits 0..1 alone since vstart=2).
vstart_v0_init:
    .byte 0xC3, 0x00, 0x00, 0x00

# Source for the vmsbf-ignores-disabled-elements test. Bit-pattern 0x08
# = "first set bit is element 3"; combined with the v0.t mask 0x0E
# (elements 1, 2, 3 active), vmsbf should set output bits for active
# elements before the first ACTIVE set bit, i.e., bits 1 and 2 - yielding
# output bit pattern 0x06 in the active region. Bit 0 is masked-off so
# remains undisturbed in vd (the destination is pre-filled to 0xAA).
mask_aware_src:
    .byte 0x08, 0x00, 0x00, 0x00
mask_aware_v0:
    .byte 0x0E, 0x00, 0x00, 0x00
mask_aware_vd_init:
    .byte 0xAA, 0x00, 0x00, 0x00

# Source for viota-ignores-disabled-elements test. vs2 = 0x0F (bits
# 0,1,2,3 set), v0.t mask 0x0A (elements 1, 3 active). With mask-aware
# viota, the running count only advances at active set bits, so:
#   elem 0: disabled
#   elem 1: active, count_before=0, vs2[1]=1 -> count becomes 1, vd[1]=0
#   elem 2: disabled
#   elem 3: active, count_before=1, vs2[3]=1 -> count becomes 2, vd[3]=1
# A buggy implementation that counts disabled elements would write 1, 3
# at indexes 1, 3 instead.
viota_src:
    .byte 0x0F, 0x00, 0x00, 0x00
viota_v0:
    .byte 0x0A, 0x00, 0x00, 0x00

.section .bss
    .balign 4
byte_out:
    .space 16
half_out:
    .space 16
store_out:
    .space 4
cmp_out:
    .space 4
mask_out:
    .space 16
strided_out:
    .space 16
strided_store_out:
    .space 16
div_out:
    .space 16
rem_out:
    .space 16
mulh_out:
    .space 16
min_out:
    .space 16
carry_out:
    .space 16
carry_mask_out:
    .space 16
borrow_out:
    .space 16
borrow_mask_out:
    .space 16
shift_out:
    .space 16
fixed_out:
    .space 16
wide_out:
    .space 32
red_out:
    .space 16
wide_red_out:
    .space 16
perm_out:
    .space 16
vid_out:
    .space 16
mask_mem_out:
    .space 16
whole_out:
    .space 32
indexed_out:
    .space 16
indexed_store_out:
    .space 16
seg_unit_out:
    .space 16
seg_strided_out:
    .space 16
seg_indexed_out:
    .space 16
seg_indexed_store_out:
    .space 16
seg3_e16_out:
    .space 32
seg_masked_out:
    .space 16
fp32_out:
    .space 32
fp64_out:
    .space 64
eew_wide_out:
    .space 16
vstart_out:
    .space 16
mask_aware_out:
    .space 16
viota_aware_out:
    .space 16

.text
_start:
    csrw vstart, zero

    li t0, 2
    csrw vxrm, t0
    csrr t1, vxrm
    assert_reg_imm t1, 2, fail

    li t0, 1
    csrw vxsat, t0
    csrr t1, vxsat
    assert_reg_imm t1, 1, fail
    csrr t1, vcsr
    assert_reg_imm t1, 6, fail

    li t0, 1
    csrw vcsr, t0
    csrr t1, vxrm
    assert_reg_imm t1, 1, fail
    csrr t1, vxsat
    assert_reg_imm t1, 0, fail
    csrr t1, vcsr
    assert_reg_imm t1, 1, fail

    csrr t1, vlenb
    assert_reg_imm t1, 16, fail

    li t0, 0x18
    vsetvl t1, zero, t0
    bnez t1, fail
    csrr t1, vl
    bnez t1, fail
    csrr t1, vtype
    li t2, 0x80000000
    bne t1, t2, fail

    # Keep-vl forms are reserved when the reconfiguration would change
    # VLMAX. Start from a legal e8,m8 state, then try to keep vl while
    # switching to e32,m1; the result must collapse to vill.
    li a0, 16
    vsetvli t0, a0, e8, m8, ta, mu
    csrr t1, vl
    assert_reg_imm t1, 16, fail
    vsetvli zero, zero, e32, m1, ta, mu
    csrr t1, vl
    bnez t1, fail
    csrr t1, vtype
    li t2, 0x80000000
    bne t1, t2, fail

    # Round-trip 3 bytes via vle8.v / vse8.v with vta=1. The spec mandates
    # EEW (not SEW) controls register packing for unit-stride loads/stores,
    # so the loaded bytes stay byte-packed and the tail elements past vl=3
    # get the agnostic 0xFF fill in 8-bit slots.
    li a0, 3
    vsetvli t0, a0, e8, m1, ta, mu
    csrr t1, vl
    assert_reg_imm t1, 3, fail
    csrw vstart, zero
    la a1, bytes_src
    vle8.v v1, (a1)

    # Re-issue with vl=VLMAX so vse8.v writes the body and the agnostic tail
    # bytes that were synthesized by the load.
    csrr t1, vlenb
    vsetvli t0, t1, e8, m1, tu, mu
    la a1, byte_out
    vse8.v v1, (a1)

    la t0, byte_out
    lbu t1, 0(t0)
    assert_reg_imm t1, 0x11, fail
    lbu t1, 1(t0)
    assert_reg_imm t1, 0x22, fail
    lbu t1, 2(t0)
    assert_reg_imm t1, 0x33, fail
    lbu t1, 3(t0)
    assert_reg_imm t1, 0xff, fail
    lbu t1, 15(t0)
    assert_reg_imm t1, 0xff, fail

    # Round-trip 3 halfwords via vle16.v / vse16.v with vta=1. EEW=SEW=16 here,
    # so this directly validates the 16-bit element path independent of the
    # cross-EEW packing rules above.
    li a0, 3
    vsetvli t0, a0, e16, m1, ta, mu
    la a1, half_src
    vle16.v v2, (a1)

    li a0, 8
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, half_out
    vse16.v v2, (a1)

    la t0, half_out
    lhu t1, 0(t0)
    assert_reg_imm t1, 0x1234, fail
    lhu t1, 2(t0)
    assert_reg_imm t1, 0x5678, fail
    lhu t1, 4(t0)
    assert_reg_imm t1, 0x9abc, fail
    lhu t1, 6(t0)
    assert_reg_imm t1, 0xffff, fail
    lhu t1, 14(t0)
    assert_reg_imm t1, 0xffff, fail

    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, store_src
    vle8.v v2, (a1)
    la a1, mask_bits
    vle8.v v0, (a1)

    la t0, store_out
    li t1, 0xaa
    sb t1, 0(t0)
    sb t1, 1(t0)
    sb t1, 2(t0)
    sb t1, 3(t0)

    vse8.v v2, (t0), v0.t

    lbu t1, 0(t0)
    assert_reg_imm t1, 0x01, fail
    lbu t1, 1(t0)
    assert_reg_imm t1, 0xaa, fail
    lbu t1, 2(t0)
    assert_reg_imm t1, 0x03, fail
    lbu t1, 3(t0)
    assert_reg_imm t1, 0xaa, fail

    la a1, cmp_src
    vle8.v v2, (a1)
    la a1, cmp_init
    vle8.v v3, (a1)
    li t2, 7
    vmseq.vx v3, v2, t2, v0.t
    la a1, cmp_out
    vse8.v v3, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xfb, fail

    li a0, 8
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, mask_prefix_src
    vle8.v v4, (a1)

    vmsbf.m v5, v4
    la a1, mask_out
    vse8.v v5, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x03, fail

    vmsif.m v5, v4
    vse8.v v5, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x07, fail

    vmsof.m v5, v4
    vse8.v v5, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x04, fail

    viota.m v6, v4
    vse8.v v6, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x00, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x01, fail
    lbu t1, 5(a1)
    assert_reg_imm t1, 0x02, fail
    lbu t1, 7(a1)
    assert_reg_imm t1, 0x02, fail

    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    vid.v v7
    la a1, vid_out
    vse8.v v7, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x00, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x01, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x02, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x03, fail

    li a0, 3
    vsetvli t0, a0, e8, m1, tu, mu
    li t3, 2
    la a1, strided_src
    vlse8.v v8, (a1), t3
    la a1, strided_out
    vse8.v v8, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x10, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x20, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x30, fail

    la t0, strided_store_out
    li t1, 0xaa
    sb t1, 0(t0)
    sb t1, 1(t0)
    sb t1, 2(t0)
    sb t1, 3(t0)
    sb t1, 4(t0)
    sb t1, 5(t0)
    vsse8.v v8, (t0), t3
    lbu t1, 0(t0)
    assert_reg_imm t1, 0x10, fail
    lbu t1, 1(t0)
    assert_reg_imm t1, 0xaa, fail
    lbu t1, 2(t0)
    assert_reg_imm t1, 0x20, fail
    lbu t1, 3(t0)
    assert_reg_imm t1, 0xaa, fail
    lbu t1, 4(t0)
    assert_reg_imm t1, 0x30, fail

    li a0, 3
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, seg_unit_src
    vlseg2e8.v v24, (a1)
    la a1, seg_unit_out
    vsseg2e8.v v24, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x11, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x21, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x12, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x22, fail
    lbu t1, 4(a1)
    assert_reg_imm t1, 0x13, fail
    lbu t1, 5(a1)
    assert_reg_imm t1, 0x23, fail

    la a1, seg_unit_src
    vlseg2e8ff.v v24, (a1)
    csrr t1, vl
    assert_reg_imm t1, 3, fail
    la a1, seg_unit_out
    vsseg2e8.v v24, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x11, fail
    lbu t1, 5(a1)
    assert_reg_imm t1, 0x23, fail

    li t3, 4
    la a1, seg_strided_src
    vlsseg2e8.v v24, (a1), t3
    la t0, seg_strided_out
    li t1, 0xaa
    sb t1, 0(t0)
    sb t1, 1(t0)
    sb t1, 2(t0)
    sb t1, 3(t0)
    sb t1, 4(t0)
    sb t1, 5(t0)
    sb t1, 6(t0)
    sb t1, 7(t0)
    sb t1, 8(t0)
    sb t1, 9(t0)
    sb t1, 10(t0)
    sb t1, 11(t0)
    vssseg2e8.v v24, (t0), t3
    lbu t1, 0(t0)
    assert_reg_imm t1, 0x31, fail
    lbu t1, 1(t0)
    assert_reg_imm t1, 0x41, fail
    lbu t1, 2(t0)
    assert_reg_imm t1, 0xaa, fail
    lbu t1, 4(t0)
    assert_reg_imm t1, 0x32, fail
    lbu t1, 5(t0)
    assert_reg_imm t1, 0x42, fail
    lbu t1, 8(t0)
    assert_reg_imm t1, 0x33, fail
    lbu t1, 9(t0)
    assert_reg_imm t1, 0x43, fail

    li a0, 3
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, seg_indexed_offsets
    vle8.v v9, (a1)
    la a1, seg_indexed_base
    vluxseg2ei8.v v24, (a1), v9
    la a1, seg_indexed_out
    vsseg2e8.v v24, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x51, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x61, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x52, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x62, fail
    lbu t1, 4(a1)
    assert_reg_imm t1, 0x53, fail
    lbu t1, 5(a1)
    assert_reg_imm t1, 0x63, fail

    la a1, seg_indexed_base
    vloxseg2ei8.v v24, (a1), v9
    la a1, seg_indexed_out
    vsseg2e8.v v24, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x51, fail
    lbu t1, 5(a1)
    assert_reg_imm t1, 0x63, fail

    la t0, seg_indexed_store_out
    li t1, 0xaa
    sb t1, 0(t0)
    sb t1, 1(t0)
    sb t1, 2(t0)
    sb t1, 3(t0)
    sb t1, 4(t0)
    sb t1, 5(t0)
    vsoxseg2ei8.v v24, (t0), v9
    lbu t1, 0(t0)
    assert_reg_imm t1, 0x52, fail
    lbu t1, 1(t0)
    assert_reg_imm t1, 0x62, fail
    lbu t1, 2(t0)
    assert_reg_imm t1, 0x53, fail
    lbu t1, 3(t0)
    assert_reg_imm t1, 0x63, fail
    lbu t1, 4(t0)
    assert_reg_imm t1, 0x51, fail
    lbu t1, 5(t0)
    assert_reg_imm t1, 0x61, fail

    li t1, 0xaa
    sb t1, 0(t0)
    sb t1, 1(t0)
    sb t1, 2(t0)
    sb t1, 3(t0)
    sb t1, 4(t0)
    sb t1, 5(t0)
    vsuxseg2ei8.v v24, (t0), v9
    lbu t1, 0(t0)
    assert_reg_imm t1, 0x52, fail
    lbu t1, 1(t0)
    assert_reg_imm t1, 0x62, fail
    lbu t1, 2(t0)
    assert_reg_imm t1, 0x53, fail
    lbu t1, 3(t0)
    assert_reg_imm t1, 0x63, fail
    lbu t1, 4(t0)
    assert_reg_imm t1, 0x51, fail
    lbu t1, 5(t0)
    assert_reg_imm t1, 0x61, fail

    # nf=3 e16 unit-stride round-trip - exercises larger nf and 16-bit
    # element width past the original nf=2/e8 happy path. Memory layout:
    # 3 segments × 3 fields × 2 bytes = 18 bytes total. Per V 1.0 §7.7,
    # field f of segment i lands in v(vd + f*EMUL)[i]; with EMUL=1 here,
    # vd=v24 → v24[0..2]={0x11,0x12,0x13}, v25[0..2]={0x21,0x22,0x23},
    # v26[0..2]={0x31,0x32,0x33}.
    li a0, 3
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, seg3_e16_src
    vlseg3e16.v v24, (a1)
    la a1, seg3_e16_out
    vsseg3e16.v v24, (a1)
    lhu t1, 0(a1)
    assert_reg_imm t1, 0x0011, fail
    lhu t1, 2(a1)
    assert_reg_imm t1, 0x0021, fail
    lhu t1, 4(a1)
    assert_reg_imm t1, 0x0031, fail
    lhu t1, 6(a1)
    assert_reg_imm t1, 0x0012, fail
    lhu t1, 8(a1)
    assert_reg_imm t1, 0x0022, fail
    lhu t1, 10(a1)
    assert_reg_imm t1, 0x0032, fail
    lhu t1, 12(a1)
    assert_reg_imm t1, 0x0013, fail
    lhu t1, 14(a1)
    assert_reg_imm t1, 0x0023, fail
    lhu t1, 16(a1)
    assert_reg_imm t1, 0x0033, fail

    # Masked nf=2 e8 strided load. Mask v0 = 0x05 → segments 0 and 2
    # active, segment 1 masked off (mu policy: leave destination
    # undisturbed for masked-off elements). Pre-fill v24 and v25 with a
    # sentinel, then run the load. The masked-off middle segment must
    # show through as the sentinel in the output.
    #
    # This case also confirms the round-6 vd-vs-v0 overlap-trap doesn't
    # false-positive on legal masked encodings (vd=v24, v0=v0, spans
    # don't overlap). The trap-fires-on-illegal-overlap path requires
    # trap-handler infrastructure (mtvec + handler in M-mode) that is
    # outside the current user-mode smoke harness; the harness uses
    # tail -n 1 == "RVV smoke OK" as its pass criterion, so any
    # mid-program trap aborts the program before that line is printed
    # rather than producing a checkable failure mode. Trap-path coverage
    # is tracked separately and gated on the trap-handler bring-up.
    li a0, 1
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, seg_strided_mask
    vle8.v v0, (a1)

    # Pre-fill destination segments with 0x77.
    li a0, 4
    vsetvli t0, a0, e8, m1, ta, mu
    li t2, 0x77
    vmv.v.x v24, t2
    vmv.v.x v25, t2

    li a0, 3
    vsetvli t0, a0, e8, m1, tu, mu
    li t3, 4
    la a1, seg_strided_src
    vlsseg2e8.v v24, (a1), t3, v0.t
    la a1, seg_masked_out
    vsseg2e8.v v24, (a1)
    # Expected layout (vsseg2 stores f0,f1,f0,f1,f0,f1):
    #   seg 0 active:    f0=0x31, f1=0x41
    #   seg 1 masked:    v24[1]=0x77, v25[1]=0x77 (preserved)
    #   seg 2 active:    f0=0x33, f1=0x43
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x31, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x41, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x77, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x77, fail
    lbu t1, 4(a1)
    assert_reg_imm t1, 0x33, fail
    lbu t1, 5(a1)
    assert_reg_imm t1, 0x43, fail

    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu

    la a1, alu_div_lhs
    vle8.v v10, (a1)
    la a1, alu_div_rhs
    vle8.v v11, (a1)

    vdiv.vv v12, v10, v11
    la a1, div_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x04, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x80, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xff, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x00, fail

    vrem.vv v12, v10, v11
    la a1, rem_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x01, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x00, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x07, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0xff, fail

    la a1, alu_mulh_lhs
    vle8.v v10, (a1)
    la a1, alu_mulh_rhs
    vle8.v v11, (a1)
    vmulh.vv v12, v10, v11
    la a1, mulh_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xff, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0xff, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x01, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x00, fail

    la a1, alu_min_lhs
    vle8.v v10, (a1)
    la a1, alu_min_rhs
    vle8.v v11, (a1)
    vminu.vv v12, v10, v11
    la a1, min_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x03, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x0f, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x80, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x02, fail

    la a1, mask_bits
    vle8.v v0, (a1)
    la a1, carry_lhs
    vle8.v v10, (a1)
    la a1, carry_rhs
    vle8.v v11, (a1)

    vadc.vvm v12, v10, v11, v0
    la a1, carry_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x01, fail_vadc
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x02, fail_vadc
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x00, fail_vadc
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x30, fail_vadc

    vmadc.vvm v13, v10, v11, v0
    la a1, carry_mask_out
    vsm.v v13, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xf5, fail_vmadc

    la a1, borrow_lhs
    vle8.v v10, (a1)
    la a1, borrow_rhs
    vle8.v v11, (a1)

    vsbc.vvm v12, v10, v11, v0
    la a1, borrow_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xfe, fail_vsbc
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x03, fail_vsbc
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x00, fail_vsbc
    lbu t1, 3(a1)
    assert_reg_imm t1, 0xf0, fail_vsbc

    vmsbc.vvm v13, v10, v11, v0
    la a1, borrow_mask_out
    vsm.v v13, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xf9, fail_vmsbc

    la a1, shift_src
    vle8.v v10, (a1)
    la a1, shift_amt
    vle8.v v11, (a1)

    vsrl.vv v12, v10, v11
    la a1, shift_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x40, fail_vsrl_vv
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x1f, fail_vsrl_vv
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x0f, fail_vsrl_vv
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x01, fail_vsrl_vv

    li t2, 1
    vsra.vx v12, v10, t2
    la a1, shift_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xc0, fail_vsra_vx
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x3f, fail_vsra_vx
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xf8, fail_vsra_vx
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x01, fail_vsra_vx

    vsrl.vi v12, v10, 4
    la a1, shift_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x08, fail_vsrl_vi
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x07, fail_vsrl_vi
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x0f, fail_vsrl_vi
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x00, fail_vsrl_vi

    la a1, fixed_lhs
    vle8.v v10, (a1)
    la a1, fixed_rhs
    vle8.v v11, (a1)

    csrw vxsat, zero
    vsaddu.vv v12, v10, v11
    la a1, fixed_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xff, fail_vsaddu
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x90, fail_vsaddu
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xff, fail_vsaddu
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x08, fail_vsaddu
    csrr t1, vxsat
    assert_reg_imm t1, 1, fail_vsaddu

    csrw vxsat, zero
    vsadd.vv v12, v10, v11
    la a1, fixed_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x01, fail_vsadd
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x7f, fail_vsadd
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x80, fail_vsadd
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x08, fail_vsadd
    csrr t1, vxsat
    assert_reg_imm t1, 1, fail_vsadd

    li t2, 0
    csrw vxrm, t2
    vssrl.vv v12, v10, v11
    la a1, fixed_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x40, fail_vssrl
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x70, fail_vssrl
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x01, fail_vssrl
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x01, fail_vssrl

    vssra.vv v12, v10, v11
    la a1, fixed_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x00, fail_vssra
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x70, fail_vssra
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xff, fail_vssra
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x01, fail_vssra

    la a1, mulfix_lhs
    vle8.v v10, (a1)
    la a1, mulfix_rhs
    vle8.v v11, (a1)
    csrw vxsat, zero
    vsmul.vv v12, v10, v11
    la a1, fixed_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x20, fail_vsmul0
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x7f, fail_vsmul1
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x10, fail_vsmul2
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x04, fail_vsmul3
    csrr t1, vxsat
    assert_reg_imm t1, 1, fail_vsmul4

    li a0, 2
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, narrow16_a
    vle16.v v20, (a1)
    la a1, narrow16_b
    vle16.v v21, (a1)

    vwadd.vv v22, v20, v21
    la a1, wide_out
    vse32.v v22, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, -32767, fail_vwadd
    lw t1, 4(a1)
    assert_reg_imm t1, 1, fail_vwadd

    vwmulu.vv v22, v20, v21
    la a1, wide_out
    vse32.v v22, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 32768, fail_vwmulu0
    lw t1, 4(a1)
    assert_reg_imm t1, 131070, fail_vwmulu1

    la a1, wide32_src
    vle32.v v24, (a1)
    vnsrl.wi v18, v24, 4
    la a1, wide_out
    vse16.v v18, (a1)
    lhu t1, 0(a1)
    assert_reg_imm t1, 0x0801, fail_vnsrl0
    lhu t1, 2(a1)
    assert_reg_imm t1, 0x1ff0, fail_vnsrl1

    li t2, 0
    csrw vxrm, t2
    csrw vxsat, zero
    vnclipu.wi v18, v24, 0
    la a1, wide_out
    vse16.v v18, (a1)
    lhu t1, 0(a1)
    assert_reg_imm t1, 0x8010, fail_vnclipu0
    lhu t1, 2(a1)
    assert_reg_imm t1, 0xffff, fail_vnclipu1
    csrr t1, vxsat
    assert_reg_imm t1, 1, fail_vnclipu2

    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, mac_acc
    vle8.v v26, (a1)
    la a1, mac_src_a
    vle8.v v27, (a1)
    la a1, mac_src_b
    vle8.v v28, (a1)
    vmacc.vv v26, v27, v28
    la a1, wide_out
    vse8.v v26, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 11, fail_vmacc
    lbu t1, 1(a1)
    assert_reg_imm t1, 20, fail_vmacc
    lbu t1, 2(a1)
    assert_reg_imm t1, 31, fail_vmacc
    lbu t1, 3(a1)
    assert_reg_imm t1, 44, fail_vmacc

    # vmadd.vv: vd = vs1*vd + vs2 (per V 1.0 §11.13.6 - vd appears in
    # the multiply, vs2 is the addend). Inputs:
    #   vd_in (mac_acc) = {1,2,3,4}; vs1 (mac_src_a) = {2,3,4,5};
    #   vs2 (mac_src_b) = {5,6,7,8}.
    # Expected: vs1*vd_in + vs2:
    #   2*1 + 5 = 7
    #   3*2 + 6 = 12
    #   4*3 + 7 = 19
    #   5*4 + 8 = 28
    la a1, mac_acc
    vle8.v v26, (a1)
    vmadd.vv v26, v27, v28
    la a1, wide_out
    vse8.v v26, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 7, fail_vmadd
    lbu t1, 1(a1)
    assert_reg_imm t1, 12, fail_vmadd
    lbu t1, 2(a1)
    assert_reg_imm t1, 19, fail_vmadd
    lbu t1, 3(a1)
    assert_reg_imm t1, 28, fail_vmadd

    # vnmsac.vv: vd[i] -= vs1[i] * vs2[i]. Re-use the mac data; expected
    # vd[i] = mac_acc[i] - mac_src_a[i] * mac_src_b[i]:
    #   1 - 2*5 = -9   = 0xF7
    #   2 - 3*6 = -16  = 0xF0
    #   3 - 4*7 = -25  = 0xE7
    #   4 - 5*8 = -36  = 0xDC
    la a1, mac_acc
    vle8.v v26, (a1)
    vnmsac.vv v26, v27, v28
    la a1, wide_out
    vse8.v v26, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xf7, fail_vnmsac
    lbu t1, 1(a1)
    assert_reg_imm t1, 0xf0, fail_vnmsac
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xe7, fail_vnmsac
    lbu t1, 3(a1)
    assert_reg_imm t1, 0xdc, fail_vnmsac

    # vnmsub.vv: vd[i] = vs2[i] - vs1[i] * vd[i] (vd appears in multiply).
    # mac_acc[i] = vd_in:
    #   5 - 2*1  = 3
    #   6 - 3*2  = 0
    #   7 - 4*3  = -5  = 0xFB
    #   8 - 5*4  = -12 = 0xF4
    la a1, mac_acc
    vle8.v v26, (a1)
    vnmsub.vv v26, v27, v28
    la a1, wide_out
    vse8.v v26, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 3, fail_vnmsub
    lbu t1, 1(a1)
    assert_reg_imm t1, 0, fail_vnmsub
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xfb, fail_vnmsub
    lbu t1, 3(a1)
    assert_reg_imm t1, 0xf4, fail_vnmsub

    # Widening signed × unsigned multiply (vwmulsu): SEW=16 → 2*SEW=32.
    # narrow16_a = {0x8000 = -32768 signed, 0x0002 = 2 signed}
    # narrow16_b = {0x0001 = 1 unsigned, 0xFFFF = 65535 unsigned}
    # vs2 signed × vs1 unsigned:
    #   -32768 * 1     = -32768 = 0xFFFF8000
    #   2 * 65535      = 131070 = 0x0001FFFE
    li a0, 2
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, narrow16_a
    vle16.v v20, (a1)
    la a1, narrow16_b
    vle16.v v21, (a1)
    vwmulsu.vv v22, v20, v21
    la a1, wide_out
    vse32.v v22, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, -32768, fail_vwmulsu
    lw t1, 4(a1)
    assert_reg_imm t1, 131070, fail_vwmulsu

    # Mixed widening (vwadd.wv): vs2 already wide (32-bit), vs1 narrow
    # (16-bit, sign-extended). Compute vs2[i] + sext(vs1[i]):
    #   prior wide_out = {-32768 (0xFFFF8000), 131070 (0x0001FFFE)}
    #   narrow16_a (sign-extended) = {-32768, 2}
    #   sum: -65536 (0xFFFF0000), 131072 (0x00020000)
    la a1, wide_out
    vle32.v v22, (a1)
    li a0, 2
    vsetvli t0, a0, e16, m1, tu, mu
    vwadd.wv v24, v22, v20
    la a1, wide_out
    vse32.v v24, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, -65536, fail_vwadd_wv
    lw t1, 4(a1)
    assert_reg_imm t1, 131072, fail_vwadd_wv

    # Widening MAC (vwmacc.vv): vd (32-bit) += signed(vs1) * signed(vs2).
    # Pre-load v22 with {10, 20} (sign-extended 32-bit) via vmv.v.i wouldn't
    # work for arbitrary values; use vle32 with a known seed.
    li a0, 2
    vsetvli t0, a0, e32, m1, ta, mu
    la a1, wmacc_seed
    vle32.v v22, (a1)
    li a0, 2
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, narrow16_a
    vle16.v v20, (a1)
    la a1, narrow16_b
    vle16.v v21, (a1)
    # narrow16_a = {-32768, 2}, narrow16_b = {0x0001, 0xFFFF}
    # As signed 16-bit: vs1=narrow16_a, vs2=narrow16_b → both signed.
    # Products: -32768*1 = -32768; 2 * (-1) = -2
    # Accumulate into seed {1000, 2000}:
    #   1000 + (-32768) = -31768
    #   2000 + (-2) = 1998
    vwmacc.vv v22, v20, v21
    la a1, wide_out
    vse32.v v22, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, -31768, fail_vwmacc
    lw t1, 4(a1)
    assert_reg_imm t1, 1998, fail_vwmacc

    # vnsra signed (SEW=8): wide source 16-bit signed, arithmetic right
    # shift to 8-bit. Use values where the sign bit propagates.
    li a0, 2
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, vnsra_src
    vle16.v v24, (a1)
    li a0, 2
    vsetvli t0, a0, e8, m1, ta, mu
    vnsra.wi v18, v24, 4
    la a1, wide_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    # 0xF000 (signed = -4096) >> 4 = -256 → truncated to 8-bit = 0x00
    # Wait: actually -4096 (16-bit signed = 0xF000) sign-extended in 16-bit
    # >> 4 (arithmetic) = -256 (16-bit signed = 0xFF00). Truncate to 8 bits
    # = 0x00.
    assert_reg_imm t1, 0x00, fail_vnsra
    lbu t1, 1(a1)
    # 0x8001 (signed = -32767) >> 4 = -2048 (16-bit signed = 0xF800).
    # Truncate to 8 bits = 0x00.
    assert_reg_imm t1, 0x00, fail_vnsra

    # vxrm rounding mode test for vnclipu - exercise round-down (rdn=2)
    # which truncates discarded bits regardless of value. wide32_src =
    # {0x00008010, 0x0001ff00}. >> 4 with rdn = truncate low 4 bits.
    #   0x00008010 >> 4 = 0x0000801 (no saturation, fits in 16 bits)
    #   0x0001ff00 >> 4 = 0x00001ff0 (no saturation, fits in 16 bits)
    li t2, 2
    csrw vxrm, t2
    csrw vxsat, zero
    li a0, 2
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, wide32_src
    vle32.v v24, (a1)
    vnclipu.wi v18, v24, 4
    la a1, wide_out
    vse16.v v18, (a1)
    lhu t1, 0(a1)
    assert_reg_imm t1, 0x0801, fail_vnclipu_rdn
    lhu t1, 2(a1)
    assert_reg_imm t1, 0x1ff0, fail_vnclipu_rdn
    csrr t1, vxsat
    assert_reg_imm t1, 0, fail_vnclipu_rdn

    # SEW=32 vsmul - exercises rvv_roundoff_signed at width=64. The
    # CRITICAL bug from round 4 review made the helper truncate 64-bit
    # values to uint32_t before sign-extending and shifting; this regression
    # test ensures the saturation/sign-extension at full width works.
    # vsmul: vd = round((vs1 * vs2) >> (SEW-1)), saturate to SEW range.
    # 0x40000000 * 0x40000000 = 0x1000000000000000 (int64)
    # >> 31 = 0x20000000 (positive, no saturation)
    li a0, 1
    vsetvli t0, a0, e32, m1, ta, mu
    li t2, 0x40000000
    vmv.s.x v10, t2
    vmv.s.x v11, t2
    csrw vxrm, zero
    csrw vxsat, zero
    vsmul.vv v12, v10, v11
    vmv.x.s t3, v12
    li t6, 0x20000000
    bne t3, t6, fail_vsmul_w64
    csrr t1, vxsat
    assert_reg_imm t1, 0, fail_vsmul_w64

    # Restore SEW=8, vl=4 expected by the vredsum block that follows.
    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu

    la a1, red_src
    vle8.v v10, (a1)
    la a1, red_acc
    vle8.v v11, (a1)

    vredsum.vs v12, v10, v11
    la a1, red_out
    vse8.v v12, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 15, fail_vredsum

    li a0, 1
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, wide_red_acc
    vle16.v v11, (a1)
    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, red_src
    vle8.v v10, (a1)
    vwredsumu.vs v12, v10, v11
    li a0, 8
    vsetvli t0, a0, e16, m1, tu, mu
    la a1, wide_red_out
    vse16.v v12, (a1)
    lhu t1, 0(a1)
    assert_reg_imm t1, 110, fail_vwredsumu

    li a0, 4
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, perm_src
    vle8.v v16, (a1)
    la a1, perm_idx
    vle8.v v17, (a1)

    vrgather.vv v18, v16, v17
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x40, fail_vrgather
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x10, fail_vrgather
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xff, fail_vrgather
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x20, fail_vrgather

    vrgather.vi v18, v16, 2
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x30, fail_vrgather_vi
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x30, fail_vrgather_vi

    la a1, perm_idx16
    vle16.v v20, (a1)
    vrgatherei16.vv v18, v16, v20
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x40, fail_vrgatherei16
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x20, fail_vrgatherei16
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x00, fail_vrgatherei16
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x10, fail_vrgatherei16

    la a1, slide_init
    vle8.v v18, (a1)
    vslideup.vi v18, v16, 1
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xaa, fail_vslideup
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x10, fail_vslideup
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x20, fail_vslideup
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x30, fail_vslideup

    vslidedown.vi v18, v16, 1
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x20, fail_vslidedown
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x30, fail_vslidedown
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x40, fail_vslidedown
    lbu t1, 3(a1)
    assert_reg_imm t1, 0xff, fail_vslidedown

    li t2, 0x77
    vslide1up.vx v18, v16, t2
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x77, fail_vslide1up
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x10, fail_vslide1up
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x20, fail_vslide1up
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x30, fail_vslide1up

    li t2, 0x66
    vslide1down.vx v18, v16, t2
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x20, fail_vslide1down
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x30, fail_vslide1down
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x40, fail_vslide1down
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x66, fail_vslide1down

    la a1, mask_bits
    vle8.v v0, (a1)
    vcompress.vm v18, v16, v0
    la a1, perm_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x10, fail_vcompress
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x30, fail_vcompress
    lbu t1, 2(a1)
    assert_reg_imm t1, 0xff, fail_vcompress
    lbu t1, 3(a1)
    assert_reg_imm t1, 0xff, fail_vcompress

    # vmv.s.x has a unique tail policy per V 1.0 §16.1: only vd[0] is
    # written; ALL other elements are unchanged regardless of vta. So
    # after vmv.v.i v10, 0 (which fills v10[0..vl-1]=0 and uses vta to
    # synthesize v10[vl..VLMAX]=0xFF), vmv.s.x v10, -5 updates only
    # v10[0]. Elements v10[1..3] retain the 0 from vmv.v.i - NOT the
    # 0xFF that a (buggy) vta-controlled vmv.s.x would have written.
    # vmv.x.s sign-extends vs2[0] to XLEN regardless of signedness.
    li a0, 4
    vsetvli t0, a0, e8, m1, ta, mu
    vmv.v.i v10, 0
    li t2, -5
    vmv.s.x v10, t2
    vmv.x.s t3, v10
    assert_reg_imm t3, -5, fail_vmv_x_s
    la a1, shift_out
    vse8.v v10, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xfb, fail_vmv_s_x
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x00, fail_vmv_s_x
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x00, fail_vmv_s_x
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x00, fail_vmv_s_x

    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, bytes_src
    vle8ff.v v13, (a1)
    la a1, indexed_out
    vse8.v v13, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x11, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x22, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x33, fail

    li a0, 8
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, mask_mem_src
    vlm.v v14, (a1)
    la a1, mask_mem_out
    vsm.v v14, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xa5, fail

    vcpop.m t1, v14
    assert_reg_imm t1, 4, fail
    vfirst.m t1, v14
    assert_reg_imm t1, 0, fail

    la a1, mask_prefix_src
    vlm.v v5, (a1)
    vmand.mm v15, v5, v14
    la a1, mask_out
    vsm.v v15, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x04, fail

    vmv.v.v v0, v14
    la a1, whole_src
    vle8.v v20, (a1)
    vmerge.vim v21, v20, 0x0f, v0
    la a1, indexed_out
    vse8.v v21, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x0f, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x01, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x0f, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x03, fail

    la a1, whole_src
    vl2re8.v v16, (a1)
    la a1, whole_out
    vs2r.v v16, (a1)
    la t0, whole_out
    lbu t1, 0(t0)
    assert_reg_imm t1, 0x00, fail
    lbu t1, 15(t0)
    assert_reg_imm t1, 0x0f, fail
    lbu t1, 16(t0)
    assert_reg_imm t1, 0x10, fail
    lbu t1, 31(t0)
    assert_reg_imm t1, 0x1f, fail

    li a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    la a1, indexed_offsets
    vle8.v v9, (a1)
    la a1, indexed_base
    vluxei8.v v18, (a1), v9
    la a1, indexed_out
    vse8.v v18, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x27, fail
    lbu t1, 1(a1)
    assert_reg_imm t1, 0x21, fail
    lbu t1, 2(a1)
    assert_reg_imm t1, 0x2d, fail
    lbu t1, 3(a1)
    assert_reg_imm t1, 0x24, fail

    la t0, indexed_store_out
    li t1, 0xaa
    sb t1, 0(t0)
    sb t1, 1(t0)
    sb t1, 2(t0)
    sb t1, 3(t0)
    sb t1, 4(t0)
    sb t1, 5(t0)
    sb t1, 6(t0)
    sb t1, 7(t0)
    sb t1, 8(t0)
    sb t1, 9(t0)
    vsuxei8.v v18, (t0), v9
    lbu t1, 0(t0)
    assert_reg_imm t1, 0x21, fail
    lbu t1, 2(t0)
    assert_reg_imm t1, 0x24, fail
    lbu t1, 4(t0)
    assert_reg_imm t1, 0x27, fail
    lbu t1, 8(t0)
    assert_reg_imm t1, 0x2d, fail

    # Legal EEW > SEW: configure SEW=8 then issue vle16.v. EMUL =
    # (16/8)*1 = 2, so vd must be even-aligned (v22 is). The first round
    # incorrectly rejected this combination as illegal; it is now valid
    # per V 1.0 §7.4.
    li a0, 4
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, eew_wide_src
    vle16.v v22, (a1)
    la a1, eew_wide_out
    vse16.v v22, (a1)
    lhu t1, 0(a1)
    assert_reg_imm t1, 0x1111, fail_eew1
    lhu t1, 2(a1)
    assert_reg_imm t1, 0x2222, fail_eew2
    lhu t1, 4(a1)
    assert_reg_imm t1, 0x3333, fail_eew3
    lhu t1, 6(a1)
    assert_reg_imm t1, 0x4444, fail_eew4

    # vstart resume: pre-load the destination v24 with a sentinel byte,
    # then run vmseq.vx with vstart=2, vl=8. The body should rewrite
    # mask bits [2, 8) but leave bits [0, 2) untouched. Source = all
    # 0x05 vs scalar 0x05 -> body bits all match -> bits 2..7 set; bits
    # 0..1 retain their pre-load values (1, 1 from 0xC3) -> final byte
    # = 0xFF (sentinel bits 0..1 OR body bits 2..7 OR tail-agnostic
    # bits 8..VLMAX).
    li a0, 1
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, vstart_v0_init
    vle8.v v24, (a1)
    li a0, 8
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, vstart_src
    vle8.v v23, (a1)
    li t0, 2
    csrw vstart, t0
    li t2, 0x05
    vmseq.vx v24, v23, t2
    la a1, vstart_out
    vsm.v v24, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0xff, fail_vstart_body
    csrr t1, vstart
    assert_reg_imm t1, 0, fail_vstart_clear

    # Mask-aware vmsbf: with vs2 = 0x08 (first set bit at element 3) and
    # v0.t = 0x0E (elements 1, 2, 3 active), vmsbf must ignore the
    # masked-off element 0 entirely. The first ACTIVE set bit is at
    # element 3, so output bits 1, 2 are 1 (active and before first set)
    # and bit 3 is 0 (active, IS the first set). Bit 0 is disabled so
    # it stays at the destination's existing value (mu policy).
    li a0, 1
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, mask_aware_v0
    vle8.v v0, (a1)
    la a1, mask_aware_vd_init
    vle8.v v25, (a1)
    la a1, mask_aware_src
    vle8.v v26, (a1)
    li a0, 8
    vsetvli t0, a0, e8, m1, ta, mu
    vmsbf.m v25, v26, v0.t
    la a1, mask_aware_out
    vsm.v v25, (a1)
    lbu t1, 0(a1)
    # bit 0 = 0 (preserved from 0xAA's bit 0), bits 1, 2 = 1 (active
    # body, before first set), bit 3 = 0 (active, is first set), bits
    # 4..7 mu-undisturbed (0xAA's bits 4..7 = 1010), so byte = 0xA6.
    assert_reg_imm t1, 0xa6, fail_msbf

    # Mask-aware viota: with vs2 = 0x0F and v0.t = 0x0A, the mask-aware
    # prefix sum only counts vs2 bits at ACTIVE elements:
    #   elem 1: active, count=0 written -> count++ (vs2[1]=1) -> count=1
    #   elem 3: active, count=1 written -> count++ (vs2[3]=1) -> count=2
    # Indexes 0, 2 are mask-disabled; mu policy leaves them at their
    # pre-existing destination values (vd is fresh-zeroed via vse8 init).
    la t0, viota_aware_out
    sb zero, 0(t0)
    sb zero, 1(t0)
    sb zero, 2(t0)
    sb zero, 3(t0)
    li a0, 1
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, viota_v0
    vle8.v v0, (a1)
    li a0, 4
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, viota_aware_out
    vle8.v v27, (a1)
    la a1, viota_src
    vle8.v v28, (a1)
    viota.m v27, v28, v0.t
    la a1, viota_aware_out
    vse8.v v27, (a1)
    lbu t1, 1(a1)
    assert_reg_imm t1, 0, fail_viota1
    lbu t1, 3(a1)
    assert_reg_imm t1, 1, fail_viota3

    # Reduction with vl=0: per V 1.0 §14, vd is unchanged. Pre-load v29
    # with a sentinel scalar in element 0 via vmv.s.x; then run vredsum.vs
    # with vl=0; vd[0] must retain the sentinel.
    li a0, 1
    vsetvli t0, a0, e8, m1, ta, mu
    li t2, 0x77
    vmv.s.x v29, t2
    li a0, 0
    vsetvli t0, a0, e8, m1, ta, mu
    csrr t0, vl
    bnez t0, fail_red_vl0
    # vs1 carries the seed; vs2 is the body. With vl=0 the body is empty
    # and the helper must NOT clobber vd. Use v30 as the seed register
    # holding 0x55 in element 0 - if the buggy path runs, vd[0] would
    # become 0x55 (the seed) instead of 0x77 (the sentinel).
    li a0, 1
    vsetvli t0, a0, e8, m1, ta, mu
    li t2, 0x55
    vmv.s.x v30, t2
    li a0, 0
    vsetvli t0, a0, e8, m1, ta, mu
    vredsum.vs v29, v31, v30
    li a0, 4
    vsetvli t0, a0, e8, m1, ta, mu
    la a1, shift_out
    vse8.v v29, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x77, fail_red_vl0

    li a0, 4
    vsetvli t0, a0, e32, m1, tu, mu
    la a1, fp_vec_a
    vle32.v v24, (a1)
    la a1, fp_vec_b
    vle32.v v25, (a1)

    vfadd.vv v26, v24, v25
    la a1, fp32_out
    vse32.v v26, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x3fc00000, fail_fpadd
    lw t1, 4(a1)
    assert_reg_imm t1, 0x40800000, fail_fpadd
    lw t1, 8(a1)
    assert_reg_imm t1, 0x40400000, fail_fpadd
    lw t1, 12(a1)
    assert_reg_imm t1, 0x41100000, fail_fpadd

    li t2, 0x41800000
    fmv.w.x ft0, t2
    vfrdiv.vf v26, v24, ft0
    la a1, fp32_out
    vse32.v v26, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x41800000, fail_vfrdivf
    lw t1, 4(a1)
    assert_reg_imm t1, 0x41000000, fail_vfrdivf
    lw t1, 12(a1)
    assert_reg_imm t1, 0x40000000, fail_vfrdivf

    li t2, 0x41100000
    fmv.w.x ft1, t2
    vfslide1up.vf v26, v24, ft1
    la a1, fp32_out
    vse32.v v26, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x41100000, fail_vfslide1upf
    lw t1, 4(a1)
    assert_reg_imm t1, 0x3f800000, fail_vfslide1upf
    lw t1, 12(a1)
    assert_reg_imm t1, 0x40800000, fail_vfslide1upf

    vfslide1down.vf v26, v24, ft1
    la a1, fp32_out
    vse32.v v26, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x40000000, fail_vfslide1downf
    lw t1, 8(a1)
    assert_reg_imm t1, 0x41000000, fail_vfslide1downf
    lw t1, 12(a1)
    assert_reg_imm t1, 0x41100000, fail_vfslide1downf

    vfmv.v.f v26, ft1
    la a1, fp32_out
    vse32.v v26, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x41100000, fail_vfmv_v_f
    lw t1, 12(a1)
    assert_reg_imm t1, 0x41100000, fail_vfmv_v_f

    li t2, 0x40400000
    fmv.w.x ft2, t2
    vmflt.vf v27, v24, ft2
    la a1, mask_out
    vsm.v v27, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x03, fail_vmfltf

    li t2, 0x40000000
    fmv.w.x ft3, t2
    la a1, fp_acc
    vle32.v v27, (a1)
    vfmacc.vf v27, ft3, v25
    la a1, fp32_out
    vse32.v v27, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x41300000, fail_vfmaccf
    lw t1, 4(a1)
    assert_reg_imm t1, 0x41c00000, fail_vfmaccf
    lw t1, 8(a1)
    assert_reg_imm t1, 0x41e00000, fail_vfmaccf
    lw t1, 12(a1)
    assert_reg_imm t1, 0x42280000, fail_vfmaccf

    li t2, 0x3f800000
    fmv.w.x ft4, t2
    vfmv.v.f v27, ft4
    vfredusum.vs v26, v25, v27
    la a1, fp32_out
    vse32.v v26, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x40600000, fail_vfredusumf

    vfwadd.vv v28, v24, v25
    la a1, fp64_out
    vse64.v v28, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x00000000, fail_vfwaddf
    lw t1, 4(a1)
    assert_reg_imm t1, 0x3ff80000, fail_vfwaddf
    lw t1, 8(a1)
    assert_reg_imm t1, 0x00000000, fail_vfwaddf
    lw t1, 12(a1)
    assert_reg_imm t1, 0x40100000, fail_vfwaddf

    la a1, fp_wide_seed
    vle64.v v30, (a1)
    vfwmacc.vf v30, ft3, v24
    la a1, fp64_out
    vse64.v v30, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x00000000, fail_vfwmaccf
    lw t1, 4(a1)
    assert_reg_imm t1, 0x40080000, fail_vfwmaccf
    lw t1, 8(a1)
    assert_reg_imm t1, 0x00000000, fail_vfwmaccf
    lw t1, 12(a1)
    assert_reg_imm t1, 0x40180000, fail_vfwmaccf

    la a1, fp_wide_seed
    vle64.v v30, (a1)
    vfwredusum.vs v28, v24, v30
    la a1, fp64_out
    vse64.v v28, (a1)
    lw t1, 0(a1)
    assert_reg_imm t1, 0x00000000, fail_vfwredusumf
    lw t1, 4(a1)
    assert_reg_imm t1, 0x40300000, fail_vfwredusumf

    li a0, 8
    vsetvli t0, a0, e8, mf2, tu, mu
    csrr t1, vl
    assert_reg_imm t1, 8, fail_fractional
    vid.v v1
    la a1, fp32_out
    vse8.v v1, (a1)
    lbu t1, 0(a1)
    assert_reg_imm t1, 0x00, fail_fractional
    lbu t1, 7(a1)
    assert_reg_imm t1, 0x07, fail_fractional

    li a0, 4
    vsetvli t0, a0, e16, mf2, tu, mu
    csrr t1, vl
    assert_reg_imm t1, 4, fail_fractional

    la a1, ok_msg
    li a2, ok_msg_len
    j write_and_exit_ok

/*
 * Per-test failure labels. Each loads a distinct exit code into a3 so a
 * failing run can be bisected via the shell exit status without changing
 * the user-visible message. The exit codes are deliberately spaced (0xeN
 * for the EEW>SEW round-trip, 0xaN for the spec-aware mask/vstart tests)
 * so a future regression points at the offending instruction directly.
 */
fail:
    li a3, 1
    j write_and_exit_fail
fail_eew1:
    li a3, 0xe1
    j write_and_exit_fail
fail_eew2:
    li a3, 0xe2
    j write_and_exit_fail
fail_eew3:
    li a3, 0xe3
    j write_and_exit_fail
fail_eew4:
    li a3, 0xe4
    j write_and_exit_fail
fail_vstart_body:
    li a3, 0xa1
    j write_and_exit_fail
fail_vstart_clear:
    li a3, 0xa2
    j write_and_exit_fail
fail_msbf:
    li a3, 0xa3
    j write_and_exit_fail
fail_viota1:
    li a3, 0xa4
    j write_and_exit_fail
fail_viota3:
    li a3, 0xa5
    j write_and_exit_fail
fail_red_vl0:
    li a3, 0xa6
    j write_and_exit_fail
fail_vadc:
    li a3, 0xb1
    j write_and_exit_fail
fail_vmadc:
    li a3, 0xb2
    j write_and_exit_fail
fail_vsbc:
    li a3, 0xb3
    j write_and_exit_fail
fail_vmsbc:
    li a3, 0xb4
    j write_and_exit_fail
fail_vsrl_vv:
    li a3, 0xb5
    j write_and_exit_fail
fail_vsra_vx:
    li a3, 0xb6
    j write_and_exit_fail
fail_vsrl_vi:
    li a3, 0xb7
    j write_and_exit_fail
fail_vsaddu:
    li a3, 0xca
    j write_and_exit_fail
fail_vsadd:
    li a3, 0xcb
    j write_and_exit_fail
fail_vssrl:
    li a3, 0xcc
    j write_and_exit_fail
fail_vssra:
    li a3, 0xcd
    j write_and_exit_fail
fail_vsmul:
    li a3, 0xce
    j write_and_exit_fail
fail_vsmul0:
    li a3, 0xd0
    j write_and_exit_fail
fail_vsmul1:
    li a3, 0xd1
    j write_and_exit_fail
fail_vsmul2:
    li a3, 0xd2
    j write_and_exit_fail
fail_vsmul3:
    li a3, 0xd3
    j write_and_exit_fail
fail_vsmul4:
    li a3, 0xd4
    j write_and_exit_fail
fail_vwadd:
    li a3, 0xd5
    j write_and_exit_fail
fail_vwmulu:
    li a3, 0xd6
    j write_and_exit_fail
fail_vwmulu0:
    li a3, 0xdb
    j write_and_exit_fail
fail_vwmulu1:
    li a3, 0xdc
    j write_and_exit_fail
fail_vnsrl:
    li a3, 0xd7
    j write_and_exit_fail
fail_vnsrl0:
    li a3, 0xdd
    j write_and_exit_fail
fail_vnsrl1:
    li a3, 0xde
    j write_and_exit_fail
fail_vnclipu:
    li a3, 0xd8
    j write_and_exit_fail
fail_vnclipu0:
    li a3, 0xdf
    j write_and_exit_fail
fail_vnclipu1:
    li a3, 0xe0
    j write_and_exit_fail
fail_vnclipu2:
    li a3, 0xe5
    j write_and_exit_fail
fail_vmacc:
    li a3, 0xd9
    j write_and_exit_fail
fail_vmadd:
    li a3, 0xda
    j write_and_exit_fail
fail_vredsum:
    li a3, 0xb8
    j write_and_exit_fail
fail_vwredsumu:
    li a3, 0xb9
    j write_and_exit_fail
fail_vrgather:
    li a3, 0xbc
    j write_and_exit_fail
fail_vrgather_vi:
    li a3, 0xbd
    j write_and_exit_fail
fail_vrgatherei16:
    li a3, 0xbe
    j write_and_exit_fail
fail_vslideup:
    li a3, 0xbf
    j write_and_exit_fail
fail_vslidedown:
    li a3, 0xc0
    j write_and_exit_fail
fail_vslide1up:
    li a3, 0xc1
    j write_and_exit_fail
fail_vslide1down:
    li a3, 0xc2
    j write_and_exit_fail
fail_vcompress:
    li a3, 0xc3
    j write_and_exit_fail
fail_vmv_s_x:
    li a3, 0xba
    j write_and_exit_fail
fail_vmv_x_s:
    li a3, 0xbb
    j write_and_exit_fail
fail_vnmsac:
    li a3, 0xd1
    j write_and_exit_fail
fail_vnmsub:
    li a3, 0xd2
    j write_and_exit_fail
fail_vwmulsu:
    li a3, 0xd3
    j write_and_exit_fail
fail_vwadd_wv:
    li a3, 0xd4
    j write_and_exit_fail
fail_vwmacc:
    li a3, 0xd5
    j write_and_exit_fail
fail_vnsra:
    li a3, 0xd6
    j write_and_exit_fail
fail_vnclipu_rdn:
    li a3, 0xd7
    j write_and_exit_fail
fail_vsmul_w64:
    li a3, 0xd8
    j write_and_exit_fail
fail_fpadd:
    li a3, 0xe5
    j write_and_exit_fail
fail_vfrdivf:
    li a3, 0xe6
    j write_and_exit_fail
fail_vfslide1upf:
    li a3, 0xe7
    j write_and_exit_fail
fail_vfslide1downf:
    li a3, 0xe8
    j write_and_exit_fail
fail_vfmv_v_f:
    li a3, 0xe9
    j write_and_exit_fail
fail_vmfltf:
    li a3, 0xea
    j write_and_exit_fail
fail_vfmaccf:
    li a3, 0xeb
    j write_and_exit_fail
fail_vfredusumf:
    li a3, 0xec
    j write_and_exit_fail
fail_vfwaddf:
    li a3, 0xed
    j write_and_exit_fail
fail_vfwmaccf:
    li a3, 0xee
    j write_and_exit_fail
fail_vfwredusumf:
    li a3, 0xef
    j write_and_exit_fail
fail_fractional:
    li a3, 0xf0
    j write_and_exit_fail

write_and_exit_fail:
    la a1, fail_msg
    li a2, fail_msg_len
    li a7, SYSWRITE
    li a0, 1
    ecall
    li a7, SYSEXIT
    mv a0, a3
    ecall

write_and_exit_ok:
    li a7, SYSWRITE
    li a0, 1
    ecall
    li a7, SYSEXIT
    li a0, 0
    ecall
