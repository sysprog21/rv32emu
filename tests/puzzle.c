/*  puzzle benchmark */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SIZE 511
#define CLASS_MAX 3
#define TYPE_MAX 12

static char piececount[CLASS_MAX + 1];
static uint8_t class[TYPE_MAX + 1];
static short piecemax[TYPE_MAX + 1];
static bool puzzle[SIZE + 1];
static bool p[TYPE_MAX + 1][SIZE + 1];

static short m, n;
static int kount;

int fit(int i, int j)
{
    for (short k = 0; k <= piecemax[i]; k++) {
        if (p[i][k] && puzzle[j + k])
            return false;
    }

    return true;
}

int place(int i, int j)
{
    for (short k = 0; k <= piecemax[i]; k++) {
        if (p[i][k])
            puzzle[j + k] = true;
    }
    piececount[class[i]] = piececount[class[i]] - 1;
    for (short k = j; k <= SIZE; k++) {
        if (!puzzle[k])
            return k;
    }

    return false;
}

void remove_(int i, int j)
{
    for (short k = 0; k <= piecemax[i]; k++) {
        if (p[i][k])
            puzzle[j + k] = false;
    }
    piececount[class[i]] = piececount[class[i]] + 1;
}

int trial(int j)
{
    for (int i = 0; i <= TYPE_MAX; i++) {
        if (piececount[class[i]] && fit(i, j)) {
            int k = place(i, j);
            if (trial(k) || (k == 0)) {
                kount++;
                return true;
            }
            remove_(i, j);
        }
    }

    kount++;
    return false;
}

void run_puzzle(void)
{
    int i, j, k;

    for (m = 0; m <= SIZE; m++)
        puzzle[m] = 1;

    for (i = 1; i <= 5; i++) {
        for (j = 1; j <= 5; j++)
            for (k = 1; k <= 5; k++)
                puzzle[i + 8 * (j + 8 * k)] = 0;
    }
    for (i = 0; i <= 12; i++) {
        for (m = 0; m <= 511; m++)
            p[i][m] = false;
    }
    for (i = 0; i <= 3; i++) {
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 0; k++)
                p[0][i + 8 * (j + 8 * k)] = true;
    }

    class[0] = 0;
    piecemax[0] = 3 + 8 * 1 + 8 * 8 * 0;
    for (i = 0; i <= 1; i++) {
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 3; k++)
                p[1][i + 8 * (j + 8 * k)] = true;
    }

    class[1] = 0;
    piecemax[1] = 1 + 8 * 0 + 8 * 8 * 3;
    for (i = 0; i <= 0; i++) {
        for (j = 0; j <= 3; j++)
            for (k = 0; k <= 1; k++)
                p[2][i + 8 * (j + 8 * k)] = true;
    }

    class[2] = 0;
    piecemax[2] = 0 + 8 * 3 + 8 * 8 * 1;
    for (i = 0; i <= 1; i++) {
        for (j = 0; j <= 3; j++)
            for (k = 0; k <= 0; k++)
                p[3][i + 8 * (j + 8 * k)] = true;
    }

    class[3] = 0;
    piecemax[3] = 1 + 8 * 3 + 8 * 8 * 0;
    for (i = 0; i <= 3; i++) {
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 1; k++)
                p[4][i + 8 * (j + 8 * k)] = true;
    }

    class[4] = 0;
    piecemax[4] = 3 + 8 * 0 + 8 * 8 * 1;
    for (i = 0; i <= 0; i++) {
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 3; k++)
                p[5][i + 8 * (j + 8 * k)] = true;
    }

    class[5] = 0;
    piecemax[5] = 0 + 8 * 1 + 8 * 8 * 3;
    for (i = 0; i <= 2; i++) {
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 0; k++)
                p[6][i + 8 * (j + 8 * k)] = true;
    }

    class[6] = 1;
    piecemax[6] = 2 + 8 * 0 + 8 * 8 * 0;
    for (i = 0; i <= 0; i++) {
        for (j = 0; j <= 2; j++)
            for (k = 0; k <= 0; k++)
                p[7][i + 8 * (j + 8 * k)] = true;
    }

    class[7] = 1;
    piecemax[7] = 0 + 8 * 2 + 8 * 8 * 0;
    for (i = 0; i <= 0; i++) {
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 2; k++)
                p[8][i + 8 * (j + 8 * k)] = true;
    }

    class[8] = 1;
    piecemax[8] = 0 + 8 * 0 + 8 * 8 * 2;
    for (i = 0; i <= 1; i++) {
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 0; k++)
                p[9][i + 8 * (j + 8 * k)] = true;
    }

    class[9] = 2;
    piecemax[9] = 1 + 8 * 1 + 8 * 8 * 0;
    for (i = 0; i <= 1; i++) {
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 1; k++)
                p[10][i + 8 * (j + 8 * k)] = true;
    }

    class[10] = 2;
    piecemax[10] = 1 + 8 * 0 + 8 * 8 * 1;
    for (i = 0; i <= 0; i++) {
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 1; k++)
                p[11][i + 8 * (j + 8 * k)] = true;
    }

    class[11] = 2;
    piecemax[11] = 0 + 8 * 1 + 8 * 8 * 1;
    for (i = 0; i <= 1; i++) {
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 1; k++)
                p[12][i + 8 * (j + 8 * k)] = true;
    }

    class[12] = 3;
    piecemax[12] = 1 + 8 * 1 + 8 * 8 * 1;
    piececount[0] = 13;
    piececount[1] = 3;
    piececount[2] = 1;
    piececount[3] = 1;

    m = 1 + 8 * (1 + 8 * 1);
    kount = 0;

    if (fit(0, m)) {
        n = place(0, m);
    } else {
        printf("error 1\n");
    }
    if (trial(n)) {
        printf("success in %d trials\n", kount);
    } else {
        printf("failure\n");
    }
}

int main(int argc, char **argv)
{
    run_puzzle();
    return 0;
}
