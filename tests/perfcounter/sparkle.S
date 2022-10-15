/* Source: https://github.com/AsmOptC-RiscV/Assembly-Optimized-C-RiscV */

.data
.align 2

RCON:
    .word 0xB7E15162    // 0
    .word 0xBF715880    // 1
    .word 0x38B4DA56    // 2
    .word 0x324E7738    // 3
    .word 0xBB1185EB    // 4
    .word 0x4F7C7B57    // 5
    .word 0xCFBFA1C8    // 6
    .word 0xC2B3293D    // 7
    .word 0xB7E15162    // 0
    .word 0xBF715880    // 1
    .word 0x38B4DA56    // 2
    .word 0x324E7738    // 3

.text
.align 1

// rotate right: tmp0 = reg >>> dist
// info: 2 tmp register, because we dont want to overwrite reg,
// otherwise only 1 tmp reg would be needed
.macro ror_tmp tmp0,tmp1,reg,val
    srli    \tmp0, \reg, \val
    slli    \tmp1, \reg, 32-\val
    xor     \tmp0, \tmp0, \tmp1
.endm

// rotate right: reg = reg >>> dist
.macro ror reg,tmp0,val
    srli    \tmp0, \reg, \val
    slli    \reg, \reg, 32-\val
    xor     \reg, \reg, \tmp0
.endm

// ELL(x) (ROT(((x) ^ ((x) << 16)), 16))
.macro ell x,tmp0
    slli    \tmp0, \x, 16
    xor     \x, \x, \tmp0
    ror     \x, \tmp0, 16
.endm

// ARX-box
// input: a0:&x, a1:&y, a2:c // output a0:x
// t0,t1 tmp reg
.macro arxbox_asm x,y,c
    // RISC-V: 33 (12+21) instructions; ARM: 12 instructions
    // ARM needs no tmp register for the rotate
    // RISC-V needs (two) temp register for the rotate

    // round 1
    ror_tmp     t0, t1, \y, 31  // ROT(y, 31);
    add         \x, \x, t0  // x = x + ROT(y, 31);
    ror_tmp     t0, t1, \x, 24  // ROT(x, 24);
    xor         \y, \y, t0  // y = y ^ ROT(x, 24);
    xor         \x, \x, \c  // x = ^ c;

    // round 2
    ror_tmp     t0, t1, \y, 17  // ROT(y, 17);
    add         \x, \x, t0  // x = x + ROT(y, 17);
    ror_tmp     t0, t1, \x, 17  // ROT(x, 17);
    xor         \y, \y, t0  // y = y ^ ROT(x, 17);
    xor         \x, \x, \c  // x = ^ c;

    // round 3
    //ror_tmp     t0, t1, \y, 0  // ROT(y, 0);
    add         \x, \x, \y  // x = x + ROT(y, 0);
    ror_tmp     t0, t1, \x, 31  // ROT(x, 31);
    xor         \y, \y, t0  // y = y ^ ROT(x, 31);
    xor         \x, \x, \c  // x = ^ c;

    // round 4
    ror_tmp     t0, t1, \y, 24  // ROT(y, 24);
    add         \x, \x, t0  // x = x + ROT(y, 24);
    ror_tmp     t0, t1, \x, 16  // ROT(x, 16);
    xor         \y, \y, t0  // y = y ^ ROT(x, 16);
    xor         \x, \x, \c  // x = ^ c;
.endm

// non-linear layer
// t2 tmp reg
/*.macro arxbox_layer
    li          t2, 0xB7E15162
    arxbox_asm  a2, a3, t2
    li          t2, 0xBF715880
    arxbox_asm  a4, a5, t2
    li          t2, 0x38B4DA56
    arxbox_asm  a6, a7, t2
    li          t2, 0x324E7738
    arxbox_asm  t5, t6, t2
    li          t2, 0xBB1185EB
    arxbox_asm  s8, s9, t2
    li          t2, 0x4F7C7B57
    arxbox_asm  s10, s11, t2
.endm*/

// non-linear layer
.macro arxbox_layer_unrolled
    arxbox_asm  a2, a3,   s0
    arxbox_asm  a4, a5,   s1
    arxbox_asm  a6, a7,   s2
    arxbox_asm  t5, t6,   s3
    arxbox_asm  s8, s9,   s4
    arxbox_asm  s10, s11, s5
.endm

// linear layer
// RISC-V: 34 core instructions; ARM: 24 instructions
// t0, t1, t2, t3 tmp reg
.macro linear_layer
    // tx, ty
    xor         t0, a2, a4      // t0 = tx
    xor         t0, t0, a6
    xor         t1, a3, a5      // t1 = ty
    xor         t1, t1, a7
    // ELL
    ell         t0, t2          // l(tx)
    ell         t1, t2          // l(ty)
    // temp reg for x0, y0
    addi        t2, a2, 0       // t2 = x0
    addi        t3, a3, 0       // t3 = y0
    // feistel function xor and branch swap
    xor         a2, a4, t1      // temp = state[2] ^ ty;
    xor         a2, s8, a2      // state[0] = state[8] ^ temp;
    addi        s8, a4, 0       // state[8] = state[2];

    xor         a3, a5, t0      // temp = state[3] ^ tx;
    xor         a3, s9, a3      // state[1] = state[9] ^ temp;
    addi        s9, a5, 0       // state[9] = state[3];

    xor         a4, a6, t1      // temp = state[4] ^ ty;
    xor         a4, s10, a4     // state[2] = state[10] ^ temp;
    addi        s10, a6, 0      // state[10] = state[4];

    xor         a5, a7, t0      // temp = state[5] ^ tx;
    xor         a5, s11, a5     // state[3] = state[11] ^ temp;
    addi        s11, a7, 0      // state[11] = state[5];

    xor         a6, t2, t1      // temp = x0 ^ ty;
    xor         a6, t5, a6      // state[4] = state[6] ^ temp;
    addi        t5, t2, 0       // state[6] = x0;

    xor         a7, t3, t0      // temp = y0 ^ tx;
    xor         a7, t6, a7      // state[5] = state[7] ^ temp;
    addi        t6, t3, 0       // state[7] = y0;
.endm


// AddRoundConstants Loop (AddStepCounter)
// t0 tmp reg
.macro addroundconstants_loop y0,y1,step
    lw          t0, 0(t4)
    add         t4, t4, 4       // &RCON + 4 // instruction pipelining!
    xor         \y1, \y1, \step // y1 = y1 ^ s
    xor         \y0, \y0, t0    // y0 = y0 ^ c_{s mod 8}
.endm


// Sparkle permutation
// void sparkle_asm(unsigned int *state, unsigned int ns)
.globl sparkle_asm
.type sparkle_asm,%function

// variable step loop
// reg stats:
// 11/12 saved reg:
// 4 saved reg s8-s11: state
// 1 saved reg s7: step iterator
// 6 saved reg s0-s5: constants
//
// 8/8 argu reg:
// 6 argu reg a2-a7: state
// 2 argu reg a0: &state, a1: ns
//
// 6/7 temp:
// 2 temp reg t5-t6: state
// 4 temp reg t0-t3: layer
//
// 2/4 various:
// 1 sp, 1 ra
sparkle_asm: // input: a0:&state, a1:ns
    addi        sp, sp, -48  // allocate stack frame
    // store callee saved register
    sw          s0,  4(sp)      // constants
    sw          s1,  8(sp)
    sw          s2,  12(sp)
    sw          s3,  16(sp)
    sw          s4,  20(sp)
    sw          s5,  24(sp)
    sw          s7,  28(sp)  // step counter
    sw          s8,  32(sp)
    sw          s9,  36(sp)
    sw          s10, 40(sp)
    sw          s11, 44(sp)

    la          t4, RCON        // &RCON[0]

    // load complete state from stack
    // selection of register for state -> interesting
    lw          a2, 0(a0)       // load &x0
    lw          a3, 4(a0)       // load &y0
    lw          a4, 8(a0)       // load &x1
    lw          a5, 12(a0)      // load &y1
    lw          a6, 16(a0)      // load &x2
    lw          a7, 20(a0)      // load &y2
    lw          t5, 24(a0)      // load &x3
    lw          t6, 28(a0)      // load &y3
    lw          s8, 32(a0)      // load &x4
    lw          s9, 36(a0)      // load &y4
    lw          s10, 40(a0)     // load &x5
    lw          s11, 44(a0)     // load &y5

    // load all constants used in the non-linear layer
    li          s0, 0xB7E15162
    li          s1, 0xBF715880
    li          s2, 0x38B4DA56
    li          s3, 0x324E7738
    li          s4, 0xBB1185EB
    li          s5, 0x4F7C7B57

    li          s7, 0           // step iterator
Loop:
    // AddRoundConstants
    addroundconstants_loop a3, a5, s7
    // non-linear layer
    arxbox_layer_unrolled
    // linear layer
    linear_layer

    addi        s7, s7, 1       // loop footer
    bne         s7, a1, Loop

    // save complete state to stack
    sw          a2, 0(a0)       // save &x0
    sw          a3, 4(a0)       // save &y0
    sw          a4, 8(a0)       // save &x1
    sw          a5, 12(a0)      // save &y1
    sw          a6, 16(a0)      // save &x2
    sw          a7, 20(a0)      // save &y2
    sw          t5, 24(a0)      // save &x3
    sw          t6, 28(a0)      // save &y3
    sw          s8, 32(a0)      // save &x4
    sw          s9, 36(a0)      // save &y4
    sw          s10, 40(a0)     // save &x5
    sw          s11, 44(a0)     // save &y5

    // load callee saved register
    lw          s0,  4(sp)      // constants
    lw          s1,  8(sp)
    lw          s2,  12(sp)
    lw          s3,  16(sp)
    lw          s4,  20(sp)
    lw          s5,  24(sp)
    lw          s7,  28(sp)     // step iterator
    lw          s8,  32(sp)     //state
    lw          s9,  36(sp)
    lw          s10, 40(sp)
    lw          s11, 44(sp)
    addi        sp, sp, 48      // deallocate stack frame
    ret
