/*
 * Copyright (c) 2023 National Cheng Kung University. All rights reserved.
 * Copyright (C) 2017 Milo Yip. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of pngout nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Source: https://github.com/miloyip/line
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Write a byte */
#define SVPNG_PUT(u) fputc(u, fp)

/*!
 * \brief Save a RGB/RGBA image in PNG format.
 * \param FILE *fp Output stream (by default using file descriptor).
 * \param w Width of the image. (<16383)
 * \param h Height of the image.
 * \param img Image pixel data in 24-bit RGB or 32-bit RGBA format.
 * \param alpha Whether the image contains alpha channel.
 */
void svpng(FILE *fp, unsigned w, unsigned h, const uint8_t *img, bool alpha)
{
    static const unsigned t[] = {
        0,
        0x1db71064,
        0x3b6e20c8,
        0x26d930ac,
        0x76dc4190,
        0x6b6b51f4,
        0x4db26158,
        0x5005713c,
        /* CRC32 Table */ 0xedb88320,
        0xf00f9344,
        0xd6d6a3e8,
        0xcb61b38c,
        0x9b64c2b0,
        0x86d3d2d4,
        0xa00ae278,
        0xbdbdf21c,
    };
    unsigned a = 1, b = 0, c, p = w * (alpha ? 4 : 3) + 1, x, y;

#define SVPNG_U8A(ua, l)           \
    for (size_t i = 0; i < l; i++) \
        SVPNG_PUT((ua)[i]);

#define SVPNG_U32(u)                  \
    do {                              \
        SVPNG_PUT((u) >> 24);         \
        SVPNG_PUT(((u) >> 16) & 255); \
        SVPNG_PUT(((u) >> 8) & 255);  \
        SVPNG_PUT((u) &255);          \
    } while (0)

#define SVPNG_U8C(u)              \
    do {                          \
        SVPNG_PUT(u);             \
        c ^= (u);                 \
        c = (c >> 4) ^ t[c & 15]; \
        c = (c >> 4) ^ t[c & 15]; \
    } while (0)

#define SVPNG_U8AC(ua, l)          \
    for (size_t i = 0; i < l; i++) \
    SVPNG_U8C((ua)[i])

#define SVPNG_U16LC(u)               \
    do {                             \
        SVPNG_U8C((u) &255);         \
        SVPNG_U8C(((u) >> 8) & 255); \
    } while (0)

#define SVPNG_U32C(u)                 \
    do {                              \
        SVPNG_U8C((u) >> 24);         \
        SVPNG_U8C(((u) >> 16) & 255); \
        SVPNG_U8C(((u) >> 8) & 255);  \
        SVPNG_U8C((u) &255);          \
    } while (0)

#define SVPNG_U8ADLER(u)       \
    do {                       \
        SVPNG_U8C(u);          \
        a = (a + (u)) % 65521; \
        b = (b + a) % 65521;   \
    } while (0)

#define SVPNG_BEGIN(s, l) \
    do {                  \
        SVPNG_U32(l);     \
        c = ~0U;          \
        SVPNG_U8AC(s, 4); \
    } while (0)

#define SVPNG_END() SVPNG_U32(~c)

    SVPNG_U8A("\x89PNG\r\n\32\n", 8); /* Magic */
    SVPNG_BEGIN("IHDR", 13);          /* IHDR chunk { */
    SVPNG_U32C(w);
    SVPNG_U32C(h); /*   Width & Height (8 bytes) */
    SVPNG_U8C(8);
    /* Depth=8, Color=True color with/without alpha (2 bytes) */
    SVPNG_U8C(alpha ? 6 : 2);
    /* Compression=Deflate, Filter=No, Interlace=No (3 bytes) */
    SVPNG_U8AC("\0\0\0", 3);
    SVPNG_END();                              /* } */
    SVPNG_BEGIN("IDAT", 2 + h * (5 + p) + 4); /* IDAT chunk { */
    SVPNG_U8AC("\x78\1", 2); /*   Deflate block begin (2 bytes) */
    /*   Each horizontal line makes a block for simplicity */
    for (y = 0; y < h; y++) {
        /* 1 for the last block, 0 for others (1 byte) */
        SVPNG_U8C(y == h - 1);
        SVPNG_U16LC(p);
        /* Size of block in little endian and its 1's complement (4 bytes) */
        SVPNG_U16LC(~p);
        SVPNG_U8ADLER(0); /*   No filter prefix (1 byte) */
        for (x = 0; x < p - 1; x++, img++)
            SVPNG_U8ADLER(*img); /*   Image pixel data */
    }
    SVPNG_U32C((b << 16) | a); /*   Deflate block end with adler (4 bytes) */
    SVPNG_END();               /* } */
    SVPNG_BEGIN("IEND", 0);
    SVPNG_END(); /* IEND chunk {} */
}

#include <string.h>  // memset()
#define Q (20)
#define Q_PI (1686629713 >> (29 - Q))

/* 32-bit Q notation to specify a fixed point number format. */
typedef int32_t qfixed_t;

/* 64-bit Q format buffer, for handling overflow cases */
typedef int64_t qbuf_t;

/* format convertion: float to Q format */
#define f2Q(x) ((qfixed_t) ((x) * (1 << Q)))

/* format convertion: Q format to float */
#define Q2f(x) (((float) (x)) / (1 << Q))

/* format convertion: Q format to int */
#define Q2I(x) ((int) ((x) >> Q))

/* format convertion: int to Q format */
#define I2Q(x) ((qfixed_t) ((x) << Q))

/* maximum value of Q format */
#define QFMT_MAX 0x7FFFFFFF

/* minimum value of Q format */
#define QFMT_MIN 0x80000000

/* addition of Q format value */
static inline qfixed_t q_add(qfixed_t a, qfixed_t b)
{
    qbuf_t tmp = (qbuf_t) a + (qbuf_t) b;
    if (tmp > (qbuf_t) QFMT_MAX)
        return (qfixed_t) QFMT_MAX;
    if (~tmp + 1 >= (qbuf_t) QFMT_MIN)
        return (qfixed_t) QFMT_MIN;
    return (qfixed_t) tmp;
};

/* multiplication of Q format value */
static inline qfixed_t q_mul(qfixed_t a, qfixed_t b)
{
    qbuf_t tmp = (qbuf_t) a * (qbuf_t) b;

    /* rounding and resize */
    tmp += (qbuf_t) (1 << (Q - 1));
    tmp >>= Q;

    /* check overflow */
    if (tmp > (qbuf_t) QFMT_MAX)
        return (qfixed_t) QFMT_MAX;
    if (tmp * -1 >= (qbuf_t) QFMT_MIN)
        return (qfixed_t) QFMT_MIN;
    return (qfixed_t) tmp;
}

/* division of Q format value */
static inline qfixed_t q_div(qfixed_t a, qfixed_t b)
{
    qbuf_t tmp = (qbuf_t) a << Q;
    if ((tmp >= 0 && b >= 0) || (tmp < 0 && b < 0)) {
        tmp += (b >> 1);
    } else {
        tmp -= (b >> 1);
    }
    return (qfixed_t) (tmp / b);
}

/* return the largest integral value that is not greater than x */
static inline qfixed_t q_floor(qfixed_t x)
{
    qfixed_t mask = (0xFFFFFFFF >> Q) << Q;
    return x & mask;
}

/* return the smallest integral value that is not less than x */
static inline qfixed_t q_ceil(qfixed_t x)
{
    qfixed_t mask = (0xFFFFFFFF >> Q) << Q;
    qfixed_t delta = x & ~mask;
    x = x & mask;
    return delta ? q_add(x, 1 << Q) : x;
}

/* return the nonnegative square root of x */
static inline qfixed_t q_sqrt(qfixed_t x)
{
    if (x <= 0)
        return 0;
    if (x < I2Q(1) + (1 << (Q / 2 - 1)) && x > I2Q(1) - (1 << (Q / 2 - 1)))
        return I2Q(1);

    qfixed_t res = 0;
    qfixed_t bit = 1 << 15;

    /* left shift x as more as possible */
    int offset = 0;
    for (qfixed_t i = x; !(0x40000000 & i); i <<= 1)
        ++offset;
    offset = (offset & ~1);
    x <<= offset;

    /* shift bit to the highest bit 1' in x */
    while (bit > x)
        bit >>= 1;

    for (; bit > 0; bit >>= 1) {
        int tmp = bit + res;

        /* check overflow: 46341^2 > 2^31 - 1, which is the maximun value */
        if (tmp > 46340)
            continue;

        int square = tmp * tmp;
        if (square <= x) {
            res = tmp;
            if (square == x)
                break;
        }
    } /* iter: goto next lower bit to get more precise sqrt value */

    offset >>= 1;
    offset -= (Q >> 1);
    return (offset >= 0) ? res >> offset : res << (-offset);
}

/* get both sin and cos value in the same radius */
static inline void q_sincos(qfixed_t radius, qfixed_t *sin_t, qfixed_t *cos_t)
{
    int region = (radius / (Q_PI >> 1)) & 0b11;

    /* theta must be pi/2 to 0 and start from x-axis */
    qfixed_t theta = radius % (Q_PI >> 1);
    if (region & 0b1)
        theta = (Q_PI >> 1) - theta;

    /* start from cos(pi/2) and sin(pi/2) */
    radius = Q_PI >> 1;
    qfixed_t cos_rad = 0;
    qfixed_t sin_rad = I2Q(1);

    /* start from cos(0) and sin(0) */
    *cos_t = I2Q(1);
    *sin_t = 0;

    while (radius > 0) {
        if (radius <= theta) {
            theta -= radius;
            /*
             * Trigonometric Identities:
             * cos(a + b) = cos(a)*cos(b) - sin(a)sin(b)
             * sin(a + b) = sin(a)*cos(b) + cos(a)sin(b)
             */
            qfixed_t tmp = q_mul(*cos_t, cos_rad) - q_mul(*sin_t, sin_rad);
            *sin_t = q_mul(*sin_t, cos_rad) + q_mul(*cos_t, sin_rad);
            *cos_t = tmp;
        }
        if (theta == 0)
            break;

        /* Half angle formula to approach the value */
        radius >>= 1;
        sin_rad = q_sqrt((I2Q(1) - cos_rad) >> 1);
        cos_rad = q_sqrt((I2Q(1) + cos_rad) >> 1);
    }

    if (region == 0b10 || region == 0b01)
        *cos_t = ~*cos_t + 1;
    if (region & 0b10)
        *sin_t = ~*sin_t + 1;
}

#define W 512
#define H 512
static uint8_t img[W * H * 3];

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

/*
 * Using signed distnace field (SDF) of capsule shape to perform anti-aliasing
 * with single sample per pixel.
 */
qfixed_t capsuleSDF(qfixed_t px,
                    qfixed_t py,
                    qfixed_t ax,
                    qfixed_t ay,
                    qfixed_t bx,
                    qfixed_t by,
                    qfixed_t r)
{
    qfixed_t pax = q_add(px, -ax);
    qfixed_t pay = q_add(py, -ay);
    qfixed_t bax = q_add(bx, -ax);
    qfixed_t bay = q_add(by, -ay);

    qfixed_t t0 = q_add(q_mul(pax, bax), q_mul(pay, bay));
    qfixed_t t1 = q_add(q_mul(bax, bax), q_mul(bay, bay));
    qfixed_t tmp = min(q_div(t0, t1), I2Q(1));
    qfixed_t h = max(tmp, 0);

    qfixed_t dx = q_add(pax, -q_mul(bax, h));
    qfixed_t dy = q_add(pay, -q_mul(bay, h));

    tmp = q_add(q_mul(dx, dx), q_mul(dy, dy));
    qfixed_t res = q_add(q_sqrt(tmp), -r);
    return res;
}

#define PUT(v)            \
    p[v] = (uint8_t) Q2I( \
        q_add(p[v] * q_add((1 << Q), -alpha), q_mul(g, alpha) * 255))

/* Render shapes into the buffer individually with alpha blending. */
void alphablend(int x,
                int y,
                qfixed_t alpha,
                qfixed_t r,
                qfixed_t g,
                qfixed_t b)
{
    uint8_t *p = img + (y * W + x) * 3;
    PUT(0);
    PUT(1);
    PUT(2);
}
#undef PUT

/* Use AABB of capsule to reduce the number of samples. */
void lineSDFAABB(qfixed_t ax, qfixed_t ay, qfixed_t bx, qfixed_t by, qfixed_t r)
{
    int x0 = Q2I(q_floor(q_add(min(ax, bx), -r)));
    int x1 = Q2I(q_ceil(q_add(max(ax, bx), r)));
    int y0 = Q2I(q_floor(q_add(min(ay, by), -r)));
    int y1 = Q2I(q_ceil(q_add(max(ay, by), r)));
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++)
            alphablend(x, y,
                       max(min((1 << (Q - 1)) - capsuleSDF(I2Q(x), I2Q(y), ax,
                                                           ay, bx, by, r),
                               I2Q(1)),
                           0),
                       0, 0, 0);
    }
}

int main()
{
    memset(img, 255, sizeof(img));

    /* center of the line drawing, (1 << (Q - 1)) is equal to 0.5 */
    qfixed_t cx = W * (1 << (Q - 1)), cy = H * (1 << (Q - 1));
    /* cos(theta) and sin(theta) value for each line */
    qfixed_t ct, st;

    for (int j = 0; j < 5; j++) {
        qfixed_t r1 = min(W, H) * q_mul((I2Q(j) + (1 << (Q - 1))), f2Q(0.085f));
        qfixed_t r2 = min(W, H) * q_mul((I2Q(j) + (3 << (Q - 1))), f2Q(0.085f));
        qfixed_t t = j * q_div(Q_PI, I2Q(64));
        qfixed_t r = (j + 1) * (1 << (Q - 1));
        for (int i = 1; i <= 64; i++) {
            t = q_add(t, q_mul(I2Q(2), q_div(Q_PI, I2Q(64))));
            q_sincos(t, &st, &ct);
            lineSDFAABB(q_add(cx, q_mul(r1, ct)), q_add(cy, -q_mul(r1, st)),
                        q_add(cx, q_mul(r2, ct)), q_add(cy, -q_mul(r2, st)), r);
        }
    }
    svpng(fopen("line.png", "wb"), W, H, img, false);
}
