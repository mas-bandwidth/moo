// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo-math-oracle: the offline generator of tests/moo/vectors/math-oracle.bin, the accuracy oracle of layer 4
// (docs/DESIGN.md, "math library", "Accuracy oracle"). For every input in each function's sample set it computes
// CORE-MATH's correctly rounded double result and converts that to the nearest Q48.16 by moo's boundary rule, ties to
// even (mooOracleFromDouble, copied from Common/include/moo_number.h moo_numfromdouble). moo's tests read the frozen
// file and never call CORE-MATH or libm. Standalone C: it includes nothing of moo's. See README.md.
//
// usage: moo-math-oracle OUTPUT.bin
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef MOO_CORE_MATH_COMMIT
#error "build with CMakeLists.txt, which passes the CORE-MATH commit (MOO_CORE_MATH_COMMIT)"
#endif

// CORE-MATH's correctly rounded binary64 functions (https://gitlab.inria.fr/core-math/core-math, src/binary64)
double cr_sin(double);
double cr_cos(double);
double cr_tan(double);
double cr_asin(double);
double cr_acos(double);
double cr_atan(double);
double cr_atan2(double, double);
double cr_exp(double);
double cr_log(double);
double cr_log10(double);
double cr_log2(double);
double cr_sinh(double);
double cr_cosh(double);
double cr_tanh(double);
double cr_pow(double, double);

// CORE-MATH has no sqrt: IEEE 754 requires sqrt to be correctly rounded, and the generator is built with
// -fno-builtin and -fno-fast-math on an SSE2 or AArch64 target, where sqrt is the hardware's correctly rounded one.
static double moo_cr_sqrt(double x)
{
    return sqrt(x);
}

#define MOO_MAX INT64_MAX
#define MOO_MIN (-INT64_MAX)
#define MOO_ONE ((int64_t)65536)

// ---------------------------------------------------------------------------------------------------------------
// docs/DESIGN.md "Boundary": moo's boundary rule, copied from Common/include/moo_number.h moo_numfromdouble so the
// generator stands alone:
// the IEEE bits decoded in integer arithmetic and rounded once, ties to even. Returns 0 (ok), 1 (NaN),
// 2 (infinite) or 3 (out of range); writes *out only on 0.
static int mooOracleFromDouble(double x, int64_t* out)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));

    int negative = (bits >> 63) != 0;
    int exponent = (int)((bits >> 52) & 0x7ff);
    uint64_t mantissa = bits & (((uint64_t)1 << 52) - 1);

    if (exponent == 0x7ff)
        return mantissa ? 1 : 2;

    if (exponent == 0)
        exponent = 1; // subnormal: no implicit bit
    else
        mantissa |= (uint64_t)1 << 52;

    int shift = exponent - 1059;
    uint64_t magnitude;

    if (mantissa == 0)
        magnitude = 0;
    else if (shift >= 0)
    {
        if (shift >= 63 || mantissa > ((uint64_t)INT64_MAX >> shift))
            return 3;
        magnitude = mantissa << shift;
    }
    else if (-shift >= 64)
        magnitude = 0;
    else
    {
        int drop = -shift;
        magnitude = mantissa >> drop;
        uint64_t rest = mantissa & (((uint64_t)1 << drop) - 1);
        uint64_t half = (uint64_t)1 << (drop - 1);
        if (rest > half || (rest == half && (magnitude & 1)))
            magnitude += 1;
    }

    if (magnitude > (uint64_t)INT64_MAX)
        return 3;

    *out = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return 0;
}

// The ideal answer (docs/DESIGN.md, "math library", "Accuracy oracle"): the correctly rounded double, converted by
// the boundary rule. A result beyond the range saturates to the nearest end (docs/DESIGN.md, "Overflow"); a NaN
// means the input is outside the function's domain, which the sample definitions exclude, so it stops the run.
static int64_t mooOracleIdeal(double y, const char* name, int64_t a, int64_t b)
{
    int64_t out = 0;
    int status = mooOracleFromDouble(y, &out);
    if (status == 0)
        return out;
    if (status == 1)
    {
        fprintf(stderr, "moo-math-oracle: %s(%lld, %lld) is NaN: the sample left the domain\n", name, (long long)a, (long long)b);
        exit(1);
    }
    return y > 0 ? MOO_MAX : MOO_MIN;
}

// docs/DESIGN.md "math library", "Accuracy oracle": the ideal is f of the input itself, so the input must be exact.
// The input raw value as the double that holds it exactly. Every sampled raw has at most 53 significant bits
// (mooOracleExact), so the conversion and the scaling by 2^-16 are exact.
static double mooOracleToDouble(int64_t raw)
{
    return (double)raw * (1.0 / 65536.0);
}

// A raw value with its low bits cleared so that it has at most 53 significant bits, toward zero: the largest such
// magnitude not above the input. Inputs at and above 2^53 raw are therefore sampled on the grid a double holds.
static int64_t mooOracleExact(int64_t raw)
{
    uint64_t m = raw < 0 ? (uint64_t)0 - (uint64_t)raw : (uint64_t)raw;
    int width = 0;
    for (uint64_t t = m; t; t >>= 1)
        width++;
    if (width > 53)
        m &= ~(((uint64_t)1 << (width - 53)) - 1);
    return raw < 0 ? -(int64_t)m : (int64_t)m;
}

// ---------------------------------------------------------------------------------------------------------------
// PCG32 (O'Neill's reference pcg32_random_r), the same generator the moo-oracle suite uses
typedef struct
{
    uint64_t state;
    uint64_t inc;
} MooPcg32;

static uint32_t mooPcg32Next(MooPcg32* rng)
{
    uint64_t old = rng->state;
    rng->state = old * 6364136223846793005ULL + rng->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static void mooPcg32Seed(MooPcg32* rng, uint64_t seed, uint64_t stream)
{
    rng->state = 0;
    rng->inc = (stream << 1u) | 1u;
    mooPcg32Next(rng);
    rng->state += seed;
    mooPcg32Next(rng);
}

static uint64_t mooPcg64(MooPcg32* rng)
{
    uint64_t hi = mooPcg32Next(rng);
    return (hi << 32) | mooPcg32Next(rng);
}

// uniform in [lo, hi], lo <= hi, by rejection
static int64_t mooUniform(MooPcg32* rng, int64_t lo, int64_t hi)
{
    uint64_t span = (uint64_t)hi - (uint64_t)lo;
    if (span == UINT64_MAX)
        return (int64_t)mooPcg64(rng);
    uint64_t n = span + 1;
    uint64_t limit = UINT64_MAX - (UINT64_MAX % n);
    uint64_t r;
    do
        r = mooPcg64(rng);
    while (r >= limit);
    return (int64_t)((uint64_t)lo + r % n);
}

// ---------------------------------------------------------------------------------------------------------------
// The nearest raw value of k * pi/2, for |k| <= 2^40: pi/2 * 2^16 = 0x1921F.B54442D18469898CC5..., kept as an
// integer part and a 64-bit fraction (rounded), so k times it is exact to far under half a raw unit.
static int64_t mooHalfPiMultiple(int64_t k)
{
    const uint64_t whole = 0x1921FULL;
    const uint64_t frac = 0xB54442D18469898DULL;
    uint64_t m = k < 0 ? (uint64_t)0 - (uint64_t)k : (uint64_t)k;

    // m * frac as a 128-bit product (hi, lo), in 32-bit halves
    uint64_t a0 = m & 0xffffffffULL, a1 = m >> 32;
    uint64_t b0 = frac & 0xffffffffULL, b1 = frac >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffULL) + (p10 & 0xffffffffULL);
    uint64_t lo = (p00 & 0xffffffffULL) | (mid << 32);
    uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);

    uint64_t r = m * whole + hi + (lo >> 63); // round to nearest on the 64-bit fraction
    return k < 0 ? -(int64_t)r : (int64_t)r;
}

// ---------------------------------------------------------------------------------------------------------------
// The sample definition of each function (README.md, "The samples").
typedef struct
{
    int64_t a;
    int64_t b;
    int64_t ideal;
} MooRecord;

typedef struct
{
    const char* name;
    int arity;
    double (*f1)(double);
    double (*f2)(double, double);
    int64_t lo, hi;         // the domain of the first input, raw
    int64_t coreLo, coreHi; // where the result is neither saturated nor rounded to 0, sampled densely
    int64_t lo2, hi2;       // the second input (arity 2)
    int64_t core2Lo, core2Hi;
    int exhaustive;         // every raw in [lo, hi]
    int trig;               // edges at multiples of pi/2
} MooFunction;

#define R(x) ((int64_t)((x) * 65536.0)) // a raw bound from a decimal one (bounds only, never a sample value)

static const MooFunction functions[] = {
    {"sin", 1, cr_sin, 0, MOO_MIN, MOO_MAX, R(-64), R(64), 0, 0, 0, 0, 0, 1},
    {"cos", 1, cr_cos, 0, MOO_MIN, MOO_MAX, R(-64), R(64), 0, 0, 0, 0, 0, 1},
    {"tan", 1, cr_tan, 0, MOO_MIN, MOO_MAX, R(-64), R(64), 0, 0, 0, 0, 0, 1},
    {"asin", 1, cr_asin, 0, -MOO_ONE, MOO_ONE, -MOO_ONE, MOO_ONE, 0, 0, 0, 0, 1, 0},
    {"acos", 1, cr_acos, 0, -MOO_ONE, MOO_ONE, -MOO_ONE, MOO_ONE, 0, 0, 0, 0, 1, 0},
    {"atan", 1, cr_atan, 0, MOO_MIN, MOO_MAX, R(-64), R(64), 0, 0, 0, 0, 0, 1},
    {"atan2", 2, 0, cr_atan2, MOO_MIN, MOO_MAX, R(-64), R(64), MOO_MIN, MOO_MAX, R(-64), R(64), 0, 0},
    {"exp", 1, cr_exp, 0, MOO_MIN, MOO_MAX, R(-12), R(33), 0, 0, 0, 0, 0, 0},
    {"log", 1, cr_log, 0, 1, MOO_MAX, 1, R(64), 0, 0, 0, 0, 0, 0},
    {"log10", 1, cr_log10, 0, 1, MOO_MAX, 1, R(64), 0, 0, 0, 0, 0, 0},
    {"log2", 1, cr_log2, 0, 1, MOO_MAX, 1, R(64), 0, 0, 0, 0, 0, 0},
    {"sinh", 1, cr_sinh, 0, MOO_MIN, MOO_MAX, R(-33), R(33), 0, 0, 0, 0, 0, 0},
    {"cosh", 1, cr_cosh, 0, MOO_MIN, MOO_MAX, R(-33), R(33), 0, 0, 0, 0, 0, 0},
    {"tanh", 1, cr_tanh, 0, MOO_MIN, MOO_MAX, R(-12), R(12), 0, 0, 0, 0, 0, 0},
    {"sqrt", 1, moo_cr_sqrt, 0, 0, MOO_MAX, 0, R(64), 0, 0, 0, 0, 0, 0},
    {"pow", 2, 0, cr_pow, MOO_MIN, MOO_MAX, R(-16), R(16), MOO_MIN, MOO_MAX, R(-8), R(8), 0, 0},
};

#define FUNCTION_COUNT ((int)(sizeof(functions) / sizeof(functions[0])))

// MOO_ORACLE_SAMPLES: the seeded draws per sampled function (README.md, "Size")
#ifndef MOO_ORACLE_SAMPLES
#define MOO_ORACLE_SAMPLES 65536
#endif
#define MOO_ORACLE_SEED 0x6d6f6f2d6d617468ULL // "moo-math"

static int64_t mooClamp(int64_t x, int64_t lo, int64_t hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

// pow is real for base > 0, for base 0 with exponent >= 0, and for base < 0 with an integer exponent (docs/DESIGN.md
// "pow and ^": 0 ^ negative is division by zero, a negative base with a non-integer exponent is a named error).
static int mooInDomain(const MooFunction* f, int64_t a, int64_t b)
{
    if (a < f->lo || a > f->hi)
        return 0;
    if (f->arity == 2 && (b < f->lo2 || b > f->hi2))
        return 0;
    if (strcmp(f->name, "pow") == 0)
    {
        if (a == 0 && b < 0)
            return 0;
        if (a < 0 && (b & 0xffff) != 0)
            return 0;
    }
    return 1;
}

// docs/DESIGN.md "math library", "Accuracy oracle" ("dense sampling elsewhere"). One draw, one of four ways with
// equal odds: in the dense core; anywhere in the domain; any bit width from 1 to
// 63 with either sign; or an edge moved by up to 8 raw units. Then put on the exact-double grid and into the domain.
static int64_t mooDraw(MooPcg32* rng, int64_t lo, int64_t hi, int64_t coreLo, int64_t coreHi, const int64_t* edges, int edgeCount)
{
    int64_t x;
    switch (mooPcg32Next(rng) & 3)
    {
    case 0:
        x = mooUniform(rng, coreLo, coreHi);
        break;
    case 1:
        x = mooUniform(rng, lo, hi);
        break;
    case 2:
    {
        int width = 1 + (int)(mooPcg32Next(rng) % 63);
        uint64_t m = mooPcg64(rng) & (((uint64_t)1 << width) - 1);
        m |= (uint64_t)1 << (width - 1);
        x = (lo < 0 && (mooPcg32Next(rng) & 1)) ? -(int64_t)m : (int64_t)m; // a domain with no negatives takes no sign
        break;
    }
    default:
    {
        int64_t e = edges[mooPcg32Next(rng) % (uint32_t)edgeCount];
        int64_t d = (int64_t)(mooPcg32Next(rng) % 17) - 8;
        x = (d > 0 && e > MOO_MAX - d) ? MOO_MAX : (d < 0 && e < MOO_MIN - d) ? MOO_MIN : e + d;
        break;
    }
    }
    return mooOracleExact(mooClamp(x, lo, hi));
}

// The edges of one input: 0, 1 and 2 raw, 1, the domain limits and one raw inside them, the core's ends, and (trig)
// the nearest raw of k * pi/2 with its two neighbours, for |k| <= 16 and k = +-2^j, 5 <= j <= 40. All put on the
// exact-double grid and into the domain.
static int mooEdges(int64_t lo, int64_t hi, int64_t coreLo, int64_t coreHi, int trig, int64_t* out)
{
    int n = 0;
    const int64_t fixedEdges[] = {0, 1, -1, 2, -2, MOO_ONE, -MOO_ONE, lo, hi, lo + 1, hi - 1, coreLo, coreHi};
    for (size_t i = 0; i < sizeof(fixedEdges) / sizeof(fixedEdges[0]); i++)
        out[n++] = fixedEdges[i];
    if (trig)
    {
        for (int64_t k = -16; k <= 16; k++)
            for (int d = -1; d <= 1; d++)
                out[n++] = mooHalfPiMultiple(k) + d;
        for (int j = 5; j <= 40; j++)
            for (int s = -1; s <= 1; s += 2)
                for (int d = -1; d <= 1; d++)
                    out[n++] = mooHalfPiMultiple(s * ((int64_t)1 << j)) + d;
    }
    for (int i = 0; i < n; i++)
        out[i] = mooOracleExact(mooClamp(out[i], lo, hi));
    return n;
}

static int mooRecordCompare(const void* x, const void* y)
{
    const MooRecord* p = (const MooRecord*)x;
    const MooRecord* q = (const MooRecord*)y;
    if (p->a != q->a)
        return p->a < q->a ? -1 : 1;
    if (p->b != q->b)
        return p->b < q->b ? -1 : 1;
    return 0;
}

static double mooEval(const MooFunction* f, int64_t a, int64_t b)
{
    if (f->arity == 1)
        return f->f1(mooOracleToDouble(a));
    return f->f2(mooOracleToDouble(a), mooOracleToDouble(b));
}

// ---------------------------------------------------------------------------------------------------------------
// The file (README.md, "Format"): little-endian, written byte by byte.
typedef struct
{
    unsigned char* data;
    size_t size, capacity;
} MooBuffer;

static void mooPut(MooBuffer* buf, const void* bytes, size_t n)
{
    if (buf->size + n > buf->capacity)
    {
        buf->capacity = (buf->size + n) * 2;
        buf->data = (unsigned char*)realloc(buf->data, buf->capacity);
        if (!buf->data)
            exit(1);
    }
    memcpy(buf->data + buf->size, bytes, n);
    buf->size += n;
}

static void mooPutU8(MooBuffer* buf, unsigned v)
{
    unsigned char c = (unsigned char)v;
    mooPut(buf, &c, 1);
}

static void mooPutU32(MooBuffer* buf, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        mooPutU8(buf, (v >> (8 * i)) & 0xff);
}

static void mooPutU64(MooBuffer* buf, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        mooPutU8(buf, (unsigned)((v >> (8 * i)) & 0xff));
}

// zigzag LEB128 of a signed difference, taken modulo 2^64
static void mooPutVarint(MooBuffer* buf, uint64_t delta)
{
    uint64_t z = (delta << 1) ^ (uint64_t)((int64_t)delta >> 63);
    while (z >= 0x80)
    {
        mooPutU8(buf, (unsigned)(z & 0x7f) | 0x80);
        z >>= 7;
    }
    mooPutU8(buf, (unsigned)z);
}

static uint64_t mooFnv1a(const unsigned char* p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: moo-math-oracle OUTPUT.bin\n");
        return 2;
    }

    char header[1024];
    snprintf(header, sizeof(header),
        "moo math oracle v1\n"
        "core-math %s\n"
        "ideal = moo_numfromdouble(cr_f(x)), ties to even, out of range saturates; sqrt is IEEE sqrt\n"
        "inputs: at most 53 significant bits; exhaustive for asin and acos over [-1, 1]; otherwise %d PCG32 draws "
        "(seed 0x%016llx, stream = function index) plus edges, sorted, duplicates removed\n",
        MOO_CORE_MATH_COMMIT, MOO_ORACLE_SAMPLES, (unsigned long long)MOO_ORACLE_SEED);

    MooBuffer out = {0, 0, 0};
    mooPut(&out, "MOOMATH1", 8);
    mooPutU32(&out, 1);
    mooPutU32(&out, (uint32_t)strlen(header));
    mooPut(&out, header, strlen(header));
    mooPutU32(&out, (uint32_t)FUNCTION_COUNT);

    for (int fi = 0; fi < FUNCTION_COUNT; fi++)
    {
        const MooFunction* f = &functions[fi];
        size_t capacity = (f->exhaustive ? (size_t)(f->hi - f->lo + 1) : (size_t)MOO_ORACLE_SAMPLES) + 64 * 64;
        MooRecord* records = (MooRecord*)malloc(capacity * sizeof(MooRecord));
        size_t count = 0;

        if (f->exhaustive)
        {
            for (int64_t a = f->lo; a <= f->hi; a++)
                records[count++] = (MooRecord){a, 0, 0};
        }
        else
        {
            int64_t edges[512], edges2[512];
            int edgeCount = mooEdges(f->lo, f->hi, f->coreLo, f->coreHi, f->trig, edges);
            int edgeCount2 = f->arity == 2 ? mooEdges(f->lo2, f->hi2, f->core2Lo, f->core2Hi, 0, edges2) : 0;

            if (f->arity == 1)
            {
                for (int i = 0; i < edgeCount; i++)
                    if (mooInDomain(f, edges[i], 0))
                        records[count++] = (MooRecord){edges[i], 0, 0};
            }
            else
            {
                // the first 13 edges of each input (0, 1, 2 raw, 1, the limits and the core's ends), crossed
                for (int i = 0; i < 13; i++)
                    for (int j = 0; j < 13; j++)
                        if (mooInDomain(f, edges[i], edges2[j]))
                            records[count++] = (MooRecord){edges[i], edges2[j], 0};
            }

            MooPcg32 rng;
            mooPcg32Seed(&rng, MOO_ORACLE_SEED, (uint64_t)fi);
            for (int i = 0; i < MOO_ORACLE_SAMPLES;)
            {
                int64_t a = mooDraw(&rng, f->lo, f->hi, f->coreLo, f->coreHi, edges, edgeCount);
                int64_t b = 0;
                if (f->arity == 2)
                    b = mooDraw(&rng, f->lo2, f->hi2, f->core2Lo, f->core2Hi, edges2, edgeCount2);
                if (strcmp(f->name, "pow") == 0 && a < 0)
                    b &= ~(int64_t)0xffff; // a negative base takes an integer exponent (mooInDomain)
                if (!mooInDomain(f, a, b))
                    continue;
                records[count++] = (MooRecord){a, b, 0};
                i++;
            }
        }

        qsort(records, count, sizeof(MooRecord), mooRecordCompare);
        size_t unique = 0;
        for (size_t i = 0; i < count; i++)
            if (unique == 0 || mooRecordCompare(&records[unique - 1], &records[i]) != 0)
                records[unique++] = records[i];
        count = unique;

        for (size_t i = 0; i < count; i++)
            records[i].ideal = mooOracleIdeal(mooEval(f, records[i].a, records[i].b), f->name, records[i].a, records[i].b);

        MooBuffer payload = {0, 0, 0};
        int64_t prevA = 0, prevIdeal = 0;
        for (size_t i = 0; i < count; i++)
        {
            mooPutVarint(&payload, (uint64_t)records[i].a - (uint64_t)prevA);
            if (f->arity == 2)
                mooPutVarint(&payload, (uint64_t)records[i].b);
            mooPutVarint(&payload, (uint64_t)records[i].ideal - (uint64_t)prevIdeal);
            prevA = records[i].a;
            prevIdeal = records[i].ideal;
        }

        mooPutU8(&out, (unsigned)strlen(f->name));
        mooPut(&out, f->name, strlen(f->name));
        mooPutU8(&out, (unsigned)f->arity);
        mooPutU8(&out, (unsigned)f->exhaustive);
        mooPutU64(&out, (uint64_t)count);
        mooPutU64(&out, (uint64_t)payload.size);
        mooPut(&out, payload.data, payload.size);

        fprintf(stderr, "moo-math-oracle %s records=%zu bytes=%zu\n", f->name, count, payload.size);
        free(payload.data);
        free(records);
    }

    mooPutU64(&out, mooFnv1a(out.data, out.size));

    FILE* file = fopen(argv[1], "wb");
    if (!file || fwrite(out.data, 1, out.size, file) != out.size || fclose(file) != 0)
    {
        fprintf(stderr, "moo-math-oracle: cannot write %s\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "moo-math-oracle wrote %s bytes=%zu fnv1a=%016llx\n", argv[1], out.size,
        (unsigned long long)mooFnv1a(out.data, out.size - 8));
    free(out.data);
    return 0;
}
