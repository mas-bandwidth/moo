// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's pow for an integer exponent: the exact power of a Q48.16 base, rounded once to the nearest Q48.16, ties to
// even, saturating, with a status for saturation and 0 ^ negative (docs/DESIGN.md "pow and ^"). Integer arithmetic
// only. Every pow path computes with it: the ^ operator (luai_numpow, LOP_POW/LOP_POWK, luaV_doarithimpl) discards
// the status; the compiler's constant folding folds only MOO_POW_OK; and math.pow and its fastcall raise on it
// (docs/layer4/moo-l4-pow-integer.md has the design and each caller's steps).
//
// For raw base b with a = |b| and exponent n the value is V = 2^16 * (a / 2^16)^|n|, or 2^16 / (a / 2^16)^|n| for
// n < 0, negative when b < 0 and n is odd, rounded half to even. Two paths:
//
//   exact, |n| <= MOO_POW_EXACT_MAX: D = a^|n| as a 256-bit integer. n > 0: V = D / 2^(16(n-1)), a shift; n < 0:
//   V = 2^(16|n|+16) / D, one division. If D does not fit in 256 bits, V >= 2^64 (saturates) or V <= 2^-32
//   (rounds to 0).
//
//   interval, |n| > MOO_POW_EXACT_MAX: (a / 2^16)^|n| bracketed by lo <= P <= hi, each a 128-bit mantissa with a
//   binary exponent, by binary exponentiation rounding lo down and hi up. While no bit has been dropped lo == hi
//   is exact, so exact results and ties are seen exactly. The answer is certain when both bounds round to the same
//   value. Only bases near 1 reach the final rounding, a finite set, and MooPowIntCertainEverywhere (tests/moo/
//   moo-l4-pow-integer-design.test.cpp) checks every one of them is certain. The loop stops early once a bound
//   shows the result saturates or rounds to 0, which also keeps the exponent small.
#pragma once

#include "moo_number.h"
#include "fixed/fixed_int256.h"

#include <stdint.h>

// moo_pow_int's status. Only math.pow and its fastcall raise on it; the ^ operator keeps *out and raises nothing.
#define MOO_POW_OK 0
#define MOO_POW_SATURATED 1        // *out is MOO_NUMBER_MAX or MOO_NUMBER_MIN; math.pow: "number overflow" when checked
#define MOO_POW_DIVISION_BY_ZERO 2 // 0 ^ negative; *out is 0; math.pow: "division by zero" in every build

// the largest |n| computed exactly in 256 bits
#define MOO_POW_EXACT_MAX 13

namespace moo_pow_detail
{

// a wide value m * 2^e, m in [2^127, 2^128)
struct Wide
{
    fixUInt128 m;
    int e;
};

inline fixUInt128 top128()
{
    return fixUInt128Make(uint64_t(1) << 63, 0);
}

// a * b exactly, 256 bits
inline fixUInt256 mul128(fixUInt128 a, fixUInt128 b)
{
    uint64_t a0 = fixUInt128Lo(a), a1 = fixUInt128Hi(a), b0 = fixUInt128Lo(b), b1 = fixUInt128Hi(b);
    fixUInt256 r;
    r.lo = fixUInt128MulU64(a0, b0);
    r.hi = fixUInt128MulU64(a1, b1);
    fixUInt128 c1 = fixUInt128MulU64(a0, b1);
    fixUInt128 c2 = fixUInt128MulU64(a1, b0);
    fixUInt256 s;
    s.hi = fixUInt128Shr(c1, 64);
    s.lo = fixUInt128Shl(c1, 64);
    r = fixUInt256Add(r, s);
    s.hi = fixUInt128Shr(c2, 64);
    s.lo = fixUInt128Shl(c2, 64);
    return fixUInt256Add(r, s);
}

// the next wide value up (m + 1, renormalized)
inline Wide bumpUp(Wide w)
{
    w.m = fixUInt128Add(w.m, fixUInt128FromU64(1));
    if (fixUInt128Eq(w.m, FIX_UINT128_ZERO)) // was 2^128 - 1
    {
        w.m = top128();
        w.e += 1;
    }
    return w;
}

// x * y rounded down into *down and up into *up; returns true when the product is exact (sticky clear)
inline bool mul(Wide x, Wide y, Wide* down, Wide* up)
{
    fixUInt256 p = mul128(x.m, y.m); // in [2^254, 2^256)
    bool top = !fixUInt128Eq(fixUInt128Shr(p.hi, 127), FIX_UINT128_ZERO);
    Wide r;
    bool sticky;
    if (top)
    {
        r.m = p.hi;
        r.e = x.e + y.e + 128;
        sticky = !fixUInt128Eq(p.lo, FIX_UINT128_ZERO);
    }
    else
    {
        r.m = fixUInt128Or(fixUInt128Shl(p.hi, 1), fixUInt128Shr(p.lo, 127));
        r.e = x.e + y.e + 127;
        sticky = !fixUInt128Eq(fixUInt128Shl(p.lo, 1), FIX_UINT128_ZERO);
    }
    *down = r;
    *up = sticky ? bumpUp(r) : r;
    return !sticky;
}

// 1 / w rounded down (up = false) or up, as a wide value: 2^255 / m in (2^127, 2^128], times 2^(-255 - e)
inline Wide reciprocal(Wide w, bool up)
{
    fixUInt256 q, r;
    fixUInt256DivMod(fixUInt256Shl(fixUInt256FromU64(1), 255), fixUInt256FromU128(w.m), &q, &r);
    Wide out;
    out.e = -255 - w.e;
    if (!fixUInt128Eq(q.hi, FIX_UINT128_ZERO)) // q == 2^128, only when m == 2^127, exact
    {
        out.m = top128();
        out.e += 1;
        return out;
    }
    out.m = q.lo;
    return (up && !fixUInt256IsZero(r)) ? bumpUp(out) : out;
}

const uint64_t kSaturated = uint64_t(1) << 63; // any magnitude at or above 2^63 saturates

// m * 2^e rounded to an integer, half to even, clamped to kSaturated (m in [2^127, 2^128])
inline uint64_t roundWide(fixUInt128 m, int e)
{
    if (e >= 0)
        return kSaturated;
    int s = -e;
    if (s > 128)
        return 0; // below 2^128 * 2^-129 = 1/2
    if (s == 128)
        return fixUInt128Eq(m, top128()) ? 0 : 1; // in [1/2, 1): exactly 1/2 is the tie to 0
    fixUInt128 i = fixUInt128Shr(m, s);
    if (fixUInt128Hi(i) != 0 || fixUInt128Lo(i) >= kSaturated)
        return kSaturated;
    fixUInt128 rest = fixUInt128Sub(m, fixUInt128Shl(i, s));
    fixUInt128 half = fixUInt128Shl(fixUInt128FromU64(1), s - 1);
    uint64_t v = fixUInt128Lo(i);
    if (fixUInt128Gt(rest, half) || (fixUInt128Eq(rest, half) && (v & 1)))
        v += 1;
    return v; // at most 2^63
}

// D = a^n exactly (n >= 1); false when it does not fit in 256 bits
inline bool exactPower(uint64_t a, uint64_t n, fixUInt256* out)
{
    uint64_t d[4] = {1, 0, 0, 0};
    for (uint64_t k = 0; k < n; ++k)
    {
        uint64_t carry = 0;
        for (int i = 0; i < 4; ++i)
        {
            fixUInt128 t = fixUInt128Add(fixUInt128MulU64(d[i], a), fixUInt128FromU64(carry));
            d[i] = fixUInt128Lo(t);
            carry = fixUInt128Hi(t);
        }
        if (carry != 0)
            return false;
    }
    *out = fixUInt256FromLimbs(d);
    return true;
}

inline bool isBelow2to63(fixUInt256 v)
{
    return fixUInt128Eq(v.hi, FIX_UINT128_ZERO) && fixUInt128Hi(v.lo) == 0 && fixUInt128Lo(v.lo) < kSaturated;
}

// the rounded magnitude for 1 <= n <= MOO_POW_EXACT_MAX, clamped to kSaturated
inline uint64_t exactMagnitude(uint64_t a, uint64_t n, bool negativeExponent)
{
    fixUInt256 d;
    if (!exactPower(a, n, &d))
        return negativeExponent ? 0 : kSaturated; // D >= 2^256: V >= 2^64, or V <= 2^-32

    if (!negativeExponent)
    {
        int s = int(16 * (n - 1)); // V = D / 2^s, s <= 192
        fixUInt256 i = fixUInt256Shr(d, s);
        if (!isBelow2to63(i))
            return kSaturated;
        uint64_t v = fixUInt128Lo(i.lo);
        if (s == 0)
            return v;
        fixUInt256 rest = fixUInt256Sub(d, fixUInt256Shl(i, s));
        fixUInt256 half = fixUInt256Shl(fixUInt256FromU64(1), s - 1);
        if (fixUInt256Lt(half, rest) || (!fixUInt256Lt(rest, half) && (v & 1)))
            v += 1;
        return v;
    }

    // V = 2^(16n + 16) / D, 16n + 16 <= 224
    fixUInt256 q, r;
    fixUInt256DivMod(fixUInt256Shl(fixUInt256FromU64(1), int(16 * n + 16)), d, &q, &r);
    if (!isBelow2to63(q))
        return kSaturated;
    uint64_t v = fixUInt128Lo(q.lo);
    fixUInt256 other = fixUInt256Sub(d, r); // r > d - r is r > d / 2
    if (fixUInt256Lt(other, r) || (!fixUInt256Lt(r, other) && (v & 1)))
        v += 1;
    return v;
}

// the rounded magnitude for n > MOO_POW_EXACT_MAX, clamped to kSaturated; *certain is true when both bounds round
// to the same value (1 <= a, a != 2^16)
inline uint64_t intervalMagnitude(uint64_t a, uint64_t n, bool negativeExponent, bool* certain)
{
    int bits = 64 - fixCountLeadingZeros64(a);
    Wide x;
    x.m = fixUInt128Shl(fixUInt128FromU64(a), 128 - bits);
    x.e = -16 - (128 - bits); // x = a / 2^16

    bool above = a > (uint64_t(1) << 16); // x > 1: the power grows with each step
    Wide lo = x, hi = x;
    bool exact = true;

    int top = 63 - fixCountLeadingZeros64(n);
    for (int i = top - 1; i >= 0; --i)
    {
        Wide unused;
        if (exact)
            exact = mul(lo, lo, &lo, &hi);
        else
        {
            mul(lo, lo, &lo, &unused);
            mul(hi, hi, &unused, &hi);
        }
        if ((n >> i) & 1)
        {
            if (exact)
                exact = mul(lo, x, &lo, &hi);
            else
            {
                mul(lo, x, &lo, &unused);
                mul(hi, x, &unused, &hi);
            }
        }

        // stop once a bound shows the result is out of range or below 1/2: lo >= 2^(lo.e + 127), hi < 2^(hi.e + 128),
        // and the power only moves away from 1 from here
        if (above && lo.e + 127 >= (negativeExponent ? 18 : 48))
        {
            *certain = true;
            return negativeExponent ? 0 : kSaturated; // P >= 2^18: V <= 2^-2; P >= 2^48: V >= 2^64
        }
        if (!above && hi.e + 128 <= (negativeExponent ? -48 : -18))
        {
            *certain = true;
            return negativeExponent ? kSaturated : 0; // P < 2^-48: V > 2^64; P < 2^-18: V < 2^-2
        }
    }

    uint64_t vlo, vhi;
    if (!negativeExponent)
    {
        vlo = roundWide(lo.m, lo.e + 16); // V = 2^16 * P
        vhi = roundWide(hi.m, hi.e + 16);
    }
    else
    {
        Wide rlo = reciprocal(hi, false); // V = 2^16 / P
        Wide rhi = reciprocal(lo, true);
        vlo = roundWide(rlo.m, rlo.e + 16);
        vhi = roundWide(rhi.m, rhi.e + 16);
    }
    *certain = vlo == vhi;
    return vlo; // MooPowIntCertainEverywhere shows vlo == vhi for every input that gets here
}

} // namespace moo_pow_detail

// moo_pow_int, with *certain from the interval path (true on every other path). For the tests.
inline int moo_pow_int_certain(fixed_t base, int64_t exponent, fixed_t* out, bool* certain)
{
    using namespace moo_pow_detail;
    *certain = true;

    if (exponent == 0) // x ^ 0 = 1 for every x, 0 ^ 0 included (Lua's rule)
    {
        *out = MOO_NUMBER_ONE;
        return MOO_POW_OK;
    }

    bool negativeExponent = exponent < 0;
    uint64_t n = moo_numabs(exponent);
    uint64_t a = moo_numabs(base);
    bool negative = base < 0 && (n & 1);

    if (a == 0) // 0 ^ negative is division by zero
    {
        *out = 0;
        return negativeExponent ? MOO_POW_DIVISION_BY_ZERO : MOO_POW_OK;
    }

    uint64_t magnitude;
    if (a == (uint64_t(1) << 16)) // |x| = 1
        magnitude = a;
    else if (n <= MOO_POW_EXACT_MAX)
        magnitude = exactMagnitude(a, n, negativeExponent);
    else
        magnitude = intervalMagnitude(a, n, negativeExponent, certain);

    if (magnitude >= kSaturated)
    {
        *out = negative ? MOO_NUMBER_MIN : MOO_NUMBER_MAX;
        return MOO_POW_SATURATED;
    }
    *out = negative ? -fixed_t(magnitude) : fixed_t(magnitude);
    return MOO_POW_OK;
}

// base ^ exponent, the exact power rounded once, half to even. Returns a MOO_POW_ status with *out the release
// value. An exact edge (MOO_NUMBER_MAX ^ 1) is not saturation.
inline int moo_pow_int(fixed_t base, int64_t exponent, fixed_t* out)
{
    bool certain;
    return moo_pow_int_certain(base, exponent, out, &certain);
}
