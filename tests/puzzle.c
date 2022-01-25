/*
 * provide timing and utility functions.
 *
 * identifiers starting with bm_ are reserved
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* return the last result of TIME_CYCLES.  */
#define GET_CYCLES 1 /* bm_get_cycles () */

/* output the result of type double, higher is better.  Never returns.  */
#define RESULT(val) bm_set_result(val)

/* output an error and kill the program.  Never returns.  */
#define ERROR(msg) bm_error(msg)

/* make sure this value is calculated and not optimized away.  */
#define USE(value) bm_eat((int) (value))

/* return a zero without the compiler knowing it.  */
#define ZERO bm_return_zero()

/* tag the next result with the given string.  */
#define TAG(s) bm_set_tag(s)

#define TIME_CYCLES

static int bm_return_zero(void)
{
    return 0;
}
static void bm_eat(int val) {}
static void bm_set_tag(const char *tag)
{
    printf("TAG %s\n", tag);
}
static void bm_error(char *msg)
{
    printf("ERROR %s\n", msg);
}
static void bm_set_result(float val)
{
    ;
}
static void bm_get_cycles()
{
    ;
}

/*
 * declares the main function for you, use it like this:
 * BEGIN_MAIN
 *   code;
 *   more code;
 * END_MAIN
 */
#define BEGIN_MAIN                  \
    int main(int argc, char **argv) \
    {
#define END_MAIN \
    return 0;    \
    }

/*
 * can be used instead of coding your own main(), if you
 * only have a single, simple test
 */
#define SINGLE_BENCHMARK(code) \
    BEGIN_MAIN                 \
    code;                      \
    END_MAIN

#define size 511
#define classmax 3
#define typemax 12
#define d 8

/*
 * The standard puzzle benchmark
 */

char piececount[classmax + 1];
char class[typemax + 1];
short piecemax[typemax + 1];
char puzzle[size + 1];
char p[typemax + 1][size + 1];

short m, n;
int kount;

int fit(int i, int j)
{
    short k;

    for (k = 0; k <= piecemax[i]; k++)
        if (p[i][k])
            if (puzzle[j + k])
                return false;
    return true;
}

int place(int i, int j)
{
    short k;
    for (k = 0; k <= piecemax[i]; k++)
        if (p[i][k])
            puzzle[j + k] = true;
    piececount[class[i]] = piececount[class[i]] - 1;
    for (k = j; k <= size; k++)
        if (!puzzle[k])
            return k;

    return false;
}

void remove_(int i, int j)
{
    for (short k = 0; k <= piecemax[i]; k++)
        if (p[i][k])
            puzzle[j + k] = false;
    piececount[class[i]] = piececount[class[i]] + 1;
}

int trial(int j)
{
    int i, k;
    char trialval;

    for (i = 0; i <= typemax; i++) {
        if (piececount[class[i]])
            if (fit(i, j)) {
                k = place(i, j);
                if (trial(k) || (k == 0)) {
                    /*printf("piece %d at %d\n", i + 1, k + 1);*/;
                    kount = kount + 1;
                    return (true);
                } else
                    remove_(i, j);
            }
    }
    kount = kount + 1;
    return (false);
}

void puzzle_(void)
{
    int i, j, k;

    for (m = 0; m <= size; m++)
        puzzle[m] = 1;
    for (i = 1; i <= 5; i++)
        for (j = 1; j <= 5; j++)
            for (k = 1; k <= 5; k++)
                puzzle[i + 8 * (j + 8 * k)] = 0;
    for (i = 0; i <= 12; i++)
        for (m = 0; m <= 511; m++)
            p[i][m] = 0;
    for (i = 0; i <= 3; i++)
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 0; k++)
                p[0][i + 8 * (j + 8 * k)] = 1;
    class[0] = 0;
    piecemax[0] = 3 + 8 * 1 + 8 * 8 * 0;
    for (i = 0; i <= 1; i++)
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 3; k++)
                p[1][i + 8 * (j + 8 * k)] = 1;
    class[1] = 0;
    piecemax[1] = 1 + 8 * 0 + 8 * 8 * 3;
    for (i = 0; i <= 0; i++)
        for (j = 0; j <= 3; j++)
            for (k = 0; k <= 1; k++)
                p[2][i + 8 * (j + 8 * k)] = 1;
    class[2] = 0;
    piecemax[2] = 0 + 8 * 3 + 8 * 8 * 1;
    for (i = 0; i <= 1; i++)
        for (j = 0; j <= 3; j++)
            for (k = 0; k <= 0; k++)
                p[3][i + 8 * (j + 8 * k)] = 1;
    class[3] = 0;
    piecemax[3] = 1 + 8 * 3 + 8 * 8 * 0;
    for (i = 0; i <= 3; i++)
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 1; k++)
                p[4][i + 8 * (j + 8 * k)] = 1;
    class[4] = 0;
    piecemax[4] = 3 + 8 * 0 + 8 * 8 * 1;
    for (i = 0; i <= 0; i++)
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 3; k++)
                p[5][i + 8 * (j + 8 * k)] = 1;
    class[5] = 0;
    piecemax[5] = 0 + 8 * 1 + 8 * 8 * 3;
    for (i = 0; i <= 2; i++)
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 0; k++)
                p[6][i + 8 * (j + 8 * k)] = 1;
    class[6] = 1;
    piecemax[6] = 2 + 8 * 0 + 8 * 8 * 0;
    for (i = 0; i <= 0; i++)
        for (j = 0; j <= 2; j++)
            for (k = 0; k <= 0; k++)
                p[7][i + 8 * (j + 8 * k)] = 1;
    class[7] = 1;
    piecemax[7] = 0 + 8 * 2 + 8 * 8 * 0;
    for (i = 0; i <= 0; i++)
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 2; k++)
                p[8][i + 8 * (j + 8 * k)] = 1;
    class[8] = 1;
    piecemax[8] = 0 + 8 * 0 + 8 * 8 * 2;
    for (i = 0; i <= 1; i++)
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 0; k++)
                p[9][i + 8 * (j + 8 * k)] = 1;
    class[9] = 2;
    piecemax[9] = 1 + 8 * 1 + 8 * 8 * 0;
    for (i = 0; i <= 1; i++)
        for (j = 0; j <= 0; j++)
            for (k = 0; k <= 1; k++)
                p[10][i + 8 * (j + 8 * k)] = 1;
    class[10] = 2;
    piecemax[10] = 1 + 8 * 0 + 8 * 8 * 1;
    for (i = 0; i <= 0; i++)
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 1; k++)
                p[11][i + 8 * (j + 8 * k)] = 1;
    class[11] = 2;
    piecemax[11] = 0 + 8 * 1 + 8 * 8 * 1;
    for (i = 0; i <= 1; i++)
        for (j = 0; j <= 1; j++)
            for (k = 0; k <= 1; k++)
                p[12][i + 8 * (j + 8 * k)] = 1;
    class[12] = 3;
    piecemax[12] = 1 + 8 * 1 + 8 * 8 * 1;
    piececount[0] = 13;
    piececount[1] = 3;
    piececount[2] = 1;
    piececount[3] = 1;
    m = 1 + 8 * (1 + 8 * 1);
    kount = 0;
    if (fit(0, m))
        n = place(0, m);
    else {
        printf("error 1\n");
    }
    if (trial(n)) {
        printf("success in %d trials\n", kount);
    } else {
        printf("failure\n");
    }
}

SINGLE_BENCHMARK(puzzle_())
