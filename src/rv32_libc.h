#pragma once

struct rv32libc_impl {
    const uint32_t *insn;
    size_t len;
};

static const uint32_t memset0_insn[] = {
    0x00f00313, /* li	t1,15 */
    0x00050713, /* mv	a4,a0 */
    0x02c37e63, /* bgeu	t1,a2,0x11828 */
    0x00f77793, /* and	a5,a4,15 */
    0x0a079063, /* bnez	a5,0x11894 */
    0x08059263, /* bnez	a1,0x1187c */
    0xff067693, /* and	a3,a2,-16 */
    0x00f67613, /* and	a2,a2,15 */
    0x00e686b3, /* add	a3,a3,a4 */
    0x00b72023, /* sw	a1,0(a4) */
    0x00b72223, /* sw	a1,4(a4) */
    0x00b72423, /* sw	a1,8(a4) */
    0x00b72623, /* sw	a1,12(a4) */
    0x01070713, /* add	a4,a4,16 */
    0xfed766e3, /* bltu	a4,a3,0x11808 */
    0x00061463, /* bnez	a2,0x11828 */
    0x00008067, /* ret */
    0x40c306b3, /* sub	a3,t1,a2 */
    0x00269693, /* sll	a3,a3,0x2 */
    0x00000297, /* auipc	t0,0x0 */
    0x005686b3, /* add	a3,a3,t0 */
    0x00c68067, /* jr	12(a3) */
    0x00b70723, /* sb	a1,14(a4) */
    0x00b706a3, /* sb	a1,13(a4) */
    0x00b70623, /* sb	a1,12(a4) */
    0x00b705a3, /* sb	a1,11(a4) */
    0x00b70523, /* sb	a1,10(a4) */
    0x00b704a3, /* sb	a1,9(a4) */
    0x00b70423, /* sb	a1,8(a4) */
    0x00b703a3, /* sb	a1,7(a4) */
    0x00b70323, /* sb	a1,6(a4) */
    0x00b702a3, /* sb	a1,5(a4) */
    0x00b70223, /* sb	a1,4(a4) */
    0x00b701a3, /* sb	a1,3(a4) */
    0x00b70123, /* sb	a1,2(a4) */
    0x00b700a3, /* sb	a1,1(a4) */
    0x00b70023, /* sb	a1,0(a4) */
    0x00008067, /* ret */
    0x0ff5f593, /* zext.b	a1,a1 */
    0x00859693, /* sll	a3,a1,0x8 */
    0x00d5e5b3, /* or	a1,a1,a3 */
    0x01059693, /* sll	a3,a1,0x10 */
    0x00d5e5b3, /* or	a1,a1,a3 */
    0xf6dff06f, /* j	0x117fc */
    0x00279693, /* sll	a3,a5,0x2 */
    0x00000297, /* auipc	t0,0x0 */
    0x005686b3, /* add	a3,a3,t0 */
    0x00008293, /* mv	t0,ra */
    0xfa0680e7, /* jalr	-96(a3) */
    0x00028093, /* mv	ra,t0 */
    0xff078793, /* add	a5,a5,-16 */
    0x40f70733, /* sub	a4,a4,a5 */
    0x00f60633, /* add	a2,a2,a5 */
    0xf6c378e3, /* bgeu	t1,a2,0x11828 */
    0xf3dff06f, /* j	0x117f8 */
};

static const uint32_t memset1_insn[] = {
    0x00050313, /* mv	t1,a0 */
    0x00060a63, /* beqz	a2, 0x18 */
    0x00b30023, /* sb	a1,0(t1) */
    0xfff60613, /* add	a2,a2,-1 */
    0x00130313, /* add	t1,t1,1 */
    0xfe061ae3, /* bnez	a2, 0x8 */
    0x00008067, /* ret */
};

static const uint32_t memcpy0_insn[] = {
    0x00b547b3, /* xor	a5,a0,a1 */
    0x0037f793, /* and	a5,a5,3 */
    0x00c508b3, /* add	a7,a0,a2 */
    0x06079463, /* bnez	a5,0x21428 */
    0x00300793, /* li	a5,3 */
    0x06c7f063, /* bgeu	a5,a2,0x21428 */
    0x00357793, /* and	a5,a0,3 */
    0x00050713, /* mv	a4,a0 */
    0x06079a63, /* bnez	a5,0x21448 */
    0xffc8f613, /* and	a2,a7,-4 */
    0x40e606b3, /* sub	a3,a2,a4 */
    0x02000793, /* li	a5,32 */
    0x08d7ce63, /* blt	a5,a3,0x21480 */
    0x00058693, /* mv	a3,a1 */
    0x00070793, /* mv	a5,a4 */
    0x02c77863, /* bgeu	a4,a2,0x21420 */
    0x0006a803, /* lw	a6,0(a3) */
    0x00478793, /* add	a5,a5,4 */
    0x00468693, /* add	a3,a3,4 */
    0xff07ae23, /* sw	a6,-4(a5) */
    0xfec7e8e3, /* bltu	a5,a2,0x213f4 */
    0xfff60793, /* add	a5,a2,-1 */
    0x40e787b3, /* sub	a5,a5,a4 */
    0xffc7f793, /* and	a5,a5,-4 */
    0x00478793, /* add	a5,a5,4 */
    0x00f70733, /* add	a4,a4,a5 */
    0x00f585b3, /* add	a1,a1,a5 */
    0x01176863, /* bltu	a4,a7,0x21430 */
    0x00008067, /* ret */
    0x00050713, /* mv	a4,a0 */
    0x05157863, /* bgeu	a0,a7,0x2147c */
    0x0005c783, /* lbu	a5,0(a1) */
    0x00170713, /* add	a4,a4,1 */
    0x00158593, /* add	a1,a1,1 */
    0xfef70fa3, /* sb	a5,-1(a4) */
    0xfee898e3, /* bne	a7,a4,0x21430 */
    0x00008067, /* ret */
    0x0005c683, /* lbu	a3,0(a1) */
    0x00170713, /* add	a4,a4,1 */
    0x00377793, /* and	a5,a4,3 */
    0xfed70fa3, /* sb	a3,-1(a4) */
    0x00158593, /* add	a1,a1,1 */
    0xf6078ee3, /* beqz	a5,0x213d8 */
    0x0005c683, /* lbu	a3,0(a1) */
    0x00170713, /* add	a4,a4,1 */
    0x00377793, /* and	a5,a4,3 */
    0xfed70fa3, /* sb	a3,-1(a4) */
    0x00158593, /* add	a1,a1,1 */
    0xfc079ae3, /* bnez	a5,0x21448 */
    0xf61ff06f, /* j	0x213d8 */
    0x00008067, /* ret */
    0xff010113, /* add	sp,sp,-16 */
    0x00812623, /* sw	s0,12(sp) */
    0x02000413, /* li	s0,32 */
    0x0005a383, /* lw	t2,0(a1) */
    0x0045a283, /* lw	t0,4(a1) */
    0x0085af83, /* lw	t6,8(a1) */
    0x00c5af03, /* lw	t5,12(a1) */
    0x0105ae83, /* lw	t4,16(a1) */
    0x0145ae03, /* lw	t3,20(a1) */
    0x0185a303, /* lw	t1,24(a1) */
    0x01c5a803, /* lw	a6,28(a1) */
    0x0205a683, /* lw	a3,32(a1) */
    0x02470713, /* add	a4,a4,36 */
    0x40e607b3, /* sub	a5,a2,a4 */
    0xfc772e23, /* sw	t2,-36(a4) */
    0xfe572023, /* sw	t0,-32(a4) */
    0xfff72223, /* sw	t6,-28(a4) */
    0xffe72423, /* sw	t5,-24(a4) */
    0xffd72623, /* sw	t4,-20(a4) */
    0xffc72823, /* sw	t3,-16(a4) */
    0xfe672a23, /* sw	t1,-12(a4) */
    0xff072c23, /* sw	a6,-8(a4) */
    0xfed72e23, /* sw	a3,-4(a4) */
    0x02458593, /* add	a1,a1,36 */
    0xfaf446e3, /* blt	s0,a5,0x2148c */
    0x00058693, /* mv	a3,a1 */
    0x00070793, /* mv	a5,a4 */
    0x02c77863, /* bgeu	a4,a2,0x2151c */
    0x0006a803, /* lw	a6,0(a3) */
    0x00478793, /* add	a5,a5,4 */
    0x00468693, /* add	a3,a3,4 */
    0xff07ae23, /* sw	a6,-4(a5) */
    0xfec7e8e3, /* bltu	a5,a2,0x214f0 */
    0xfff60793, /* add	a5,a2,-1 */
    0x40e787b3, /* sub	a5,a5,a4 */
    0xffc7f793, /* and	a5,a5,-4 */
    0x00478793, /* add	a5,a5,4 */
    0x00f70733, /* add	a4,a4,a5 */
    0x00f585b3, /* add	a1,a1,a5 */
    0x01176863, /* bltu	a4,a7,0x2152c */
    0x00c12403, /* lw	s0,12(sp) */
    0x01010113, /* add	sp,sp,16 */
    0x00008067, /* ret */
    0x0005c783, /* lbu	a5,0(a1) */
    0x00170713, /* add	a4,a4,1 */
    0x00158593, /* add	a1,a1,1 */
    0xfef70fa3, /* sb	a5,-1(a4) */
    0xfee882e3, /* beq	a7,a4,0x21520 */
    0x0005c783, /* lbu	a5,0(a1) */
    0x00170713, /* add	a4,a4,1 */
    0x00158593, /* add	a1,a1,1 */
    0xfef70fa3, /* sb	a5,-1(a4) */
    0xfce89ee3, /* bne	a7,a4,0x2152c */
    0xfcdff06f, /* j	0x21520 */
};

static const uint32_t memcpy1_insn[] = {
    0x00050313, /* mv	t1,a0 */
    0x00060e63, /* beqz	a2,44d18 */
    0x00058383, /* lb	t2,0(a1) */
    0x00730023, /* sb	t2,0(t1) */
    0xfff60613, /* add	a2,a2,-1 */
    0x00130313, /* add	t1,t1,1 */
    0x00158593, /* add	a1,a1,1 */
    0xfe0616e3, /* bnez	a2,44d00 */
    0x00008067, /* ret */
};
