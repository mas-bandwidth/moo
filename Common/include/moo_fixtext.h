// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Exact decimal text for the fixed raws wider than number: Q112.16 wide (a 128-bit raw, 16 fraction bits) and Q32.32
// time (a 64-bit raw, 32 fraction bits). moo_format.h and moo_parse.h, generalised over the raw's width and its
// fraction bits (docs/DESIGN.md "Layer 3d as built", W4), in integer arithmetic: no libc, no locale, no float.
//
// moo_fixtostr writes the shortest decimal that moo_fixparse reads back to the same raw, so parse(format(x)) == x bit
// for bit. Every raw has an exact decimal expansion of at most `frac` fraction digits, since 2^-frac = 5^frac / 10^frac.
// moo_fixparse reads a decimal as the exact rational it denotes and rounds it once to the nearest raw, ties to even,
// as moo_numparse does; 0x and 0b literals are exact integers. Neither writes nor reads a suffix: that is the caller's.
//
// Raws are two's complement in two 64-bit halves (hi, lo); a 64-bit raw is sign-extended into hi. The reserved minimum
// (-2^(bits - 1)) is never a value: the parser's range is symmetric and the formatter is never given it.
#pragma once

#include "moo_parse.h"

#include <stdint.h>
#include <string.h>

// the longest moo_fixtostr output: a sign, 34 integer digits (2^111 < 10^34) and a point, or a sign, 10 integer
// digits (2^31 < 10^10), a point and 32 fraction digits
#define MOO_FIXTOSTR_MAX 52

namespace moo_fixtext_detail
{

// an unsigned 256-bit integer in eight 32-bit limbs, little-endian: room for (2^128 * 10^32) << 32
struct U256
{
    uint32_t limb[8];

    static U256 fromHalves(uint64_t hi, uint64_t lo)
    {
        U256 r;
        r.limb[0] = uint32_t(lo);
        r.limb[1] = uint32_t(lo >> 32);
        r.limb[2] = uint32_t(hi);
        r.limb[3] = uint32_t(hi >> 32);
        for (int i = 4; i < 8; ++i)
            r.limb[i] = 0;
        return r;
    }

    // this = this * m + a; the callers stay below 2^256 by construction
    void muladd(uint32_t m, uint32_t a)
    {
        uint64_t carry = a;
        for (int i = 0; i < 8; ++i)
        {
            uint64_t t = uint64_t(limb[i]) * m + carry;
            limb[i] = uint32_t(t);
            carry = t >> 32;
        }
    }

    // this << bits, bits < 64
    void shl(int bits)
    {
        while (bits > 0)
        {
            int k = bits > 31 ? 31 : bits;
            uint32_t carry = 0;
            for (int i = 0; i < 8; ++i)
            {
                uint32_t v = limb[i];
                limb[i] = (v << k) | carry;
                carry = v >> (32 - k);
            }
            bits -= k;
        }
    }

    // this / d, returns the remainder (d > 0)
    uint32_t divmod(uint32_t d)
    {
        uint64_t rem = 0;
        for (int i = 7; i >= 0; --i)
        {
            uint64_t cur = (rem << 32) | limb[i];
            limb[i] = uint32_t(cur / d);
            rem = cur % d;
        }
        return uint32_t(rem);
    }

    bool isZero() const
    {
        for (int i = 0; i < 8; ++i)
            if (limb[i])
                return false;
        return true;
    }
};

inline int cmp(const U256& a, const U256& b)
{
    for (int i = 7; i >= 0; --i)
        if (a.limb[i] != b.limb[i])
            return a.limb[i] < b.limb[i] ? -1 : 1;
    return 0;
}

// |a - b|
inline U256 absdiff(const U256& a, const U256& b)
{
    const U256& big = cmp(a, b) >= 0 ? a : b;
    const U256& small = cmp(a, b) >= 0 ? b : a;
    U256 r;
    int64_t borrow = 0;
    for (int i = 0; i < 8; ++i)
    {
        int64_t t = int64_t(big.limb[i]) - int64_t(small.limb[i]) - borrow;
        borrow = t < 0;
        r.limb[i] = uint32_t(t + (borrow << 32));
    }
    return r;
}

// the decimal digits of v (at least one) into p; returns the end
inline char* putU256(char* p, U256 v)
{
    char tmp[80];
    int n = 0;
    do
    {
        tmp[n++] = char('0' + v.divmod(10));
    } while (!v.isZero());
    while (n)
        *p++ = tmp[--n];
    return p;
}

// does ip + digits[0..k) / 10^k read back (rounded once at 2^-frac, ties to even) to the raw magnitude m? The test is
// exact: 2 |(ip * 10^k + q) * 2^frac - m * 10^k| < 10^k, or equal with m even.
inline bool readsBack(const U256& ip, const char* digits, int k, int frac, uint64_t mhi, uint64_t mlo)
{
    U256 x = ip;
    for (int i = 0; i < k; ++i)
        x.muladd(10, uint32_t(digits[i] - '0'));
    x.shl(frac);
    U256 y = U256::fromHalves(mhi, mlo);
    U256 tenk = U256::fromHalves(0, 1);
    for (int i = 0; i < k; ++i)
    {
        y.muladd(10, 0);
        tenk.muladd(10, 0);
    }
    U256 d = absdiff(x, y);
    d.shl(1);
    int c = cmp(d, tenk);
    return c < 0 || (c == 0 && (mlo & 1) == 0);
}

} // namespace moo_fixtext_detail

// The shortest decimal that moo_fixparse reads back to exactly the raw (hi, lo) with `frac` fraction bits (16 or 32);
// returns the end (at most MOO_FIXTOSTR_MAX characters, not terminated). Never given the reserved minimum.
inline char* moo_fixtostr(char* buf, uint64_t hi, uint64_t lo, int frac)
{
    using namespace moo_fixtext_detail;
    char* p = buf;
    bool neg = (hi >> 63) != 0;
    uint64_t mhi = hi, mlo = lo;
    if (neg)
    {
        // two's complement magnitude: 0 - (hi, lo)
        mlo = uint64_t(0) - lo;
        mhi = uint64_t(0) - hi - (lo != 0 ? 1 : 0);
        *p++ = '-';
    }

    // the integer part m >> frac and the fraction m mod 2^frac (frac <= 32, so the fraction fits 64 bits)
    uint64_t fmask = (uint64_t(1) << frac) - 1;
    uint64_t f = mlo & fmask;
    uint64_t iplo = (mlo >> frac) | (frac == 0 ? 0 : (mhi << (64 - frac)));
    uint64_t iphi = mhi >> frac;
    U256 ip = U256::fromHalves(iphi, iplo);
    if (f == 0)
        return putU256(p, ip);

    // the exact fraction digits, frac of them, each one ((f * 10) >> frac) in 64-bit arithmetic
    char exact[32];
    uint64_t r = f;
    for (int i = 0; i < frac; ++i)
    {
        r *= 10;
        exact[i] = char('0' + (r >> frac));
        r &= fmask;
    }

    for (int k = 1; k <= frac; ++k)
    {
        // the first k digits rounded half to even on the exact tail
        char q[32];
        memcpy(q, exact, size_t(k));
        bool up = false;
        if (k < frac)
        {
            char nd = exact[k];
            bool rest = false;
            for (int i = k + 1; i < frac; ++i)
                rest |= exact[i] != '0';
            up = nd > '5' || (nd == '5' && (rest || ((q[k - 1] - '0') & 1)));
        }
        U256 cip = ip;
        if (up)
        {
            int i = k - 1;
            while (i >= 0 && q[i] == '9')
                q[i--] = '0';
            if (i >= 0)
                q[i]++;
            else
                cip.muladd(1, 1); // the rounding carried into the integer part
        }
        if (readsBack(cip, q, k, frac, mhi, mlo))
        {
            p = putU256(p, cip);
            int digits = k;
            while (digits > 0 && q[digits - 1] == '0')
                digits--;
            if (digits == 0)
                return p;
            *p++ = '.';
            memcpy(p, q, size_t(digits));
            return p + digits;
        }
    }
    // unreachable: frac digits are exact
    return putU256(p, ip);
}

// Parses the whole of s (an optional leading '-', then digits; no spaces, no '_', no suffix) as a raw of `bits` bits
// (64 or 128) with `frac` fraction bits (16 or 32) into (*hi, *lo), sign-extended. The range is symmetric:
// |raw| <= 2^(bits - 1) - 1. Returns a MOO_PARSE_* code, as moo_numparse does.
inline int moo_fixparse(const char* s, int bits, int frac, uint64_t* hi, uint64_t* lo)
{
    using namespace moo_parse_detail;

    bool negative = false;
    if (*s == '-')
    {
        negative = true;
        ++s;
    }

    // the largest magnitude: 2^(bits - 1) - 1
    uint64_t maxhi = bits == 128 ? UINT64_C(0x7FFFFFFFFFFFFFFF) : 0;
    uint64_t maxlo = bits == 128 ? UINT64_MAX : UINT64_C(0x7FFFFFFFFFFFFFFF);

    uint64_t qhi = 0, qlo = 0;
    int status = MOO_PARSE_OK;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'b' || s[1] == 'B'))
    {
        // an integer: its raw is v << frac, so v has bits - 1 - frac bits of room
        int base = (s[1] == 'x' || s[1] == 'X') ? 16 : 2;
        const char* p = s + 2;
        if (!*p)
            return MOO_PARSE_MALFORMED;
        int room = bits - 1 - frac;
        bool over = false;
        for (; *p; ++p)
        {
            int d;
            if (*p >= '0' && *p <= '9')
                d = *p - '0';
            else if (base == 16 && *p >= 'a' && *p <= 'f')
                d = *p - 'a' + 10;
            else if (base == 16 && *p >= 'A' && *p <= 'F')
                d = *p - 'A' + 10;
            else
                return MOO_PARSE_MALFORMED;
            if (d >= base)
                return MOO_PARSE_MALFORMED;
            if (over)
                continue;
            // v = v * base + d, checked against 2^room
            int shift = base == 16 ? 4 : 1;
            uint64_t top = room >= 64 ? (qhi >> (room - 64)) : ((qhi != 0) ? 1 : (qlo >> room));
            if (top != 0)
            {
                over = true;
                continue;
            }
            qhi = (qhi << shift) | (qlo >> (64 - shift));
            qlo = (qlo << shift) | uint64_t(d);
            top = room >= 64 ? (qhi >> (room - 64)) : ((qhi != 0) ? 1 : (qlo >> room));
            if (top != 0)
                over = true;
        }
        if (over)
            return MOO_PARSE_OUT_OF_RANGE;
        // raw = v << frac
        qhi = (qhi << frac) | (qlo >> (64 - frac));
        qlo <<= frac;
    }
    else
    {
        // decimal: digits [. digits] [e|E [+|-] digits]
        Big d;
        d.zero();
        int sig = 0;
        long exp10 = 0;
        bool any = false;
        bool nonzero = false;
        const char* p = s;
        for (; *p >= '0' && *p <= '9'; ++p)
        {
            any = true;
            if (*p != '0' || nonzero)
            {
                nonzero = true;
                if (++sig > MOO_PARSE_MAX_DIGITS || !d.muladd(10, uint32_t(*p - '0')))
                    return MOO_PARSE_MALFORMED;
            }
        }
        if (*p == '.')
        {
            ++p;
            for (; *p >= '0' && *p <= '9'; ++p)
            {
                any = true;
                exp10--;
                if (*p != '0' || nonzero)
                {
                    nonzero = true;
                    if (++sig > MOO_PARSE_MAX_DIGITS || !d.muladd(10, uint32_t(*p - '0')))
                        return MOO_PARSE_MALFORMED;
                }
            }
        }
        if (!any)
            return MOO_PARSE_MALFORMED;
        if (*p == 'e' || *p == 'E')
        {
            ++p;
            bool neg = false;
            if (*p == '+' || *p == '-')
                neg = *p++ == '-';
            if (!(*p >= '0' && *p <= '9'))
                return MOO_PARSE_MALFORMED;
            long e = 0;
            for (; *p >= '0' && *p <= '9'; ++p)
                if (e < 100000000)
                    e = e * 10 + (*p - '0');
            exp10 += neg ? -e : e;
        }
        if (*p)
            return MOO_PARSE_MALFORMED;

        if (!nonzero)
        {
            *hi = 0;
            *lo = 0;
            return MOO_PARSE_OK;
        }

        // the value lies below 10^top. 10^40 is past every range here (2^127 < 10^39), and 10^-11 is under half of the
        // finest raw (2^-33 > 10^-11), so both ends are settled without the long division
        long top = sig + exp10;
        if (top > 40)
            return MOO_PARSE_OUT_OF_RANGE;
        if (top < -10)
        {
            *hi = 0;
            *lo = 0;
            return MOO_PARSE_BELOW_RESOLUTION;
        }

        // raw = round(d * 10^exp10 * 2^frac): numerator d * 2^frac * 10^max(exp10, 0), denominator 10^max(-exp10, 0)
        Big num = d;
        for (int left = frac; left > 0; left -= 16)
            if (!num.shl(left > 16 ? 16 : left))
                return MOO_PARSE_MALFORMED;
        for (long i = 0; i < exp10; ++i)
            if (!num.muladd(10, 0))
                return MOO_PARSE_MALFORMED;

        Big den;
        den.zero();
        den.limb[0] = 1;
        den.n = 1;
        for (long i = 0; i < -exp10; ++i)
            if (!den.muladd(10, 0))
                return MOO_PARSE_MALFORMED;

        // long division one bit at a time; a quotient bit at 2^128 or above is out of range
        Big rem = num;
        Big sh = den;
        int shift = 0;
        while (cmp(sh, rem) <= 0)
        {
            if (!sh.shl(1))
                return MOO_PARSE_MALFORMED;
            shift++;
        }
        bool over = false;
        for (int b = shift - 1; b >= 0; --b)
        {
            Big step = den;
            int left = b;
            while (left > 0)
            {
                int k = left > 31 ? 31 : left;
                step.shl(k);
                left -= k;
            }
            if (cmp(step, rem) <= 0)
            {
                if (b >= 128)
                {
                    over = true;
                    break;
                }
                sub(rem, step);
                if (b >= 64)
                    qhi |= uint64_t(1) << (b - 64);
                else
                    qlo |= uint64_t(1) << b;
            }
        }
        if (over)
            return MOO_PARSE_OUT_OF_RANGE;

        // round half to even: compare 2 * rem with den
        Big twice = rem;
        twice.shl(1);
        int c = cmp(twice, den);
        if (c > 0 || (c == 0 && (qlo & 1)))
        {
            qlo += 1;
            if (qlo == 0)
            {
                if (qhi == UINT64_MAX)
                    return MOO_PARSE_OUT_OF_RANGE;
                qhi += 1;
            }
        }
        if (qhi == 0 && qlo == 0)
            status = MOO_PARSE_BELOW_RESOLUTION;
    }

    if (qhi > maxhi || (qhi == maxhi && qlo > maxlo))
        return MOO_PARSE_OUT_OF_RANGE;

    if (negative && (qhi | qlo) != 0)
    {
        uint64_t nlo = uint64_t(0) - qlo;
        qhi = uint64_t(0) - qhi - (qlo != 0 ? 1 : 0);
        qlo = nlo;
    }
    else if (bits == 64)
    {
        qhi = 0;
    }
    *hi = qhi;
    *lo = qlo;
    return status;
}
