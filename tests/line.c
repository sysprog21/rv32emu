/*
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

#include <math.h>  // ceilf(), floorf(), fminf(), fmaxf(), sinf(), cosf(), sqrtf()
#include <string.h>  // memset()

#define PI 3.14159265359f
#define W 512
#define H 512
static uint8_t img[W * H * 3];

/* Using signed distnace field (SDF) of capsule shape to perform anti-aliasing
 * with single sample per pixel.
 */
float capsuleSDF(float px,
                 float py,
                 float ax,
                 float ay,
                 float bx,
                 float by,
                 float r)
{
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = fmaxf(
        fminf((pax * bax + pay * bay) / (bax * bax + bay * bay), 1.0f), 0.0f);
    float dx = pax - bax * h, dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy) - r;
}

/* Render shapes into the buffer individually with alpha blending. */
void alphablend(int x, int y, float alpha, float r, float g, float b)
{
    uint8_t *p = img + (y * W + x) * 3;
    p[0] = (uint8_t) (p[0] * (1 - alpha) + r * alpha * 255);
    p[1] = (uint8_t) (p[1] * (1 - alpha) + g * alpha * 255);
    p[2] = (uint8_t) (p[2] * (1 - alpha) + b * alpha * 255);
}

/* Use AABB of capsule to reduce the number of samples. */
void lineSDFAABB(float ax, float ay, float bx, float by, float r)
{
    int x0 = (int) floorf(fminf(ax, bx) - r);
    int x1 = (int) ceilf(fmaxf(ax, bx) + r);
    int y0 = (int) floorf(fminf(ay, by) - r);
    int y1 = (int) ceilf(fmaxf(ay, by) + r);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++)
            alphablend(
                x, y,
                fmaxf(fminf(0.5f - capsuleSDF(x, y, ax, ay, bx, by, r), 1.0f),
                      0.0f),
                0.0f, 0.0f, 0.0f);
    }
}

int main()
{
    memset(img, 255, sizeof(img));
    float cx = W * 0.5f, cy = H * 0.5f;
    for (int j = 0; j < 5; j++) {
        float r1 = fminf(W, H) * (j + 0.5f) * 0.085f;
        float r2 = fminf(W, H) * (j + 1.5f) * 0.085f;
        float t = j * PI / 64.0f, r = (j + 1) * 0.5f;
        for (int i = 1; i <= 64; i++, t += 2.0f * PI / 64.0f) {
            float ct = cosf(t), st = sinf(t);
            lineSDFAABB(cx + r1 * ct, cy - r1 * st, cx + r2 * ct, cy - r2 * st,
                        r);
        }
    }
    svpng(fopen("line.png", "wb"), W, H, img, false);
}
