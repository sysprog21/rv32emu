#include "LU.h"
#include <math.h>

double LU_num_flops(int N)
{
    /* rougly 2/3*N^3 */
    double Nd = (double) N;

    return (2.0 * Nd * Nd * Nd / 3.0);
}

int LU_factor(int M, int N, double **A, int *pivot)
{
    int minMN = M < N ? M : N;

    for (int j = 0; j < minMN; j++) {
        /* find pivot in column j and  test for singularity. */
        int jp = j;

        double t = fabs(A[j][j]);
        for (int i = j + 1; i < M; i++) {
            double ab = fabs(A[i][j]);
            if (ab > t) {
                jp = i;
                t = ab;
            }
        }

        pivot[j] = jp;

        /* jp now has the index of maximum element  */
        /* of column j, below the diagonal          */

        if (A[jp][j] == 0)
            return 1; /* factorization failed because of zero pivot */

        if (jp != j) {
            /* swap rows j and jp */
            double *tA = A[j];
            A[j] = A[jp];
            A[jp] = tA;
        }

        if (j < M - 1) { /* compute elements j+1:M of jth column  */
            /* note A(j,j), was A(jp,p) previously which was */
            /* guarranteed not to be zero (Label #1)         */

            double recp = 1.0 / A[j][j];
            int k;
            for (k = j + 1; k < M; k++)
                A[k][j] *= recp;
        }

        if (j < minMN - 1) {
            /* rank-1 update to trailing submatrix:   E = E - x*y; */
            /* E is the region A(j+1:M, j+1:N) */
            /* x is the column vector A(j+1:M,j) */
            /* y is row vector A(j,j+1:N)        */
            for (int ii = j + 1; ii < M; ii++) {
                double *Aii = A[ii];
                double *Aj = A[j];
                double AiiJ = Aii[j];
                for (int jj = j + 1; jj < N; jj++)
                    Aii[jj] -= AiiJ * Aj[jj];
            }
        }
    }

    return 0;
}
