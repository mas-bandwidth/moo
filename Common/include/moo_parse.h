// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's number parser (docs/DESIGN.md "Literals"): a decimal is read as the exact rational it denotes and rounded
// once to the nearest Q48.16, ties to even, with no strtod and no locale. 0x and 0b literals are exact integers.
// The compiler's parser and tonumber both use it.
#pragma once

#include "moo_number.h"

#include <stdint.h>
#include <string.h>

#define MOO_PARSE_OK 0
#define MOO_PARSE_MALFORMED 1
#define MOO_PARSE_OUT_OF_RANGE 2   // the value rounds to a magnitude above the largest number
#define MOO_PARSE_BELOW_RESOLUTION 3 // not zero, but rounds to 0 (*out is 0): a warning, not an error

#define MOO_PARSE_MAX_DIGITS 1000 // significant decimal digits; longer is malformed

namespace moo_parse_detail
{

// a fixed-capacity unsigned integer in 32-bit limbs, little-endian
struct Big
{
    enum
    {
        kLimbs = 128 // 4096 bits: 10^(MOO_PARSE_MAX_DIGITS + 6) * 2^17 fits
    };
    uint32_t limb[kLimbs];
    int n; // used limbs; no leading zero limbs

    void zero()
    {
        n = 0;
    }

    void trim()
    {
        while (n > 0 && limb[n - 1] == 0)
            n--;
    }

    // this = this * m + a; false on overflow of the capacity
    bool muladd(uint32_t m, uint32_t a)
    {
        uint64_t carry = a;
        for (int i = 0; i < n; ++i)
        {
            uint64_t t = uint64_t(limb[i]) * m + carry;
            limb[i] = uint32_t(t);
            carry = t >> 32;
        }
        if (carry)
        {
            if (n == kLimbs)
                return false;
            limb[n++] = uint32_t(carry);
        }
        return true;
    }

    bool shl(int bits) // bits < 32
    {
        if (bits == 0)
            return true;
        uint32_t carry = 0;
        for (int i = 0; i < n; ++i)
        {
            uint32_t v = limb[i];
            limb[i] = (v << bits) | carry;
            carry = v >> (32 - bits);
        }
        if (carry)
        {
            if (n == kLimbs)
                return false;
            limb[n++] = carry;
        }
        return true;
    }
};

inline int cmp(const Big& a, const Big& b)
{
    if (a.n != b.n)
        return a.n < b.n ? -1 : 1;
    for (int i = a.n - 1; i >= 0; --i)
        if (a.limb[i] != b.limb[i])
            return a.limb[i] < b.limb[i] ? -1 : 1;
    return 0;
}

// a -= b, a >= b
inline void sub(Big& a, const Big& b)
{
    int64_t borrow = 0;
    for (int i = 0; i < a.n; ++i)
    {
        int64_t t = int64_t(a.limb[i]) - (i < b.n ? int64_t(b.limb[i]) : 0) - borrow;
        borrow = t < 0;
        a.limb[i] = uint32_t(t + (borrow << 32));
    }
    a.trim();
}

} // namespace moo_parse_detail

// Parses the whole of s (no spaces, no sign, no '_': the caller strips them) into *out.
inline int moo_numparse(const char* s, fixed_t* out)
{
    using namespace moo_parse_detail;

    // hexadecimal and binary: integers
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'b' || s[1] == 'B'))
    {
        int base = (s[1] == 'x' || s[1] == 'X') ? 16 : 2;
        const char* p = s + 2;
        if (!*p)
            return MOO_PARSE_MALFORMED;
        uint64_t v = 0;
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
            if (v > (uint64_t(MOO_NUMBER_INT_MAX) - uint64_t(d)) / uint64_t(base))
                over = true;
            else
                v = v * base + d;
        }
        if (over)
            return MOO_PARSE_OUT_OF_RANGE;
        *out = moo_numfromint(int64_t(v));
        return MOO_PARSE_OK;
    }

    // decimal: digits [. digits] [e|E [+|-] digits]
    Big d;
    d.zero();
    int sig = 0;      // significant digits taken into d
    long exp10 = 0;   // value = d * 10^exp10
    bool any = false; // saw a digit
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
        *out = 0;
        return MOO_PARSE_OK;
    }

    // the value has sig significant digits: it lies in [10^(sig - 1 + exp10), 10^(sig + exp10))
    long top = sig + exp10; // the value is below 10^top
    if (top > 15)           // at least 10^15 > 2^47
        return MOO_PARSE_OUT_OF_RANGE;
    if (top < -5) // below 10^-6, under half a unit (2^-17 is about 7.6e-6)
    {
        *out = 0;
        return MOO_PARSE_BELOW_RESOLUTION;
    }

    // raw = round(d * 10^exp10 * 2^16): numerator d * 2^16 * 10^max(exp10, 0), denominator 10^max(-exp10, 0)
    Big num = d;
    if (!num.shl(16))
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

    // q = num / den by long division one bit at a time: shift the denominator up past the numerator, then walk it
    // down. A quotient bit at 2^64 or above is out of range.
    Big rem = num;
    Big sh = den;
    int shift = 0;
    while (cmp(sh, rem) <= 0)
    {
        if (!sh.shl(1))
            return MOO_PARSE_MALFORMED;
        shift++;
    }
    uint64_t q = 0;
    bool over = false;
    for (int b = shift - 1; b >= 0; --b)
    {
        // sh = den << b, rebuilt from den each step (shifts are cheap at these sizes)
        Big step = den;
        int left = b;
        while (left > 0)
        {
            int k = left > 31 ? 31 : left;
            step.shl(k);
            left -= k;
        }
        if (b >= 64)
        {
            if (cmp(step, rem) <= 0)
                over = true;
            continue;
        }
        if (cmp(step, rem) <= 0)
        {
            sub(rem, step);
            q |= uint64_t(1) << b;
        }
    }
    if (over)
        return MOO_PARSE_OUT_OF_RANGE;

    // round half to even: compare 2 * rem with den
    Big twice = rem;
    twice.shl(1);
    int c = cmp(twice, den);
    if (c > 0 || (c == 0 && (q & 1)))
    {
        if (q == UINT64_MAX)
            return MOO_PARSE_OUT_OF_RANGE;
        q += 1;
    }

    if (q > uint64_t(MOO_NUMBER_MAX))
        return MOO_PARSE_OUT_OF_RANGE;
    *out = fixed_t(q);
    return q == 0 ? MOO_PARSE_BELOW_RESOLUTION : MOO_PARSE_OK;
}
