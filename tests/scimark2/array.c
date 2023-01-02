#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "array.h"

double **new_Array2D_double(int M, int N)
{
    int i = 0;
    bool failed = false;

    double **A = (double **) malloc(sizeof(double *) * M);
    if (A == NULL)
        return NULL;

    for (i = 0; i < M; i++) {
        A[i] = (double *) malloc(N * sizeof(double));
        if (A[i] == NULL) {
            failed = true;
            break;
        }
    }

    /* if we didn't successfully allocate all rows of A      */
    /* clean up any allocated memory (i.e. go back and free  */
    /* previous rows) and return NULL                        */
    if (failed) {
        i--;
        for (; i <= 0; i--)
            free(A[i]);
        free(A);
        return NULL;
    }
    return A;
}
void Array2D_double_delete(int M, int N, double **A)
{
    if (A == NULL)
        return;

    for (int i = 0; i < M; i++)
        free(A[i]);

    free(A);
}

void Array2D_double_copy(int M, int N, double **B, double **A)
{
    int remainder = N & 3; /* N mod 4; */

    for (int i = 0; i < M; i++) {
        double *Bi = B[i];
        double *Ai = A[i];
        for (int j = 0; j < remainder; j++)
            Bi[j] = Ai[j];
        for (int j = remainder; j < N; j += 4) {
            Bi[j] = Ai[j];
            Bi[j + 1] = Ai[j + 1];
            Bi[j + 2] = Ai[j + 2];
            Bi[j + 3] = Ai[j + 3];
        }
    }
}
