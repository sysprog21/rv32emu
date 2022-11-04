.data

.align 3
chacha20constants:
    .word 0x61707865
    .word 0x3320646e
    .word 0x79622d32
    .word 0x6b206574
chacha20masks:
    .word 0x00000000
    .word 0x000000ff
    .word 0x0000ffff
    .word 0x00ffffff

.text

.macro quarterround a,b,c,d, t
    add     \a, \a, \b
    xor     \d, \d, \a
    slli    \t, \d, 16
    srli    \d, \d, 16
    xor     \d, \d, \t
    add     \c, \c, \d
    xor     \b, \b, \c
    slli    \t, \b, 12
    srli    \b, \b, 20
    xor     \b, \b, \t
    add     \a, \a, \b
    xor     \d, \d, \a
    slli    \t, \d,  8
    srli    \d, \d, 24
    xor     \d, \d, \t
    add     \c, \c, \d
    xor     \b, \b, \c
    slli    \t, \b,  7
    srli    \b, \b, 25
    xor     \b, \b, \t
.endm

.macro tworounds a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p, tmp
    quarterround \a,\e,\i,\m, \tmp
    quarterround \b,\f,\j,\n, \tmp
    quarterround \c,\g,\k,\o, \tmp
    quarterround \d,\h,\l,\p, \tmp
    quarterround \a,\f,\k,\p, \tmp
    quarterround \b,\g,\l,\m, \tmp
    quarterround \c,\h,\i,\n, \tmp
    quarterround \d,\e,\j,\o, \tmp
.endm

.macro chacha20block C, key, nonce, ctr, a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p, tmp0,tmp1
    // load state
    lw      \a,  0(\C)
    lw      \b,  4(\C)
    lw      \c,  8(\C)
    lw      \d, 12(\C)
    lw      \e,  0(\key)
    lw      \f,  4(\key)
    lw      \g,  8(\key)
    lw      \h, 12(\key)
    lw      \i, 16(\key)
    lw      \j, 20(\key)
    lw      \k, 24(\key)
    lw      \l, 28(\key)
    mv      \m, \ctr
    lw      \n, 0(\nonce)
    lw      \o, 4(\nonce)
    lw      \p, 8(\nonce)

    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0
    tworounds \a,\b,\c,\d,\e,\f,\g,\h,\i,\j,\k,\l,\m,\n,\o,\p, \tmp0

    // add initial state
    lw      \tmp0,  0(\C)
    lw      \tmp1,  4(\C)
    add     \a, \a, \tmp0
    add     \b, \b, \tmp1
    lw      \tmp0,  8(\C)
    lw      \tmp1, 12(\C)
    add     \c, \c, \tmp0
    add     \d, \d, \tmp1
    lw      \tmp0,  0(\key)
    lw      \tmp1,  4(\key)
    add     \e, \e, \tmp0
    add     \f, \f, \tmp1
    lw      \tmp0,  8(\key)
    lw      \tmp1, 12(\key)
    add     \g, \g, \tmp0
    add     \h, \h, \tmp1
    lw      \tmp0, 16(\key)
    lw      \tmp1, 20(\key)
    add     \i, \i, \tmp0
    add     \j, \j, \tmp1
    lw      \tmp0, 24(\key)
    lw      \tmp1, 28(\key)
    add     \k, \k, \tmp0
    add     \l, \l, \tmp1
    add     \m, \m, \ctr
    lw      \tmp0, 0(\nonce)
    lw      \tmp1, 4(\nonce)
    add     \n, \n, \tmp0
    lw      \tmp0, 8(\nonce)
    add     \o, \o, \tmp1
    add     \p, \p, \tmp0
.endm

.macro lastwords off, var, tmp0, tmp1
    blt     a2, a3, 3f
    lw      \tmp0, \off(a1)
    addi    a2, a2, -4
    xor     \var, \var, \tmp0
    sw      \var, \off(a0)
    j       4f
3:  bge     zero, a2, 5f
    slli    a2, a2, 2
    add     a2, a2, a4
    lw      \tmp0, \off(a1)
    lw      \tmp1, (a2)
    xor     \var, \var, \tmp0
    and     \var, \var, \tmp1
    sw      \var, \off(a0)
    j       5f
4:
.endm

// void chacha20(uint8_t *out, const uint8_t *in, size_t inlen; const uint8_t *key, const uint8_t *nonce, const uint32_t ctr);
.globl chacha20
.type chacha20,%function
.align 3
chacha20:
// a0 out
// a1 in
// a2 inlen
// a3 key
// a4 nonce
// a5 ctr
// a6-a7,t0-t6,s0-s6 state
// s7 tmp
// s8 constants

// a6,a7,t0,t1,t2,t3,t4,t5,t6,s0,s1,s2,s3,s4,s5,s6,s7,s8
// 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 t, c

    // push s0-s8 to stack
    addi    sp, sp, -44
    sw      s0,  4(sp)
    sw      s1,  8(sp)
    sw      s2, 12(sp)
    sw      s3, 16(sp)
    sw      s4, 20(sp)
    sw      s5, 24(sp)
    sw      s6, 28(sp)
    sw      s7, 32(sp)
    sw      s8, 36(sp)
    sw      s9, 40(sp)

    la      s8, chacha20constants

    // goto 2 if inlen < 64
.align 2
1:  addi    s7, zero, 64
    blt     a2, s7, 2f

    chacha20block s8, a3, a4, a5, a6,a7,t0,t1,t2,t3,t4,t5,t6,s0,s1,s2,s3,s4,s5,s6, s7,s9

    // xor keystream with input
    lw      s7,  0(a1)
    lw      s8,  4(a1)
    xor     a6, a6, s7
    xor     a7, a7, s8
    lw      s7,  8(a1)
    lw      s8, 12(a1)
    xor     t0, t0, s7
    xor     t1, t1, s8
    lw      s7, 16(a1)
    lw      s8, 20(a1)
    xor     t2, t2, s7
    xor     t3, t3, s8
    lw      s7, 24(a1)
    lw      s8, 28(a1)
    xor     t4, t4, s7
    xor     t5, t5, s8
    lw      s7, 32(a1)
    lw      s8, 36(a1)
    xor     t6, t6, s7
    xor     s0, s0, s8
    lw      s7, 40(a1)
    lw      s8, 44(a1)
    xor     s1, s1, s7
    xor     s2, s2, s8
    lw      s7, 48(a1)
    lw      s8, 52(a1)
    xor     s3, s3, s7
    xor     s4, s4, s8
    lw      s7, 56(a1)
    lw      s8, 60(a1)
    xor     s5, s5, s7
    xor     s6, s6, s8

    la      s8, chacha20constants
    // store output
    sw      a6,  0(a0)
    sw      a7,  4(a0)
    sw      t0,  8(a0)
    sw      t1, 12(a0)
    sw      t2, 16(a0)
    sw      t3, 20(a0)
    sw      t4, 24(a0)
    sw      t5, 28(a0)
    sw      t6, 32(a0)
    sw      s0, 36(a0)
    sw      s1, 40(a0)
    sw      s2, 44(a0)
    sw      s3, 48(a0)
    sw      s4, 52(a0)
    sw      s5, 56(a0)
    sw      s6, 60(a0)

    // update
    addi    a0, a0, 64  // output
    addi    a1, a1, 64  // input
    addi    a2, a2, -64 // inlen
    addi    a5, a5, 1   // ctr
    j       1b

    // goto 5 if inlen <= 0
.align 2
2:  bge     zero, a2, 5f

    chacha20block s8, a3, a4, a5, a6,a7,t0,t1,t2,t3,t4,t5,t6,s0,s1,s2,s3,s4,s5,s6, s7,s9

    addi    a3, zero, 4
    la      a4, chacha20masks

    lastwords  0, a6, s7, s8
    lastwords  4, a7, s7, s8
    lastwords  8, t0, s7, s8
    lastwords 12, t1, s7, s8
    lastwords 16, t2, s7, s8
    lastwords 20, t3, s7, s8
    lastwords 24, t4, s7, s8
    lastwords 28, t5, s7, s8
    lastwords 32, t6, s7, s8
    lastwords 36, s0, s7, s8
    lastwords 40, s1, s7, s8
    lastwords 44, s2, s7, s8
    lastwords 48, s3, s7, s8
    lastwords 52, s4, s7, s8
    lastwords 56, s5, s7, s8

.align 2
5:  // done
    // pop s0-s8
    lw      s0,  4(sp)
    lw      s1,  8(sp)
    lw      s2, 12(sp)
    lw      s3, 16(sp)
    lw      s4, 20(sp)
    lw      s5, 24(sp)
    lw      s6, 28(sp)
    lw      s7, 32(sp)
    lw      s8, 36(sp)
    lw      s9, 40(sp)
    addi    sp, sp, 44

    ret
.size chacha20,.-chacha20
