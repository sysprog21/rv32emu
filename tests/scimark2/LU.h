#pragma once

double LU_num_flops(int N);
int LU_factor(int M, int N, double **A, int *pivot);
