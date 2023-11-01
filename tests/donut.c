/*
 * Copyright (c) 2023 Andy Sloane
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
 * Source: https://github.com/a1k0n/donut-raymarch
 * Modified by Jim Huang <jserv@ccns.ncku.edu.tw>
 * - Refine the comments
 * - Colorize the renderer
 * - Support nanosleep
 */

/* An ASCII donut renderer that relies solely on shifts, additions,
 * subtractions, and does not use sine, cosine, square root, division, or
 * multiplication instructions. It operates without requiring framebuffer
 * memory throughout the entire process.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 0 for 80x24, 1 for 160x48, etc. */
enum {
    RESX_SHIFT = 0,
    RESY_SHIFT = 0,
};

/* Torus radius and camera distance.
 * These values are closely tied to other constants, so modifying them
 * significantly may lead to unexpected behavior.
 */
static const int dz = 5, r1 = 1, r2 = 2;

/* "Magic Circle Algorithm" or DDA?
 * This algorithm, which I have encountered in several sources including Hal
 * Chamberlain's "Musical Applications of Microprocessors," lacks a clear
 * theoretical justification. It effectively rotates around a point "near" the
 * origin without significant magnitude loss, provided there is sufficient
 * precision in the x and y values. Here, I'm using 14 bits.
 */
#define R(s, x, y) \
    x -= (y >> s); \
    y += (x >> s)

/* CORDIC algorithm used to calculate the magnitude of the vector |x, y| by
 * rotating the vector onto the x-axis. This operation also transforms vector
 * (x2, y2) accordingly, and updates the value of x2. This rotation is employed
 * to align the lighting vector with the normal of the torus surface relative
 * to the camera, enabling the determination of lighting intensity. It is worth
 * noting that only one of the two lighting normal coordinates needs to be
 * retained.
 */
static int length_cordic(int16_t x, int16_t y, int16_t *x2_, int16_t y2)
{
    int x2 = *x2_;
    if (x < 0) {  // start in right half-plane
        x = -x;
        x2 = -x2;
    }
    for (int i = 0; i < 8; i++) {
        int t = x;
        int t2 = x2;
        if (y < 0) {
            x -= y >> i;
            y += t >> i;
            x2 -= y2 >> i;
            y2 += t2 >> i;
        } else {
            x += y >> i;
            y -= t >> i;
            x2 += y2 >> i;
            y2 -= t2 >> i;
        }
    }
    /* Divide by 0.625 as a rough approximation to the 0.607 scaling factor
     * introduced by this algorithm
     * See https://en.wikipedia.org/wiki/CORDIC
     */
    *x2_ = (x2 >> 1) + (x2 >> 3);
    return (x >> 1) + (x >> 3) - (x >> 6);
}

int main()
{
    /* Precise rotation directions, sines, cosines, and their products */
    int16_t sB = 0, cB = 16384;
    int16_t sA = 11583, cA = 11583;
    int16_t sAsB = 0, cAsB = 0;
    int16_t sAcB = 11583, cAcB = 11583;

    for (int count = 0; count < 500; count++) {
        /* This is a multiplication, but since dz is 5, it is equivalent to
         * (sb + (sb << 2)) >> 6.
         */
        const int16_t p0x = dz * sB >> 6;
        const int16_t p0y = dz * sAcB >> 6;
        const int16_t p0z = -dz * cAcB >> 6;

        const int r1i = r1 * 256;
        const int r2i = r2 * 256;

        int niters = 0;
        int nnormals = 0;
        /* per-row increments
         * These can all be compiled into two shifts and an add.
         */
        int16_t yincC = (12 * cA) >> (8 + RESY_SHIFT);
        int16_t yincS = (12 * sA) >> (8 + RESY_SHIFT);

        /* per-column increments */
        int16_t xincX = (6 * cB) >> (8 + RESX_SHIFT);
        int16_t xincY = (6 * sAsB) >> (8 + RESX_SHIFT);
        int16_t xincZ = (6 * cAsB) >> (8 + RESX_SHIFT);

        /* top row y cosine/sine */
        int16_t ycA = -((cA >> 1) + (cA >> 4));  // -12 * yinc1 = -9*cA >> 4;
        int16_t ysA = -((sA >> 1) + (sA >> 4));  // -12 * yinc2 = -9*sA >> 4;

        for (int j = 0; j < (24 << RESY_SHIFT) - 1;
             j++, ycA += yincC, ysA += yincS) {
            /* left columnn x cosines/sines */
            int xsAsB = (sAsB >> 4) - sAsB;  // -40 * xincY
            int xcAsB = (cAsB >> 4) - cAsB;  // -40 * xincZ;

            /* ray direction */
            int16_t vxi14 = (cB >> 4) - cB - sB;  // -40 * xincX - sB;
            int16_t vyi14 = (ycA - xsAsB - sAcB);
            int16_t vzi14 = (ysA + xcAsB + cAcB);

            for (int i = 0; i < ((80 << RESX_SHIFT) - 1);
                 i++, vxi14 += xincX, vyi14 -= xincY, vzi14 += xincZ) {
                int t = 512;  // (256 * dz) - r2i - r1i;

                /* Assume t = 512, t * vxi >> 8 == vxi << 1 */
                int16_t px = p0x + (vxi14 >> 5);
                int16_t py = p0y + (vyi14 >> 5);
                int16_t pz = p0z + (vzi14 >> 5);
                int16_t lx0 = sB >> 2;
                int16_t ly0 = (sAcB - cA) >> 2;
                int16_t lz0 = (-cAcB - sA) >> 2;
                for (;;) {
                    int t0, t1, t2, d;
                    int16_t lx = lx0, ly = ly0, lz = lz0;
                    t0 = length_cordic(px, py, &lx, ly);
                    t1 = t0 - r2i;
                    t2 = length_cordic(pz, t1, &lz, lx);
                    d = t2 - r1i;
                    t += d;

                    if (t > 8 * 256) {
                        putchar(' ');
                        break;
                    } else if (d < 2) {
                        int N = lz >> 9;
                        static const char charset[] = ".,-~:;!*=#$@";
                        printf("\033[48;05;%dm%c\033[0m", N / 4 + 1,
                               charset[N > 0 ? N < 12 ? N : 11 : 0]);
                        nnormals++;
                        break;
                    }

                    {
                        /* equivalent to:
                         * px += d*vxi14 >> 14;
                         * py += d*vyi14 >> 14;
                         * pz += d*vzi14 >> 14;
                         *
                         * idea is to make a 3d vector mul hw peripheral
                         * equivalent to this algorithm
                         */

                        /* 11x1.14 fixed point 3x parallel multiply only 16 bit
                         * registers needed; starts from highest bit to lowest
                         * d is about 2..1100, so 11 bits are sufficient.
                         */
                        int16_t dx = 0, dy = 0, dz = 0;
                        int16_t a = vxi14, b = vyi14, c = vzi14;
                        while (d) {
                            if (d & 1024) {
                                dx += a;
                                dy += b;
                                dz += c;
                            }
                            d = (d & 1023) << 1;
                            a >>= 1;
                            b >>= 1;
                            c >>= 1;
                        }
                        /* We have already shifted down by 10 bits, so this
                         * extracts the last four bits.
                         */
                        px += dx >> 4;
                        py += dy >> 4;
                        pz += dz >> 4;
                    }

                    niters++;
                }
            }
            puts("");
        }
        printf("%d iterations %d lit pixels\x1b[K", niters, nnormals);
        fflush(stdout);

        /* Rotate sines, cosines, and their products to animate the torus
         * rotation about two axes.
         */
        R(5, cA, sA);
        R(5, cAsB, sAsB);
        R(5, cAcB, sAcB);
        R(6, cB, sB);
        R(6, cAcB, cAsB);
        R(6, sAcB, sAsB);

        /* FIXME: Adjust tv_nsec to align with runtime expectations. */
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 30000};
        nanosleep(&ts, &ts);

        printf("\r\x1b[%dA", (24 << RESY_SHIFT) - 1);
    }
}
