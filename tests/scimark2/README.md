# SciMark2 (C version)

This is an ANSI C version of the SciMark2 benchmark, translated from the
original Java sources. The intent in making this benchmark available in C is
for mainly for performance comparisons. For more information about SciMark, see http://math.nist.gov/scimark.

Results of this benchmark can be sent to pozo@nist.gov.

The program is split up into the main driver (`scimark2.c`) and kernel routines.
A sample makefile is included;  however, one could simply write 

```shell
cc -o scimark2  -O *.c
```

and then run
```shell
./scimark2
```

This produces an output similar to
```
**                                                              **
** SciMark2 Numeric Benchmark, see http://math.nist.gov/scimark **
** for details. (Results can be submitted to pozo@nist.gov)     **
**                                                              **
Using       2.00 seconds min time per kenel.
Composite Score:           65.56
FFT             Mflops:    63.38    (N=1024)
SOR             Mflops:   124.80    (100 x 100)
MonteCarlo:     Mflops:    16.05
Sparse matmult  Mflops:    59.15    (N=1000, nz=5000)
LU              Mflops:    64.40    (M=100, N=100)
0:29.62 Elapsed, 29.620 user sec, 0.010 sys sec, 100.0% utilization.
```

The first SciMark number reported is the composite score, followed by the an
approximate Mflop rate for each kernel.

To run the "large" version of this benchmark (with data structures
that typically do not fit in cache) use
```shell
./scimark2 -large
```
