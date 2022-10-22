/*
 * QR123: minimal fast QR encoder for version 1, 2, 3.
 *
 * Copyright (c) 2019 Ling LI <lix2ng@gmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Verify results here:
 *    https://www.nayuki.io/page/creating-a-qr-code-step-by-step
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned uint;

/*
 * QR_OPT: use log/exp LUT-based GF MUL.
 */
#define QR_OPT 1

#define QR_LINES 29

typedef struct qr_ctx {
    uint8_t size;            // 21, 25 or 29 (ver*4+17)
    uint8_t len;             // length of input data.
    const uint8_t *data;     // input data.
    void *params;            // data and ECC parameters.
    uint32_t bmp[QR_LINES];  // QR code bitmap, 1 word per line.
} qr_ctx;

/*
 * Get dots for display.
 */
static inline bool qr_getdot(qr_ctx *ctx, uint x, uint y)
{
    return ctx->bmp[y] << x >> 31;
}

/*
 * Draw finders, timing pattern, alignment pattern, and the dark dot.
 * And now the format bits for fixed mask 0.
 */
static void _init_bmp(uint32_t A[], uint size)
{
    /* Draw top-left finder. */
    A[0] = A[6] = 0xFE000000;
    A[1] = A[5] = 0x82000000;
    A[2] = A[3] = A[4] = 0xBA000000;

    /* Replicate to bottom-left then top-right. */
    int y;
    for (y = 0; y < 7; y++) {
        A[size - 1 - y] = A[y];
        A[y] |= A[y] >> (size - 7);
    }

    /* Horizontal timing pattern. */
    A[6] |= 0xAAA800;

    /* Vertical timing pattern. */
    for (y = 9; y < size - 7; y++)
        A[y] = ((y + 1) & 1) << 25;

    /* The dark dot, then some format bits. */
    y -= 1;  // size-9
    for (int i = 0; i < 8; i++) {
        if (i == 4)
            continue;
        A[y + i] |= 0x800000;
    }

    /* More format bits. */
    A[2] |= 0x800000;
    A[7] = 0x800000;
    A[8] = 0xEF800000 | 0x31 << (34 - size);

    /* Alignment pattern for version 2 & 3. */
    if (size > 21) {
        uint pat = 0x1F << (36 - size);
        A[size - 9] |= pat;
        A[size - 5] |= pat;
        pat = 0x11 << (36 - size);
        A[size - 8] |= pat;
        A[size - 6] |= pat;
        A[size - 7] |= 0x15 << (36 - size);
    }
}

typedef struct qr_params {
    uint8_t capa;   /* total capacity in bytes. */
    uint8_t eccdeg; /* ECC degree/byte count. */
    uint8_t gen[];  /* ECC generator polynomial. */
} qr_params;

/*
 * Check capacity then setup parameters.
 *
 * Return false if version number is invalid or input exceeds the capacity of
 * specified version.
 *  - Parameters are written to the context.
 *  - Must evaluate before encoding.
 *  - Must fail if evaluation fails.
 *  Capacity: V1 17B, V2 32B, V3 53B.
 */
bool qr_eval(qr_ctx *ctx, uint ver, const uint8_t *data, uint len)
{
    static const uint8_t _params_blob[] = {
        //
        26,   7,    0x7f, 0x7a, 0x9a, 0xa4, 0x0b, 0x44, 0x75,  // V1
        44,   10,   0xd8, 0xc2, 0x9f, 0x6f, 0xc7, 0x5e, 0x5f,
        0x71, 0x9d, 0xc1,  // V2
        70,   15,   0x1d, 0xc4, 0x6f, 0xa3, 0x70, 0x4a, 0x0a,
        0x69, 0x69, 0x8b,  // V3
        0x84, 0x97, 0x20, 0x86, 0x1a};

    if (!ctx)
        return false;
    ctx->data = data;
    ctx->len = len;

    uintptr_t params = (uintptr_t) _params_blob; /* intentional */
    /* Skip-overs, cross check with the blob layout. */
    switch (ver) {
    case 1:
        break;
    case 2:
        params += 9;
        break;
    case 3:
        params += 21;
        break;
    default:
        return false;
    }

    uint size = ver * 4 + 17;
    ctx->params = (void *) params;
    /* 4b mode, 8b count, 4b terminator. */
    uint usable =
        ((qr_params *) params)->capa - ((qr_params *) params)->eccdeg - 2;
    if (usable < len)
        return false;

    ctx->size = size;
    _init_bmp(ctx->bmp, ctx->size);
    return true;
}

/*
 * Prepare all the data bits before ECC.
 */
static void _serialize_data(qr_ctx *ctx, uint8_t *buf)
{
    /* Mode bits and length bits. */
    uint b = 4 << 8 | ctx->len;
    buf[0] = b >> 4;
    uint i = 0;
    while (i < ctx->len) {
        b <<= 8;
        b |= ctx->data[i++];
        buf[i] = b >> 4;
    }

    /* Final 4 bits with terminator. */
    i++;
    buf[i++] = b << 4;

    /* Byte padding. */
    b = 0xEC;
    qr_params *para = (qr_params *) ctx->params;
    while (i < para->capa - para->eccdeg) {
        buf[i++] = b;
        b ^= 0xFD; /* alternating EC, 11. */
    }

    /* Clear out the rest bytes for ECC; also clear 1 extra byte for the spare
     * bits (v2 & v3 have 7 unused).
     */
    while (i <= para->capa)
        buf[i++] = 0;
}

#if defined(QR_OPT)
/*
 * The GF(2^8, 285) finite field element multiplication.
 * Basic iterative, unrolled, and log/exp LUT versions are implemented.
 */
static inline uint _rs_mul(uint x, uint y)
{
    static const uint8_t _luts[2][256] = {
        // Log table.
        {0,   0,   1,   25,  2,   50,  26,  198, 3,   223, 51,  238, 27,  104,
         199, 75,  4,   100, 224, 14,  52,  141, 239, 129, 28,  193, 105, 248,
         200, 8,   76,  113, 5,   138, 101, 47,  225, 36,  15,  33,  53,  147,
         142, 218, 240, 18,  130, 69,  29,  181, 194, 125, 106, 39,  249, 185,
         201, 154, 9,   120, 77,  228, 114, 166, 6,   191, 139, 98,  102, 221,
         48,  253, 226, 152, 37,  179, 16,  145, 34,  136, 54,  208, 148, 206,
         143, 150, 219, 189, 241, 210, 19,  92,  131, 56,  70,  64,  30,  66,
         182, 163, 195, 72,  126, 110, 107, 58,  40,  84,  250, 133, 186, 61,
         202, 94,  155, 159, 10,  21,  121, 43,  78,  212, 229, 172, 115, 243,
         167, 87,  7,   112, 192, 247, 140, 128, 99,  13,  103, 74,  222, 237,
         49,  197, 254, 24,  227, 165, 153, 119, 38,  184, 180, 124, 17,  68,
         146, 217, 35,  32,  137, 46,  55,  63,  209, 91,  149, 188, 207, 205,
         144, 135, 151, 178, 220, 252, 190, 97,  242, 86,  211, 171, 20,  42,
         93,  158, 132, 60,  57,  83,  71,  109, 65,  162, 31,  45,  67,  216,
         183, 123, 164, 118, 196, 23,  73,  236, 127, 12,  111, 246, 108, 161,
         59,  82,  41,  157, 85,  170, 251, 96,  134, 177, 187, 204, 62,  90,
         203, 89,  95,  176, 156, 169, 160, 81,  11,  245, 22,  235, 122, 117,
         44,  215, 79,  174, 213, 233, 230, 231, 173, 232, 116, 214, 244, 234,
         168, 80,  88,  175},
        // Exponential table.
        {1,   2,   4,   8,   16,  32,  64,  128, 29,  58,  116, 232, 205, 135,
         19,  38,  76,  152, 45,  90,  180, 117, 234, 201, 143, 3,   6,   12,
         24,  48,  96,  192, 157, 39,  78,  156, 37,  74,  148, 53,  106, 212,
         181, 119, 238, 193, 159, 35,  70,  140, 5,   10,  20,  40,  80,  160,
         93,  186, 105, 210, 185, 111, 222, 161, 95,  190, 97,  194, 153, 47,
         94,  188, 101, 202, 137, 15,  30,  60,  120, 240, 253, 231, 211, 187,
         107, 214, 177, 127, 254, 225, 223, 163, 91,  182, 113, 226, 217, 175,
         67,  134, 17,  34,  68,  136, 13,  26,  52,  104, 208, 189, 103, 206,
         129, 31,  62,  124, 248, 237, 199, 147, 59,  118, 236, 197, 151, 51,
         102, 204, 133, 23,  46,  92,  184, 109, 218, 169, 79,  158, 33,  66,
         132, 21,  42,  84,  168, 77,  154, 41,  82,  164, 85,  170, 73,  146,
         57,  114, 228, 213, 183, 115, 230, 209, 191, 99,  198, 145, 63,  126,
         252, 229, 215, 179, 123, 246, 241, 255, 227, 219, 171, 75,  150, 49,
         98,  196, 149, 55,  110, 220, 165, 87,  174, 65,  130, 25,  50,  100,
         200, 141, 7,   14,  28,  56,  112, 224, 221, 167, 83,  166, 81,  162,
         89,  178, 121, 242, 249, 239, 195, 155, 43,  86,  172, 69,  138, 9,
         18,  36,  72,  144, 61,  122, 244, 245, 247, 243, 251, 235, 203, 139,
         11,  22,  44,  88,  176, 125, 250, 233, 207, 131, 27,  54,  108, 216,
         173, 71,  142, 1},
    };

    if (!x || !y)
        return 0;
    uint xp = _luts[0][x] + _luts[0][y];
    if (xp > 255)
        xp -= 255;
    return _luts[1][xp];
}

#else /* use iterative GF MUL */
static inline uint _rs_mul(uint x, uint y)
{
    uint z = 0;
    /* This is called (133, 340, 825) times in V(1, 2, 3) ECC calculation. */
    for (int i = 7; i >= 0; i--) {
        /* And this body is run 1064, 2720, 6600 times. */
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return z;
}
#endif

/*
 * Calculate the ECC bytes.
 */
static void _reed_solomon(qr_ctx *ctx, uint8_t *buf)
{
    qr_params *para = (qr_params *) ctx->params;
    uint deg = para->eccdeg;
    uint8_t *gen = para->gen;
    uint len = para->capa - para->eccdeg;
    uint8_t *res = buf + len;
    for (uint i = 0; i < len; i++) {
        uint factor = buf[i] ^ res[0];
        for (uint j = 1; j < deg; j++)
            res[j - 1] = res[j];
        res[deg - 1] = 0;
        for (uint j = 0; j < deg; j++)
            res[j] ^= _rs_mul(gen[j], factor);
    }
}

/*
 * Return if dot (x,y) is for data (i.e. not function patterns).
 */
static inline bool _is_data(uint x, uint y, uint size_m1)
{
    if (x == 6 || y == 6)
        return false;
    if (y <= 8)
        return x >= 9 && x <= size_m1 - 8;
    if (y >= size_m1 - 7 && x <= 8)
        return false;
    if (size_m1 > 20 && x >= size_m1 - 8 && x <= size_m1 - 4 &&
        y >= size_m1 - 8 && y <= size_m1 - 4)
        return false;
    return true;
}

typedef union poly64_t {
    uint64_t bits;
    uint32_t u[2];
    struct {
        uint32_t u0;
        uint32_t u1;
    };
} poly64_t;

/*
 * The QR zig-zag sequence generator.
 *
 * To iterate through all valid data positions: call with initial x and y =
 * size-1, feed back the result as input, and repeat the call-feed cycle.
 */
static uint64_t zigzag_step(uint x, uint y, uint size_m1)
{
    poly64_t r;
    while (true) {
        switch ((x - (x > 6)) & 3) {
        case 0:
            if (y < size_m1)
                x += 1, y += 1;
            else
                x -= 1;
            break;
        case 1:
            x -= 1;
            break;
        case 2:
            if (y > 0)
                x += 1, y -= 1;
            else {
                x -= 1;
                if (x == 6)
                    x = 5;
            }
            break;
        default:
            x -= 1;
        }
        if (_is_data(x, y, size_m1))
            break;
    }
    r.u0 = x, r.u1 = y;
    return r.bits;
}

/*
 * Put data bits to the QR bitmap.
 * Fixed masking (0) is applied on the fly.
 */
static void _place_data(qr_ctx *ctx, const uint8_t *buf)
{
    uint size_m1 = ctx->size - 1;
    poly64_t xy = {.u0 = size_m1, .u1 = size_m1};
    qr_params *para = (qr_params *) ctx->params;
    uint nbits = para->capa * 8;

    /* NB: count in the unused bits in V2 and V3. */
    if (size_m1 > 20)
        nbits += 7;

    for (int i = 0; i < nbits; i++) {
        bool mask0 = (xy.u0 + xy.u1) % 2 == 0;
        bool dot = buf[i / 8] & (0x80u >> i % 8);
        if (dot ^ mask0)
            ctx->bmp[xy.u1] |= 0x80000000u >> xy.u0;
        xy.bits = zigzag_step(xy.u0, xy.u1, size_m1);
    }
}

/*
 * The actual encoding.
 */
void qr_encode(qr_ctx *ctx)
{
    if (!ctx)
        return;

    uint8_t dbuf[72];  // V3 capacity 70 and extra 2.
    _serialize_data(ctx, dbuf);
    _reed_solomon(ctx, dbuf);
    _place_data(ctx, dbuf);
}

void dump_bmp(qr_ctx *ctx)
{
    for (int i = 0; i < ctx->size + 2; i++)
        printf("██");
    printf("\n");

    for (int y = 0; y < ctx->size; y++) {
        printf("██");
        for (int x = 0; x < ctx->size; x++)
            if (qr_getdot(ctx, x, y))
                printf("  ");
            else
                printf("██");
        printf("██\n");
    }
    for (int i = 0; i < ctx->size + 2; i++)
        printf("██");
    printf("\n");
}

int main(int argc, const char *argv[])
{
    qr_ctx ctx[1];
    const char *str = "https://github.com/sysprog21/rv32emu";

    if (!qr_eval(ctx, /* version */ 3, (const uint8_t *) str, strlen(str))) {
        printf("Evaluation failed. Version invalid or data too long?\n");
        return -2;
    }
    qr_encode(ctx);
    dump_bmp(ctx);
    return 0;
}
