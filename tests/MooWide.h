// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// A signed 128-bit value as two 64-bit words, for test references only, with no compiler 128-bit integer type: the
// 64 x 64 -> 128 multiply is _umul128 on MSVC and four 32-bit partial products elsewhere (the
// CORE-MATH port's portable path). It is deliberately independent of fixed's own fixInt128.
#pragma once

#include <stdint.h>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

struct MooWide
{
    uint64_t lo = 0;
    int64_t hi = 0;

    static MooWide from(int64_t v)
    {
        MooWide w;
        w.lo = uint64_t(v);
        w.hi = v < 0 ? -1 : 0;
        return w;
    }

    friend MooWide operator+(MooWide a, MooWide b)
    {
        MooWide r;
        r.lo = a.lo + b.lo;
        r.hi = int64_t(uint64_t(a.hi) + uint64_t(b.hi) + (r.lo < a.lo ? 1 : 0));
        return r;
    }

    friend MooWide operator-(MooWide a)
    {
        MooWide r;
        r.lo = ~a.lo + 1;
        r.hi = int64_t(~uint64_t(a.hi) + (r.lo == 0 ? 1 : 0));
        return r;
    }

    friend MooWide operator-(MooWide a, MooWide b)
    {
        return a + (-b);
    }

    friend bool operator<(MooWide a, MooWide b)
    {
        return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo;
    }

    friend bool operator==(MooWide a, MooWide b)
    {
        return a.hi == b.hi && a.lo == b.lo;
    }

    friend bool operator<=(MooWide a, MooWide b)
    {
        return !(b < a);
    }

    // arithmetic shift right by 0 < n < 64
    MooWide shr(int n) const
    {
        MooWide r;
        r.lo = (lo >> n) | (uint64_t(hi) << (64 - n));
        r.hi = hi >> n;
        return r;
    }

    // shift left by 0 < n < 64
    MooWide shl(int n) const
    {
        MooWide r;
        r.lo = lo << n;
        r.hi = int64_t((uint64_t(hi) << n) | (lo >> (64 - n)));
        return r;
    }

    bool negative() const
    {
        return hi < 0;
    }
};

inline void mooUmul128(uint64_t a, uint64_t b, uint64_t* lo, uint64_t* hi)
{
#if defined(_MSC_VER) && defined(_M_X64)
    *lo = _umul128(a, b, hi);
#else
    uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    *lo = (mid << 32) | (p00 & 0xffffffffu);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
}

// a * b, exact
inline MooWide mooMulWide(int64_t a, int64_t b)
{
    bool neg = (a < 0) != (b < 0);
    uint64_t ua = a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
    uint64_t ub = b < 0 ? uint64_t(0) - uint64_t(b) : uint64_t(b);
    MooWide r;
    uint64_t hi;
    mooUmul128(ua, ub, &r.lo, &hi);
    r.hi = int64_t(hi);
    return neg ? -r : r;
}

// clamps into moo's number domain [-INT64_MAX, INT64_MAX]
inline int64_t mooClampWide(MooWide v)
{
    if (MooWide::from(INT64_MAX) < v)
        return INT64_MAX;
    if (v < MooWide::from(-INT64_MAX))
        return -INT64_MAX;
    return int64_t(v.lo);
}
