// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's number: Q48.16 fixed point, the raw signed 64-bit value of fixed's fixed_t
// (docs/DESIGN.md, "Representation" to "Division by zero"). Every operator the VM applies to a
// number is here, so the VM's arithmetic has one definition.
//
// The domain is [MOO_NUMBER_MIN, MOO_NUMBER_MAX] = [-INT64_MAX, INT64_MAX] raw, fixed's
// [FIX_MIN, FIX_MAX]: about +-1.4e14, symmetric, so negation is exact and fixed's C, which does
// not take INT64_MIN, takes every value. Every operation saturates into the domain, never wraps
// and never produces INT64_MIN. Division, floor division and remainder by zero are not defined
// here: the VM raises the named error "division by zero" before calling them.
#pragma once

#include "fixed/fixed.h"

#include <stdint.h>
#include <string.h>

#define MOO_NUMBER_MAX FIX_MAX
#define MOO_NUMBER_MIN FIX_MIN
#define MOO_NUMBER_ONE FIX_ONE
#define MOO_NUMBER_FRACTION_BITS FIX_FRACTION_BITS

// the largest and smallest integer value a number holds (2^47 - 1 and its negation)
#define MOO_NUMBER_INT_MAX (MOO_NUMBER_MAX >> MOO_NUMBER_FRACTION_BITS)

// a + b, exact, saturating (a and b in the domain)
inline fixed_t moo_numadd(fixed_t a, fixed_t b)
{
    if (b > 0 && a > MOO_NUMBER_MAX - b)
        return MOO_NUMBER_MAX;
    if (b < 0 && a < MOO_NUMBER_MIN - b)
        return MOO_NUMBER_MIN;
    return a + b;
}

// a - b, exact, saturating (a and b in the domain)
inline fixed_t moo_numsub(fixed_t a, fixed_t b)
{
    if (b < 0 && a > MOO_NUMBER_MAX + b)
        return MOO_NUMBER_MAX;
    if (b > 0 && a < MOO_NUMBER_MIN + b)
        return MOO_NUMBER_MIN;
    return a - b;
}

// -a, exact (the domain is symmetric)
inline fixed_t moo_numunm(fixed_t a)
{
    return a == INT64_MIN ? MOO_NUMBER_MAX : -a;
}

// a * b: fixed's fixMul, round half up, saturating (FIX_SATURATE)
inline fixed_t moo_nummul(fixed_t a, fixed_t b)
{
    return fixMul(a, b);
}

// a / b, b != 0: fixed's fixDiv, truncates toward zero, saturating
inline fixed_t moo_numdiv(fixed_t a, fixed_t b)
{
    return fixDiv(a, b);
}

// floor(a / b) as a number, b != 0: the exact quotient's floor (both raws share the 2^16 scale,
// so the integer quotient of the raws is the quotient's integer part), saturating
inline fixed_t moo_numidiv(fixed_t a, fixed_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    if (q > MOO_NUMBER_INT_MAX)
        return MOO_NUMBER_MAX;
    if (q < -MOO_NUMBER_INT_MAX)
        return MOO_NUMBER_MIN;
    return fixShiftLeft(q, MOO_NUMBER_FRACTION_BITS);
}

// a - floor(a / b) * b, b != 0: exact, with the sign of b (Lua's rule)
inline fixed_t moo_nummod(fixed_t a, fixed_t b)
{
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
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

// the number of iterations of `for i = from, to, step` (step != 0): one for each k >= 0 with from + k * step between
// from and to inclusive, computed exactly before the loop starts (tla/NumericFor.tla). At most 2^64 - 1.
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

// TEMPORARY BRIDGE until the math library is rebuilt on fixed (docs/DESIGN.md, "math library"): the
// number nearest a double from libm, ties to even, saturating; NaN gives 0. Only the math library and
// the float vector type call it, and both go in later layer 3 PRs.
inline fixed_t moo_numbridgefromdouble(double d)
{
    if (d != d)
        return 0;
    if (d >= 140737488355328.0)
        return MOO_NUMBER_MAX;
    if (d <= -140737488355328.0)
        return MOO_NUMBER_MIN;
    double scaled = d * 65536.0; // exact
    double r = scaled - double(int64_t(scaled));
    int64_t t = int64_t(scaled); // toward zero, in range
    double frac = r < 0 ? -r : r;
    if (frac > 0.5 || (frac == 0.5 && (t & 1)))
        t += scaled < 0 ? -1 : 1;
    if (t > MOO_NUMBER_MAX)
        return MOO_NUMBER_MAX;
    if (t < MOO_NUMBER_MIN)
        return MOO_NUMBER_MIN;
    return t;
}

inline double moo_numbridgetodouble(fixed_t a)
{
    return double(a) * (1.0 / 65536.0);
}

// the boundary rule (moo.h moo_fromdouble): the IEEE bits decoded in integer arithmetic and rounded once, ties to even
// returns 0 (0), 1 (NaN), 2 (infinite) or 3 (out of range); writes *out only on 0
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
    {
        int drop = -shift;
        magnitude = mantissa >> drop;
        uint64_t rest = mantissa & ((uint64_t(1) << drop) - 1);
        uint64_t half = uint64_t(1) << (drop - 1);
        if (rest > half || (rest == half && (magnitude & 1)))
            magnitude += 1;
    }

    if (magnitude > uint64_t(INT64_MAX))
        return 3;

    *out = negative ? -int64_t(magnitude) : int64_t(magnitude);
    return 0;
}

// the IEEE-754 binary32 bits nearest the exact value raw / 2^16, ties to even, in integer arithmetic
// (docs/DESIGN.md, "Boundary"): the boundary rule going out of the VM, and buffer.writef32 and
// string.pack's float encoding. Every Q48.16 value is a finite normal binary32, so there is no
// subnormal, infinity or overflow case. raw = 0 gives +0.0's bits.
inline uint32_t moo_numtofloatbits(fixed_t raw)
{
    if (raw == 0)
        return 0;

    uint32_t sign = raw < 0 ? 0x80000000u : 0u;
    uint64_t m = raw < 0 ? uint64_t(0) - uint64_t(raw) : uint64_t(raw);

    int b = 0;
    for (uint64_t x = m; x; x >>= 1)
        b += 1;
    int E = b - 17; // value = m * 2^-16 = significand * 2^E, significand in [1, 2)

    uint64_t M24;
    if (b <= 24)
        M24 = m << (24 - b);
    else
    {
        int shift = b - 24;
        M24 = m >> shift;
        uint64_t rest = m & ((uint64_t(1) << shift) - 1);
        uint64_t half = uint64_t(1) << (shift - 1);
        if (rest > half || (rest == half && (M24 & 1)))
            M24 += 1;
    }

    if (M24 == (uint64_t(1) << 24)) // rounded up to 2.0
    {
        M24 = uint64_t(1) << 23;
        E += 1;
    }

    return sign | uint32_t(E + 127) << 23 | uint32_t(M24 - (uint64_t(1) << 23));
}

// the IEEE-754 binary64 bits nearest the exact value raw / 2^16, ties to even, in integer arithmetic
// (docs/DESIGN.md, "Boundary"): moo_todouble's one rule. Every Q48.16 value is a finite normal
// binary64, so there is no subnormal, infinity or overflow case. raw = 0 gives +0.0's bits.
inline uint64_t moo_numtodoublebits(fixed_t raw)
{
    if (raw == 0)
        return 0;

    uint64_t sign = raw < 0 ? 0x8000000000000000ull : 0ull;
    uint64_t m = raw < 0 ? uint64_t(0) - uint64_t(raw) : uint64_t(raw);

    int b = 0;
    for (uint64_t x = m; x; x >>= 1)
        b += 1;
    int E = b - 17; // value = m * 2^-16 = significand * 2^E, significand in [1, 2)

    uint64_t M53;
    if (b <= 53)
        M53 = m << (53 - b);
    else
    {
        int shift = b - 53;
        M53 = m >> shift;
        uint64_t rest = m & ((uint64_t(1) << shift) - 1);
        uint64_t half = uint64_t(1) << (shift - 1);
        if (rest > half || (rest == half && (M53 & 1)))
            M53 += 1;
    }

    if (M53 == (uint64_t(1) << 53)) // rounded up to 2.0
    {
        M53 = uint64_t(1) << 52;
        E += 1;
    }

    return sign | uint64_t(E + 1023) << 52 | (M53 - (uint64_t(1) << 52));
}
