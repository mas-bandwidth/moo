// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's number: Q48.16 fixed point, the raw int64 of fixed's fixed_t. Every number operation the VM does is here.
//
// The domain is [-INT64_MAX, INT64_MAX] raw (FIX_MIN, FIX_MAX), about +-1.4e14. It is symmetric, so negation is
// exact. Every operation saturates into it and never produces INT64_MIN. Division, floor division and remainder by
// zero are not defined here: the VM raises "division by zero" before calling them.
#pragma once

#include "fixed/fixed.h"

#include <stdint.h>
#include <string.h>

#define MOO_NUMBER_MAX FIX_MAX
#define MOO_NUMBER_MIN FIX_MIN
#define MOO_NUMBER_ONE FIX_ONE
#define MOO_NUMBER_FRACTION_BITS FIX_FRACTION_BITS

// the largest integer a number holds, 2^47 - 1; the smallest is its negation
#define MOO_NUMBER_INT_MAX (MOO_NUMBER_MAX >> MOO_NUMBER_FRACTION_BITS)

// MOO_OVERFLOW_ERRORS 1: the VM raises "number overflow in '<op>'" where release saturates, and the compiler does
// not fold that operation (docs/DESIGN.md "Overflow"). The exception is ^, which saturates silently in every build;
// only math.pow raises. Set by CMake MOO_OVERFLOW_ERRORS or make overflow=on/off; otherwise on exactly when
// LUAU_ASSERT is.
#if !defined(MOO_OVERFLOW_ERRORS)
#if !defined(NDEBUG) || defined(LUAU_ENABLE_ASSERT)
#define MOO_OVERFLOW_ERRORS 1
#else
#define MOO_OVERFLOW_ERRORS 0
#endif
#endif

// |a| as unsigned, exact for every int64
inline uint64_t moo_numabs(int64_t a)
{
    return a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
}

// m >> shift rounded to nearest, ties to even (1 <= shift <= 63)
inline uint64_t moo_numshiftround(uint64_t m, int shift)
{
    uint64_t q = m >> shift;
    uint64_t rest = m & ((uint64_t(1) << shift) - 1);
    uint64_t half = uint64_t(1) << (shift - 1);
    if (rest > half || (rest == half && (q & 1)))
        q += 1;
    return q;
}

// floor(a / b) as an integer, b != 0. Both raws share the 2^16 scale, so it is the quotient's integer part.
inline int64_t moo_numfloorquot(fixed_t a, fixed_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    return q;
}

// a * b rounded half up at 128 bits, before saturation: fixMul's product on fixed's int128 seam, for
// moo_nummulsaturates
inline fixInt128 moo_nummulwide(fixed_t a, fixed_t b)
{
    return fixInt128Shr(fixInt128Add(fixInt128MulI64(a, b), fixInt128FromI64(FIX_HALF)), FIX_FRACTION_BITS);
}

// true when the value has no fraction bits
inline bool moo_numisinteger(fixed_t a)
{
    return (a & (MOO_NUMBER_ONE - 1)) == 0;
}

// i clamped to the int range
inline int64_t moo_numclampint(int64_t i)
{
    return i < INT32_MIN ? INT32_MIN : (i > INT32_MAX ? INT32_MAX : i);
}

// the number with integer value i, saturating
inline fixed_t moo_numfromint(int64_t i)
{
    if (i > MOO_NUMBER_INT_MAX)
        return MOO_NUMBER_MAX;
    if (i < -MOO_NUMBER_INT_MAX)
        return MOO_NUMBER_MIN;
    return fixShiftLeft(i, MOO_NUMBER_FRACTION_BITS);
}

// a + b, saturating
inline fixed_t moo_numadd(fixed_t a, fixed_t b)
{
    return fixAddSaturate(a, b);
}

// a - b, saturating
inline fixed_t moo_numsub(fixed_t a, fixed_t b)
{
    return fixSubSaturate(a, b);
}

// -a, exact on the domain
inline fixed_t moo_numunm(fixed_t a)
{
    return a == INT64_MIN ? MOO_NUMBER_MAX : -a;
}

// a * b: fixed's fixMul, round half up, saturating (FIX_SATURATE); vec3 * shares it through fixVecMul
inline fixed_t moo_nummul(fixed_t a, fixed_t b)
{
    return fixMul(a, b);
}

// a / b, b != 0: fixed's fixDiv, truncates toward zero, saturating
inline fixed_t moo_numdiv(fixed_t a, fixed_t b)
{
    return fixDiv(a, b);
}

// floor(a / b) as a number, b != 0, saturating
inline fixed_t moo_numidiv(fixed_t a, fixed_t b)
{
    return moo_numfromint(moo_numfloorquot(a, b));
}

// a - floor(a / b) * b, b != 0: exact, with the sign of b (Lua's rule)
inline fixed_t moo_nummod(fixed_t a, fixed_t b)
{
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}

// Saturation tests for checked arithmetic: each is true exactly when the operation of the same name clamps, that
// is when its exact result (for * the rounded one) is outside the domain. An exact result at MOO_NUMBER_MAX or
// MOO_NUMBER_MIN is not saturation. Unary - and % never saturate. ^ has no test: the operator never raises, and
// math.pow reads moo_pow_int's status.

inline bool moo_numaddsaturates(fixed_t a, fixed_t b)
{
    return (b > 0 && a > MOO_NUMBER_MAX - b) || (b < 0 && a < MOO_NUMBER_MIN - b);
}

inline bool moo_numsubsaturates(fixed_t a, fixed_t b)
{
    return (b < 0 && a > MOO_NUMBER_MAX + b) || (b > 0 && a < MOO_NUMBER_MIN + b);
}

inline bool moo_nummulsaturates(fixed_t a, fixed_t b)
{
    fixInt128 r = moo_nummulwide(a, b);
    return fixInt128Gt(r, fixInt128FromI64(MOO_NUMBER_MAX)) || fixInt128Lt(r, fixInt128FromI64(MOO_NUMBER_MIN));
}

// b != 0 (division by zero is its own error): a * 2^16 / b is outside the domain exactly when |a| >> 47 >= |b|
inline bool moo_numdivsaturates(fixed_t a, fixed_t b)
{
    if (b == 0)
        return false;
    return (moo_numabs(a) >> 47) >= moo_numabs(b);
}

inline bool moo_numidivsaturates(fixed_t a, fixed_t b)
{
    if (b == 0)
        return false;
    int64_t q = moo_numfloorquot(a, b);
    return q > MOO_NUMBER_INT_MAX || q < -MOO_NUMBER_INT_MAX;
}

// the integer part, toward zero
inline int64_t moo_numtrunc(fixed_t a)
{
    return a >= 0 ? (a >> MOO_NUMBER_FRACTION_BITS) : -((-a) >> MOO_NUMBER_FRACTION_BITS);
}

// the largest integer not above a
inline int64_t moo_numfloor(fixed_t a)
{
    return a >> MOO_NUMBER_FRACTION_BITS; // arithmetic shift is floor
}

// the smallest integer not below a
inline int64_t moo_numceil(fixed_t a)
{
    return -moo_numfloor(-a);
}

// the number of iterations of `for i = from, to, step` (step != 0), exact, computed before the loop starts
// (tla/NumericFor.tla). At most 2^64 - 1.
inline uint64_t moo_numfortrips(fixed_t from, fixed_t to, fixed_t step)
{
    if (step > 0)
        return from <= to ? (uint64_t(to) - uint64_t(from)) / uint64_t(step) + 1 : 0;
    return from >= to ? (uint64_t(from) - uint64_t(to)) / (uint64_t(0) - uint64_t(step)) + 1 : 0;
}

// the nearest integer, halves away from zero (Luau's math.round), as a number
inline fixed_t moo_numround(fixed_t a)
{
    fixed_t m = a < 0 ? -a : a;
    int64_t i = (m >> MOO_NUMBER_FRACTION_BITS) + ((m & (MOO_NUMBER_ONE - 1)) >= (MOO_NUMBER_ONE >> 1) ? 1 : 0);
    fixed_t r = moo_numfromint(i);
    return a < 0 ? -r : r;
}

// true, with *out, when a is integer-valued and in int range (a table array index, a string position)
inline bool moo_numtoint(fixed_t a, int* out)
{
    if (!moo_numisinteger(a))
        return false;
    int64_t i = a >> MOO_NUMBER_FRACTION_BITS;
    if (i < INT32_MIN || i > INT32_MAX)
        return false;
    *out = int(i);
    return true;
}

// The boundary rule (docs/DESIGN.md "Boundary"): a double in, the nearest Q48.16, ties to even, decoded from the IEEE
// bits in integer arithmetic. Returns 0 (ok), 1 (NaN), 2 (infinite) or 3 (out of range); writes *out only on 0.
inline int moo_numfromdouble(double x, int64_t* out)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));

    bool negative = (bits >> 63) != 0;
    int exponent = int((bits >> 52) & 0x7ff);
    uint64_t mantissa = bits & ((uint64_t(1) << 52) - 1);

    if (exponent == 0x7ff)
        return mantissa ? 1 : 2;

    if (exponent == 0)
        exponent = 1; // subnormal: no implicit bit
    else
        mantissa |= uint64_t(1) << 52;

    // x = mantissa * 2^(exponent - 1075), so the raw value is mantissa * 2^(exponent - 1075 + 16)
    int shift = exponent - 1059;
    uint64_t magnitude;

    if (mantissa == 0)
        magnitude = 0;
    else if (shift >= 0)
    {
        if (shift >= 63 || mantissa > (uint64_t(INT64_MAX) >> shift))
            return 3;
        magnitude = mantissa << shift;
    }
    else if (-shift >= 64)
        magnitude = 0; // below 2^-64 units: far under half a unit
    else
        magnitude = moo_numshiftround(mantissa, -shift);

    if (magnitude > uint64_t(INT64_MAX))
        return 3;

    *out = negative ? -int64_t(magnitude) : int64_t(magnitude);
    return 0;
}

// float to double is exact, so a float takes the double rule
inline int moo_numfromfloat(float x, int64_t* out)
{
    return moo_numfromdouble(double(x), out);
}

// The boundary rule out: the IEEE-754 bits nearest raw / 2^16, ties to even, in integer arithmetic, for a format
// `width` bits wide with `bits` significand bits (the implicit one included). Every Q48.16 value is a finite normal
// binary32 and binary64, so there is no subnormal, infinity or overflow case. raw = 0 gives +0.0.
inline uint64_t moo_numtoieeebits(fixed_t raw, int bits, int width)
{
    if (raw == 0)
        return 0;

    uint64_t m = moo_numabs(raw);

    int b = 0;
    for (uint64_t x = m; x; x >>= 1)
        b += 1;
    int e = b - 17; // raw / 2^16 = significand * 2^e, significand in [1, 2)

    uint64_t significand = b <= bits ? m << (bits - b) : moo_numshiftround(m, b - bits);
    if (significand == (uint64_t(1) << bits)) // rounded up to 2.0
    {
        significand >>= 1;
        e += 1;
    }

    int bias = (1 << (width - bits - 1)) - 1;
    uint64_t sign = raw < 0 ? 1 : 0;
    return sign << (width - 1) | uint64_t(e + bias) << (bits - 1) | (significand - (uint64_t(1) << (bits - 1)));
}

// binary32 bits: moo_tofloat, buffer.writef32 and string.pack's float
inline uint32_t moo_numtofloatbits(fixed_t raw)
{
    return uint32_t(moo_numtoieeebits(raw, 24, 32));
}

// binary64 bits: moo_todouble, buffer.writef64 and string.pack's double
inline uint64_t moo_numtodoublebits(fixed_t raw)
{
    return moo_numtoieeebits(raw, 53, 64);
}

// the nearest double (moo.h moo_todouble)
inline double moo_numtodouble(fixed_t raw)
{
    uint64_t bits = moo_numtodoublebits(raw);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

// the nearest Q48.16 to a double (moo.h moo_fromdouble): NaN gives 0, an infinity or out-of-range value saturates
inline fixed_t moo_numfromdoublevalue(double x)
{
    int64_t out = 0;
    int kind = moo_numfromdouble(x, &out);
    if (kind == 0)
        return out;
    if (kind == 1)
        return 0;
    return x < 0 ? MOO_NUMBER_MIN : MOO_NUMBER_MAX;
}
