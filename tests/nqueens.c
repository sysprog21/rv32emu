/* N Queens Problem (number of Solutions)
 * source: https://computerpuzzle.net/english/nqueens/
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define MAXSIZE 15

static int size_e;
static int board[MAXSIZE], *board_e, *board1, *board2;
static int MASK, TOPBIT, SIDEMASK, LASTMASK, ENDBIT;

static int64_t count8, count4, count2;

/* check unique solutions */
static void check()
{
    int *own, *you, bit, ptn;

    /* 90-degree rotation */
    if (*board2 == 1) {
        for (ptn = 2, own = board + 1; own <= board_e; own++, ptn <<= 1) {
            bit = 1;
            for (you = board_e; *you != ptn && *own >= bit; you--)
                bit <<= 1;
            if (*own > bit)
                return;
            if (*own < bit)
                break;
        }
        if (own > board_e) {
            count2++;
            return;
        }
    }

    /* 180-degree rotation */
    if (*board_e == ENDBIT) {
        for (you = board_e - 1, own = board + 1; own <= board_e; own++, you--) {
            bit = 1;
            for (ptn = TOPBIT; ptn != *you && *own >= bit; ptn >>= 1)
                bit <<= 1;
            if (*own > bit)
                return;
            if (*own < bit)
                break;
        }
        if (own > board_e) {
            count4++;
            return;
        }
    }

    /* 270-degree rotation */
    if (*board1 == TOPBIT) {
        for (ptn = TOPBIT >> 1, own = board + 1; own <= board_e;
             own++, ptn >>= 1) {
            bit = 1;
            for (you = board; *you != ptn && *own >= bit; you++)
                bit <<= 1;
            if (*own > bit)
                return;
            if (*own < bit)
                break;
        }
    }
    count8++;
}

/* First queen is inside */
static void backtrack2(int y,
                       int left,
                       int down,
                       int right,
                       int bound1,
                       int bound2)
{
    int bitmap = MASK & ~(left | down | right);
    if (y == size_e) {
        if (bitmap) {
            if (!(bitmap & LASTMASK)) {
                board[y] = bitmap;
                check();
            }
        }
    } else {
        if (y < bound1) {
            bitmap |= SIDEMASK;
            bitmap ^= SIDEMASK;
        } else if (y == bound2) {
            if (!(down & SIDEMASK))
                return;
            if ((down & SIDEMASK) != SIDEMASK)
                bitmap &= SIDEMASK;
        }
        while (bitmap) {
            int bit;
            bitmap ^= board[y] = bit = -bitmap & bitmap;
            backtrack2(y + 1, (left | bit) << 1, down | bit, (right | bit) >> 1,
                       bound1, bound2);
        }
    }
}

/* First queen is in the corner */
static void backtrack1(int y, int left, int down, int right, int bound1)
{
    int bitmap = MASK & ~(left | down | right);
    if (y == size_e) {
        if (bitmap) {
            board[y] = bitmap;
            count8++;
        }
    } else {
        if (y < bound1) {
            bitmap |= 2;
            bitmap ^= 2;
        }
        while (bitmap) {
            int bit;
            bitmap ^= board[y] = bit = -bitmap & bitmap;
            backtrack1(y + 1, (left | bit) << 1, down | bit, (right | bit) >> 1,
                       bound1);
        }
    }
}

/* Search of N-Queens                         */
static void nqueens(int size)
{
    int bit;

    /* Initialize */
    count8 = count4 = count2 = 0;
    size_e = size - 1;
    board_e = &board[size_e];
    TOPBIT = 1 << size_e;
    MASK = (1 << size) - 1;

    /* 0:000000001 */
    /* 1:011111100 */
    board[0] = 1;
    for (int bound1 = 2; bound1 < size_e; bound1++) {
        board[1] = bit = 1 << bound1;
        backtrack1(2, (2 | bit) << 1, 1 | bit, bit >> 1, bound1);
    }

    /* 0:000001110 */
    SIDEMASK = LASTMASK = TOPBIT | 1;
    ENDBIT = TOPBIT >> 1;
    for (int bound1 = 1, bound2 = size - 2; bound1 < bound2;
         bound1++, bound2--) {
        board1 = &board[bound1];
        board2 = &board[bound2];
        board[0] = bit = 1 << bound1;
        backtrack2(1, bit << 1, bit, bit >> 1, bound1, bound2);
        LASTMASK |= LASTMASK >> 1 | LASTMASK << 1;
        ENDBIT >>= 1;
    }
}

int main(void)
{
    printf("<---  N-Queens Solutions  --->\n");
    printf(" N:        Total       Unique\n");
    for (int size = 2; size <= MAXSIZE; size++) {
        nqueens(size);
        printf("%2d:%13" PRId64 "%13" PRId64 "\n", size,
               count8 * 8 + count4 * 4 + count2 * 2 /* total solutions */,
               count8 + count4 + count2 /* unique solutions */);
    }

    return 0;
}
