/*
 * maj2random
 *
 * maj2random is a simplified floating point hash function derived from SHA-2,
 * retaining its high quality entropy compression function modified to permute
 * entropy from a vec2 (designed for UV coordinates) returning float values
 * between 0.0 and 1.0. since maj2_random is a hash function it will return
 * coherent noise. vector argument can be truncated prior to increase grain.
 *
 * maj2random is named after the maj mixing function called within the SHA-2
 * round function and the 2 is becuase maj2random uses 2 rounds instead of 64.
 * This seems to be sufficient to create visually diffuse noise with only 3%
 * of the overhead of the full SHA-256 hashing function. maj2random also pays
 * homage to arc4random due to the similarity of its name form.
 *
 * Source: https://github.com/michaeljclark/maj2random
 */

#include <math.h>
typedef unsigned uint;
typedef struct {
    float a[2];
} vec2;
typedef struct {
    uint a[2];
} uvec2;

/* first 8 rounds of the SHA-256 k constant */
static uint sha256_k[8] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
};

static uint ror(uint x, int d)
{
    return (x >> d) | (x << (32 - d));
}
static uint sigma0(uint h1)
{
    return ror(h1, 2) ^ ror(h1, 13) ^ ror(h1, 22);
}
static uint sigma1(uint h4)
{
    return ror(h4, 6) ^ ror(h4, 11) ^ ror(h4, 25);
}
static uint ch(uint x, uint y, uint z)
{
    return z ^ (x & (y ^ z));
}
static uint maj(uint x, uint y, uint z)
{
    return (x & y) ^ ((x ^ y) & z);
}
static uint gamma0(uint a)
{
    return ror(a, 7) ^ ror(a, 18) ^ (a >> 3);
}
static uint gamma1(uint b)
{
    return ror(b, 17) ^ ror(b, 19) ^ (b >> 10);
}

static vec2 unorm(uvec2 n)
{
    vec2 r = {(float) (n.a[0] & ((1u << 23) - 1u)) / (float) ((1u << 23) - 1u),
              (float) (n.a[1] & ((1u << 23) - 1u)) / (float) ((1u << 23) - 1u)};
    return r;
}
static uvec2 sign(vec2 v)
{
    uvec2 r = {v.a[0] < 0.0f, v.a[1] < 0.0f};
    return r;
}

static uvec2 maj_extract(vec2 uv)
{
    /*
     * extract 48-bits of entropy from mantissas to create truncated
     * two word initialization vector 'W' composed using the 48-bits
     * of 'uv' entropy rotated and copied to keep the field equalized.
     * the exponent is ignored because the inputs are expected to be
     * normalized 'uv' values such as texture coordinates. it would be
     * beneficial to include the exponent entropy but we can't depend
     * on frexp or ilogb and log2 would be inaccurate.
     */
    uvec2 s = sign(uv);
    uint x = (uint) (fabsf(uv.a[0]) * (float) (1u << 23)) | (s.a[0] << 23);
    uint y = (uint) (fabsf(uv.a[1]) * (float) (1u << 23)) | (s.a[1] << 23);

    uvec2 r = {(x) | (y << 24), (y >> 8) | (x << 16)};
    return r;
}

static vec2 maj_random(vec2 uv, uint NROUNDS)
{
    uint H[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    uint W[2];

    uvec2 st = maj_extract(uv);

    W[0] = st.a[0];
    W[1] = st.a[1];

    for (uint i = 0; i < NROUNDS; i++) {
        W[i & 1] = gamma1(W[(i - 2) & 1]) + W[(i - 7) & 1] +
                   gamma0(W[(i - 15) & 1]) + W[(i - 16) & 1];
    }

    /* we use N rounds instead of 64 and alternate 2 words of iv in W */
    for (uint i = 0; i < NROUNDS; i++) {
        uint T0 =
            W[i & 1] + H[7] + sigma1(H[4]) + ch(H[4], H[5], H[6]) + sha256_k[i];
        uint T1 = maj(H[0], H[1], H[2]) + sigma0(H[0]);
        H[7] = H[6];
        H[6] = H[5];
        H[5] = H[4];
        H[4] = H[3] + T0;
        H[3] = H[2];
        H[2] = H[1];
        H[1] = H[0];
        H[0] = T0 + T1;
    }
    uvec2 u = {H[0] ^ H[1] ^ H[2] ^ H[3], H[4] ^ H[5] ^ H[6] ^ H[7]};
    return unorm(u);
}

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long ullong;

void sncatprintf(char *buf, size_t buflen, const char *fmt, int64_t n)
{
    size_t len = strlen(buf);
    snprintf(buf + len, buflen - len, fmt, n);
}

char *format_comma(int64_t n)
{
    static char buf[65];

    buf[0] = 0;
    int n2 = 0;
    int scale = 1;
    if (n < 0) {
        buf[0] = '-';
        buf[1] = 0;
        n = -n;
    }
    while (n >= 1000) {
        n2 = n2 + scale * (n % 1000);
        n /= 1000;
        scale *= 1000;
    }
    sncatprintf(buf, sizeof(buf), "%d", n);
    while (scale != 1) {
        scale /= 1000;
        n = n2 / scale;
        n2 = n2 % scale;
        sncatprintf(buf, sizeof(buf), ",%03d", n);
    }
    return buf;
}

char *format_rate(double rate)
{
    static char buf[65];
    char unit = ' ';
    double divisor = 1;
    if (rate > 1e9) {
        divisor = 1e9;
        unit = 'G';
    } else if (rate > 1e6) {
        divisor = 1e6;
        unit = 'M';
    } else if (rate > 1e3) {
        divisor = 1e3;
        unit = 'K';
    }
    snprintf(buf, sizeof(buf), "%.1f %c", rate / divisor, unit);
    return buf;
}

vec2 f1(ullong i, ullong j)
{
    return (vec2) {0.0f, (float) i / (float) j};
}
vec2 f2(ullong i, ullong j)
{
    return (vec2) {(float) i / (float) j, 0.0f};
}
vec2 f3(ullong i, ullong j)
{
    return (vec2) {(float) i / (float) j, (float) i / (float) j};
}

void test_maj(const char *name,
              int NROUNDS,
              ullong count,
              ullong range,
              vec2 (*f)(ullong, ullong))
{
    vec2 sum = {0.f, 0.f};
    vec2 var = {0.f, 0.f};

    fflush(stdout);
    for (ullong i = 0; i < count; i++) {
        vec2 p = f(i, range);
        vec2 q = maj_random(p, NROUNDS);
        sum.a[0] += q.a[0];
        sum.a[1] += q.a[1];
        var.a[0] += (q.a[0] - .5f) * (q.a[0] - .5f);
        var.a[1] += (q.a[1] - .5f) * (q.a[1] - .5f);
    }

    printf("%-32s%12s%12.5f%12.5f%12.5f%12.5f%12.5f%12.5f\n", name,
           format_comma(count), sum.a[0] / count, sum.a[1] / count,
           var.a[0] / count, var.a[1] / count, sqrt(var.a[0] / count),
           sqrt(var.a[1] / count));
}

void test_header(const char *name)
{
    printf("%-32s%12s%12s%12s%12s%12s%12s%12s\n", name, "count", "mean(x)",
           "mean(y)", "variance(x)", "variance(y)", "std-dev(x)", "std-dev(y)");
}

void run_all_tests(int NROUNDS, ullong i, ullong j)
{
    char name[32];
    snprintf(name, sizeof(name), "maj_random (NROUNDS=%d)", NROUNDS);
    printf("\n");
    test_header(name);
    test_maj("(             0, (0 - 1K)/8K )", NROUNDS, i, j, f1);
    test_maj("(             0, (0 - 8K)/8K )", NROUNDS, j, j, f1);
    test_maj("( (0 - 1K)/8K ),           0 )", NROUNDS, i, j, f2);
    test_maj("( (0 - 8K)/8K ),           0 )", NROUNDS, j, j, f2);
    test_maj("( (0 - 1K)/8K ), (0 - 1K)/8K )", NROUNDS, i, j, f3);
    test_maj("( (0 - 8K)/8K ), (0 - 8K)/8K )", NROUNDS, j, j, f3);
    printf("\n");
}

int main(int argc, char **argv)
{
    ullong i = 1000;
    ullong j = 8000;
    run_all_tests(2, i, j);
    run_all_tests(4, i, j);
    run_all_tests(6, i, j);
    run_all_tests(8, i, j);
    return 0;
}
