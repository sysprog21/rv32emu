/*
 * ASCII rendered Mandelbrot Set fractal using 16 bits of fixed point integer
 * arithmetic with a selectable fractional precision in bits.
 *
 * The fixed point implementation uses "8.8" encoding in 16 bit "short"
 * integers, where for example decimal 3.5 is hex 0x380 and decimal 2.25 is
 * hex 0x240 (0x40 being 1/4 of 0x100).
 *
 * Originally written by John Lonergan: https://github.com/Johnlon/mandelbrot
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* convert decimal value to a fixed point value in the given precision */
#if 0
short toPrec(double f, int bitsPrecision)
{
    short whole = ((short) floor(f) << (bitsPrecision));
    short part = (f - floor(f)) * (pow(2, bitsPrecision));
    short ret = whole + part;
    printf("%x\n", ret);
    return ret;
}
#endif

static const int width = 64;  /* basic width of a zx81 */
static const int height = 32; /* basic width of a zx81 */
static const int zoom = 1;    /* leave at 1 for 32x22 */

int main(int argc, char *argv[])
{
    const short bitsPrecision = 6;

    const short X1 = 0xE0;     /* toPrec(3.5, bitsPrecision) / zoom */
    const short X2 = 0x90;     /* toPrec(2.25, bitsPrecision) */
    const short Y1 = 0xC0;     /* toPrec(3, bitsPrecision) / zoom : horiz pos */
    const short Y2 = 0x60;     /* toPrec(1.5, bitsPrecision) : vert pos */
    const short LIMIT = 0x100; /* toPrec(4, bitsPrecision) */

    /* fractal */
    char charset[] = ".:-=X$#@.";
    short max_iter = sizeof(charset) - 1;

    for (short py = 0; py < height * zoom; py++) {
        for (short px = 0; px < width * zoom; px++) {
            short x0 = ((px * X1) / width) - X2;
            short y0 = ((py * Y1) / height) - Y2;
            short x = 0, y = 0;

            short i = 0;
            while (i < max_iter) {
                short xSqr = (x * x) >> bitsPrecision;
                short ySqr = (y * y) >> bitsPrecision;

                /* Breakout if sum is > the limit OR breakout also if sum is
                 * negative which indicates overflow of the addition has
                 * occurred The overflow check is only needed for precisions of
                 * over 6 bits because for 7 and above the sums come out
                 * overflowed and negative therefore we always run to max_iter
                 * and we see nothing. By including the overflow break out we
                 * can see the fractal again though with noise.
                 */
                if ((xSqr + ySqr) >= LIMIT || (xSqr + ySqr) < 0)
                    break;

                short xt = xSqr - ySqr + x0;
                y = (((x * y) >> bitsPrecision) * 2) + y0;
                x = xt;

                i++;
            }
            printf("%c", charset[--i]);
        }

        printf("\n");
    }
}
