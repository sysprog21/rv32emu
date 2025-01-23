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
 * - Adapt ANSI graphical enhancements from Bruno Levy
 */

/* An ASCII donut renderer that relies solely on shifts, additions,
 * subtractions, and does not use sine, cosine, square root, division, or
 * multiplication instructions. It operates without requiring framebuffer
 * memory throughout the entire process.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if !defined(__riscv)
#include <unistd.h>
#endif

/* Define 1 for a more accurate result (but it costs a bit) */
#define PRECISE 0

#define USE_MULTIPLIER 1

static const char *colormap[34] = {
    "0",       "8;5;232", "8;5;233", "8;5;234", "8;5;235", "8;5;236", "8;5;237",
    "8;5;238", "8;5;239", "8;5;240", "8;5;241", "8;5;242", "8;5;243", "8;5;244",
    "8;5;245", "8;5;246", "8;5;247", "8;5;248", "8;5;249", "8;5;250", "8;5;251",
    "8;5;252", "8;5;253", "8;5;254", "8;5;255", "7",       "8;5;16",  "8;5;17",
    "8;5;18",  "8;5;19",  "8;5;20",  "8;5;21",  "8;5;22",  "8;5;23",
};

/* Previous background/foreground colors */
static int prev_color1 = 0, prev_color2 = 0;

static inline void setcolors(int fg /* foreground */, int bg /* background */)
{
    printf("\033[4%s;3%sm", colormap[bg], colormap[fg]);
}

static inline void setpixel(int x, int y, int color)
{
    /* Stash the "upper" scanline so we can combine two rows of output. */
    static char scanline[80];
    int c1, c2;

    /* On even row (y & 1 == 0), just remember the color; no output yet. */
    if (!(y & 1)) {
        scanline[x] = color;
        return;
    }

    /* On the odd row, pull the stored color from the previous row. */
    c1 = scanline[x]; /* background */
    c2 = color;       /* foreground */

    /* Same background/foreground: print a space with only background color */
    if (c1 == c2) {
        if (prev_color1 != c1) {
            printf("\033[4%sm ", colormap[c1]);
            prev_color1 = c1;
        } else { /* Already set, just print a space */
            putchar(' ');
        }
        return;
    }

    /* Different colors: print a block with new bg/fg if either changed */
    if (prev_color1 != c1 || prev_color2 != c2) {
        printf("\033[4%s;3%sm", colormap[c1], colormap[c2]);
        prev_color1 = c1;
        prev_color2 = c2;
    }
    printf("\u2583");
}

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

#if PRECISE
#define N_CORDIC 10
#else
#define N_CORDIC 6
#endif

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

    /* Move x into the right half-plane */
    if (x < 0) {
        x = -x;
        x2 = -x2;
    }

    /* CORDIC iterations */
    for (int i = 0; i < N_CORDIC; i++) {
        int t = x, t2 = x2;
        int sign = (y < 0) ? -1 : 1;

        x += sign * (y >> i);
        y -= sign * (t >> i);
        x2 += sign * (y2 >> i);
        y2 -= sign * (t2 >> i);
    }

    /* Divide by ~0.625 (5/8) to approximate the 0.607 scaling factor
     * introduced by the CORDIC algorithm. See:
     * https://en.wikipedia.org/wiki/CORDIC
     */
    *x2_ = (x2 >> 1) + (x2 >> 3);
#if PRECISE
    *x2_ -= x2 >> 6; /* get closer to 0.607 */
#endif

    return (x >> 1) + (x >> 3) - (x >> 6);
}

int main()
{
    printf(
        "\033[48;5;16m" /* set background color black */
        "\033[38;5;15m" /* set foreground color white */
        "\033[H"        /* move cursor home */
        "\033[?25l"     /* hide cursor */
        "\033[2J");     /* clear screen */

    int frame = 1;

    /* Precise rotation directions, sines, cosines, and their products */
    int16_t sB = 0, cB = 16384;
    int16_t sA = 11583, cA = 11583;
    int16_t sAsB = 0, cAsB = 0;
    int16_t sAcB = 11583, cAcB = 11583;

    for (int count = 0; count < 500; count++) {
        /* Starting position (p0).
         * This is a multiplication, but since dz is 5, it is equivalent to
         * (sb + (sb << 2)) >> 6.
         */
        const int16_t p0x = dz * sB >> 6;
        const int16_t p0y = dz * sAcB >> 6;
        const int16_t p0z = -dz * cAcB >> 6;

        const int r1i = r1 * 256, r2i = r2 * 256;

        int n_iters = 0;

        /* per-row increments
         * These can all be compiled into two shifts and an add.
         */
        int16_t yincC = (cA >> 6) + (cA >> 5); /* 12*cA >> 8 */
        int16_t yincS = (sA >> 6) + (sA >> 5); /* 12*sA >> 8 */

        /* per-column increments */
        int16_t xincX = (cB >> 7) + (cB >> 6);     /* 6*cB >> 8 */
        int16_t xincY = (sAsB >> 7) + (sAsB >> 6); /* 6*sAsB >> 8 */
        int16_t xincZ = (cAsB >> 7) + (cAsB >> 6); /* 6*cAsB >> 8 */

        /* top row y cosine/sine */
        int16_t ycA = -((cA >> 1) + (cA >> 4)); /* -12 * yinc1 = -9*cA >> 4 */
        int16_t ysA = -((sA >> 1) + (sA >> 4)); /* -12 * yinc2 = -9*sA >> 4 */

        int xsAsB = (sAsB >> 4) - sAsB; /* -40*xincY */
        int xcAsB = (cAsB >> 4) - cAsB; /* -40*xincZ */

        /* Render rows */
        for (int j = 0; j < 46; j++, ycA += yincC >> 1, ysA += yincS >> 1) {
            /* ray direction */
            int16_t vxi14 = (cB >> 4) - cB - sB;  // -40*xincX - sB;
            int16_t vyi14 = ycA - xsAsB - sAcB;
            int16_t vzi14 = ysA + xcAsB + cAcB;

            /* Render columns */
            for (int i = 0; i < 79;
                 i++, vxi14 += xincX, vyi14 -= xincY, vzi14 += xincZ) {
                int t = 512; /* Depth accumulation: (256 * dz) - r2i - r1i */

                /* Assume t = 512, t * vxi >> 8 == vxi << 1 */
                int16_t px = p0x + (vxi14 >> 5);
                int16_t py = p0y + (vyi14 >> 5);
                int16_t pz = p0z + (vzi14 >> 5);
                int16_t lx0 = sB >> 2;
                int16_t ly0 = (sAcB - cA) >> 2;
                int16_t lz0 = (-cAcB - sA) >> 2;
                for (;;) {
                    /* Distance from torus surface */
                    int t0, t1, t2, d;
                    int16_t lx = lx0, ly = ly0, lz = lz0;
                    t0 = length_cordic(px, py, &lx, ly);
                    t1 = t0 - r2i;
                    t2 = length_cordic(pz, t1, &lz, lx);
                    d = t2 - r1i;
                    t += d;

                    if (t > (8 * 256)) {
                        int N = (((j - frame) >> 3) ^ (((i + frame) >> 3))) & 1;
                        setpixel(i, j, (N << 2) + 26);
                        break;
                    }
                    if (d < 2) {
                        int N = lz >> 8;
                        N = N > 0 ? N < 26 ? N : 25 : 0;
                        setpixel(i, j, N);
                        break;
                    }

#ifdef USE_MULTIPLIER
                    px += d * vxi14 >> 14;
                    py += d * vyi14 >> 14;
                    pz += d * vzi14 >> 14;
#else
                    {
                        /* Using a 3D fixed-point partial multiply approach, the
                         * idea is to make a 3D vector multiplication hardware
                         * peripheral equivalent to this algorithm.
                         */

                        /* 11x1.14 fixed point 3x parallel multiply only 16 bit
                         * registers needed; starts from highest bit to lowest
                         * d is about 2..1100, so 11 bits are sufficient.
                         */
                        int16_t dx = 0, dy = 0, dz = 0;
                        int16_t a = vxi14, b = vyi14, c = vzi14;
                        while (d) {
                            if (d & 1024) {
                                dx += a, dy += b, dz += c;
                            }
                            d = (d & 1023) << 1;
                            a >>= 1, b >>= 1, c >>= 1;
                        }
                        /* We have already shifted down by 10 bits, so this
                         * extracts the last four bits.
                         */
                        px += dx >> 4;
                        py += dy >> 4;
                        pz += dz >> 4;
                    }
#endif

                    n_iters++;
                }
            }
            if (j & 1)
                printf("\r\n");
        }

        setcolors(25, 33);
        printf("%6d iterations", n_iters);
        setcolors(25, 0);
        printf("\x1b[K");

#if !defined(__riscv)
        fflush(stdout);
#endif

        /* Rotate sines, cosines, and their products to animate the torus
         * rotation about two axes.
         */
        R(5, cA, sA);
        R(5, cAsB, sAsB);
        R(5, cAcB, sAcB);
        R(6, cB, sB);
        R(6, cAcB, cAsB);
        R(6, sAcB, sAsB);

#if !defined(__riscv)
        usleep(15000);
#endif

        printf("\r\x1b[23A");
        ++frame;
        prev_color1 = prev_color2 = -1;
    }

    /* Show cursor again */
    printf("\033[?25h");
    return 0;
}
