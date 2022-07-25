/*
 *  Computation of the n^th decimal digit of pi with constant memory using
 *  only 32-bit integer arithmetic.
 *
 *  This program is optimized by David McWilliams, 2021.
 *  Based on pi1.c by Fabrice Bellard, 1997.
 *  https://bellard.org/pi/
 *
 *  Uses the hypergeometric series by Bill Gosper, 1974.
 *  pi = sum( (50*n-6)/(binomial(3*n,n)*2^n), n=0..infinity )
 *  https://arxiv.org/abs/math/0110238
 *
 *  Uses the constant memory algorithm by Simon Plouffe, 1996.
 *  https://arxiv.org/abs/0912.0303
 *
 *  See also the faster n^th decimal digit program by Xavier Gourdon, 2003.
 *  http://numbers.computation.free.fr/Constants/Algorithms/pidec.cpp
 *
 *  To calculate the millionth digit of pi we need:
 *  - Modulo multiplication that can handle base 2,654,253 without overflow.
 *  - 6,505,391,993,984,718 main loops if all previous digits are calculated.
 *  - 171,247,233,500 main loops if only the millionth digit is calculated.
 */

#include <stdint.h>
#include <stdio.h>

/* Modulo multiplication with 4 tick latency on input a,
 * and 6 tick latency on input b.
 * Input range: 0 <= a < 16777216 or 2^24
 * Input range: 0 <= b <= 2796202 or INT32_MAX/256/3
 * Input range: 0 <= m <= 2796202 or INT32_MAX/256/3
 * Output range: 0 <= result < m
 */
static inline int32_t mul_mod_21(int32_t a, int32_t b, int32_t m)
{
    int32_t a1 = (a >> 0) & 0xFF;
    int32_t a2 = (a >> 8) & 0xFF;
    int32_t a3 = (a >> 16) & 0xFF;
    int32_t b2 = (b << 8) % m;
    int32_t b3 = (b2 << 8) % m;
    return (a1 * b + a2 * b2 + a3 * b3) % m;
}

/* Modulo multiplication with 3 tick latency on input a,
 * and 7 tick latency on input b.
 * Input range: INT32_MIN <= a <= INT32_MAX
 * Input range: 0 <= b <= 4194304 or 2^32/256/4
 * Input range: 0 <= m <= 4194304 or 2^32/256/4
 * Output range: INT32_MIN <= result <= INT32_MAX
 */
static inline int32_t mul_mod_22(int32_t a, int32_t b, int32_t m)
{
    int32_t a1 = (uint32_t) a >> 0 & 0xFF;
    int32_t a2 = (uint32_t) a >> 8 & 0xFF;
    int32_t a3 = (uint32_t) a >> 16 & 0xFF;
    int32_t a4 = (uint32_t) a >> 24 & 0xFF;
    int32_t b2 = (b << 8) % m;
    int32_t b3 = (b2 << 8) % m;
    int32_t b4 = (b3 << 8) % m;
    return a1 * b + a2 * b2 + a3 * b3 + a4 * b4;
}

/* Modulo multiplication with 4 tick latency on input a,
 * and 7 tick latency on input b.
 * Input range: INT32_MIN <= a <= INT32_MAX
 * Input range: 0 <= b <= 8421504 or INT32_MAX/255
 * Input range: 0 <= m <= 8421504 or INT32_MAX/255
 * Output range: -m < result < 4*m
 */
static inline int32_t mul_mod_23(int32_t a, int32_t b, int32_t m)
{
    int32_t a1 = (uint32_t) a >> 0 & 0xFF;
    int32_t a2 = (uint32_t) a >> 8 & 0xFF;
    int32_t a3 = (uint32_t) a >> 16 & 0xFF;
    int32_t a4 = (uint32_t) a >> 24 & 0xFF;
    int32_t b2 = (b << 8) % m;
    int32_t b3 = (b2 << 8) % m;
    int32_t b4 = (b3 << 8) % m;
    return a1 * b % m + a2 * b2 % m + a3 * b3 % m + a4 * b4 % m;
}

/* Return a^b */
int32_t powi(int32_t base, int32_t exp)
{
    int32_t result = 1;
    while (exp) {
        if (exp & 1)
            result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

/* Return (a^b) mod m */
int32_t pow_mod(int32_t a, int32_t b, int32_t m)
{
    int32_t result = 1;
    while (b > 0) {
        if (b & 1)
            result = mul_mod_21(result, a, m);
        a = mul_mod_21(a, a, m);
        b >>= 1;
    }
    return result;
}

/* Solve for x: (a * x) % m == 1
 * https://en.wikipedia.org/wiki/Extended_Euclidean_algorithm#Modular_integers
 *
 * N divisions is enough to calculate up to Fibonacci(N+3). Donald Knuth, 1981.
 * The Art of Computer Programming, Vol. 2: Seminumerical Algorithms, 2nd ed.
 * page 343.
 *
 * With 2 divisions per loop, 15 loops is enough to calculate up to 3500000.
 * Test case: 1346269 * 1346269 % 2178309 == 1
 */
int32_t inv_mod(int32_t a, int32_t m)
{
    a %= m;
    int32_t b = m;
    int32_t x = 1;
    int32_t y = 0;
    for (int32_t i = 0; i < 15; i++) {
        int32_t q = (a == 0) ? 0 : b / a;
        b -= a * q;
        y -= x * q;
        q = (b == 0) ? 0 : a / b;
        a -= b * q;
        x -= y * q;
    }

    return b ? (y + m) : x;
}

/*  Increment n until it is prime */
int next_prime(int32_t n)
{
    n++;

    static uint32_t square_root = 0;
    if (square_root >= n)
        square_root = 0; /* reset cached value */

    while (1) {
        while (square_root * square_root < n - 1)
            square_root++;

        int32_t factors = 0;
        for (int32_t i = 2; i <= square_root; i++) {
            if (n % i == 0) {
                factors++;
                break;
            }
        }

        if (factors <= 0) /* Found prime number */
            return n;

        n++; /* found composite number */
    }
}

/*  Remove prime factors from n and count how many were removed */
static int32_t prime_power[15], prime_power_count;
int32_t factor_count(int32_t *n)
{
    for (int32_t i = prime_power_count - 1; i >= 0; i--) {
        if (*n % prime_power[i] == 0) {
            *n /= prime_power[i];
            return i;
        }
    }
    __builtin_unreachable();
}

/*  Calculate sum = (sum + n/d) and store the decimal part in fixed-point format
 *  with 18 decimal places across two 32-bit integers.
 *
 *  This is equivalent to the floating point one-liner:
 *  sum = fmod(sum + (double)n / (double)d, 1.0);
 *
 *  Inputs must be <= 10,737,418 or INT32_MAX/200.
 */
void fixed_point_sum(int32_t n, int32_t d, int32_t *hi, int32_t *lo)
{
    /* Digits 1 to 9 */
    int32_t n1 = n * 200;
    int32_t n2 = n1 % d * 200;
    int32_t n3 = n2 % d * 200;
    int32_t n4 = n3 % d * 125;
    *hi += n1 / d * 5000000;
    *hi += n2 / d * 25000;
    *hi += n3 / d * 125;
    *hi += n4 / d;

    /* Digits 10 to 18 */
    int32_t n5 = n4 % d * 200;
    int32_t n6 = n5 % d * 200;
    int32_t n7 = n6 % d * 200;
    int32_t n8 = n7 % d * 125;
    *lo += n5 / d * 5000000;
    *lo += n6 / d * 25000;
    *lo += n7 / d * 125;
    *lo += n8 / d;

    /* Carry */
    if (*lo > 1000000000)
        *hi += 1;

    /* Discard overflow digits */
    *hi = *hi % 1000000000;
    *lo = *lo % 1000000000;
}

/*  Return 9 digits of pi */
int32_t pifactory(int32_t start_digit)
{
    int32_t sum = 0, sum_low = 0;

    /* N = (start_digit + 19) / log10(13.5)
     * log10(13.5) is approximately equal to 269/238
     */
    int32_t N = (start_digit + 19) * 238 / 269;

    /* Compute the Gosper series modulo each prime power up to 3*N */
    for (int32_t prime = 2; prime < 3 * N; prime = next_prime(prime)) {
        /* Compute the first few prime powers
         * Only 15 powers are needed if start_digit < 1,000,000
         * Only powers up to 10,000,000 are needed if start_digit <= 1,000,000
         */
        static const int32_t ROOT_10M[15] = {
            10000000, 10000000, 3162, 215, 56, 25, 14, 10, 7, 6, 5, 4, 3, 3, 3,
        };
        prime_power_count = 0;
        for (int32_t i = 0; i < 15; i++) {
            if (prime <= ROOT_10M[i]) {
                prime_power[i] = powi(prime, i);
                prime_power_count++;
            }
        }

        /* For small primes, use a prime power with exponent greater than 1 */
        int32_t exponent = -1;
        for (int32_t i = 0; i < prime_power_count; i++) {
            if (prime_power[i] < 3 * N)
                exponent++;
        }
        int32_t m = powi(prime, exponent);

        if (prime == 2) {
            /* Add the 2^N term in the denominator. */
            exponent += N - 1;
            /* We have some more powers of 2 in the 10^start_digit decimal shift
             * in the numerator. Use them to cancel out the 2^N term.
             */
            m = powi(prime, exponent - start_digit);
            /* Since start_digit grows faster than N, eventually we will
             * cancel the entire exponent and m will become 0.
             */
            if (m == 0)
                continue;
        }

        /* Multiply by 10^start_digit to move the target digit to the most
         * significant decimal place.
         */
        int32_t decimal = 10;
        if (prime == 2) /* We already used those powers of 2 */
            decimal = 5;
        int32_t decimal_shift = pow_mod(decimal, start_digit, m);

        /* Main loop */
        int32_t subtotal = 0;
        int32_t numerator = 1;
        int32_t denominator = 1;
        for (int32_t k = 1; k <= N; k++) {
            /* Terms for the numerator */
            int32_t t1 = 2 * k, t2 = 2 * k - 1;
            exponent += factor_count(&t1);
            exponent += factor_count(&t2);
            int32_t terms = mul_mod_21(t1 % m, t2 % m, m);
            numerator = mul_mod_22(numerator, terms, m);

            /* Terms for the denominator */
            int32_t t3 = 6 * k - 4, t4 = 9 * k - 3;
            exponent -= factor_count(&t3);
            exponent -= factor_count(&t4);
            terms = mul_mod_21(t3 % m, t4 % m, m);
            denominator = mul_mod_22(denominator, terms, m);

            /* Multiply all parts together */
            int32_t inverse = inv_mod(denominator, m);
            int32_t t = (50 * k - 6) % m;
            t = mul_mod_23(numerator, t, m);
            t = mul_mod_21(t, powi(prime, exponent), m);
            t = mul_mod_21(t, inverse, m);

            subtotal = (subtotal + t) % m;
        }
        subtotal = mul_mod_21(subtotal, decimal_shift, m);

        /* We have a fraction over a prime power, add it to the final sum */
        fixed_point_sum(subtotal, m, &sum, &sum_low);
    }
    return sum;
}

int32_t main()
{
    int32_t start = 0, end = 100;

    /* Print digits of pi */
    printf("3.");
    start++;

    for (int32_t i = start - 1; i < end; i += 9)
        printf("%09d", pifactory(i));
    printf("\n");

    return 0;
}
