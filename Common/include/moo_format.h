// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's number formatting (docs/DESIGN.md "Number formatting", "string.format"), in integer arithmetic, no libc
// printf. Every Q48.16 value has an exact decimal expansion of at most 16 fraction digits: raw / 2^16 = raw * 5^16 /
// 10^16.
//
// moo_numtostr: the shortest decimal that reads back (moo_parse.h) to the same value, so tonumber(tostring(x)) == x.
// moo_numformat: C's %f %e %g %E %G with flags, width and precision, rounded half to even on the exact expansion.
#pragma once

#include "moo_number.h"

#include <stdint.h>
#include <string.h>

namespace moo_format_detail
{

const uint64_t kPow10[17] = {1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull, 100000000ull, 1000000000ull,
    10000000000ull, 100000000000ull, 1000000000000ull, 10000000000000ull, 100000000000000ull, 1000000000000000ull, 10000000000000000ull};
const uint64_t kPow5[17] = {1ull, 5ull, 25ull, 125ull, 625ull, 3125ull, 15625ull, 78125ull, 390625ull, 1953125ull, 9765625ull, 48828125ull,
    244140625ull, 1220703125ull, 6103515625ull, 30517578125ull, 152587890625ull};

// n / d rounded half to even (d > 0)
inline uint64_t divEven(uint64_t n, uint64_t d)
{
    uint64_t q = n / d, r = n % d;
    if (r * 2 > d || (r * 2 == d && (q & 1)))
        q++;
    return q;
}

// writes the decimal digits of v (at least one) and returns the end
inline char* putUint(char* p, uint64_t v)
{
    char tmp[24];
    int n = 0;
    do
    {
        tmp[n++] = char('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    return p;
}

// writes v as exactly width digits, zero-padded
inline char* putUintPadded(char* p, uint64_t v, int width)
{
    for (int i = width - 1; i >= 0; --i)
    {
        p[i] = char('0' + v % 10);
        v /= 10;
    }
    return p + width;
}

} // namespace moo_format_detail

// the shortest decimal that reads back to exactly n; returns the end (at most 34 characters, not terminated)
inline char* moo_numtostr(char* buf, fixed_t n)
{
    using namespace moo_format_detail;
    char* p = buf;
    uint64_t m = moo_numabs(n);
    if (n < 0)
        *p++ = '-';
    uint64_t ip = m >> 16;
    uint64_t f = m & 0xffff;
    if (f == 0)
        return putUint(p, ip);

    uint64_t frac16 = f * kPow5[16]; // the exact 16 fraction digits
    for (int k = 1; k <= 16; ++k)
    {
        uint64_t q = divEven(frac16, kPow10[16 - k]); // k fraction digits
        uint64_t cip = ip;
        if (q == kPow10[k]) // the rounding carried into the integer part
        {
            cip++;
            q = 0;
        }
        // read back: round(cip + q / 10^k) at 2^-16; the fraction is q * 2^(16-k) / 5^k
        uint64_t back = divEven(q << (16 - k), kPow5[k]);
        if (cip * 65536 + back == m)
        {
            p = putUint(p, cip);
            if (q == 0)
                return p;
            *p++ = '.';
            int digits = k;
            while (q % 10 == 0) // the shortest form has no trailing zero; strip any to be sure
            {
                q /= 10;
                digits--;
            }
            return putUintPadded(p, q, digits);
        }
    }
    // unreachable: 16 digits are exact
    return putUint(p, ip);
}

// C's printf for a number: spec is the conversion's flags, width and precision (no '%'), conv one of f e E g G.
// Writes at most cap - 1 characters and a terminating 0; returns the length (or -1 if it does not fit).
inline int moo_numformat(char* out, size_t cap, const char* spec, char conv, fixed_t n)
{
    using namespace moo_format_detail;

    bool left = false, plus = false, space = false, alt = false, zero = false;
    const char* s = spec;
    for (; *s == '-' || *s == '+' || *s == ' ' || *s == '#' || *s == '0'; ++s)
    {
        left |= *s == '-';
        plus |= *s == '+';
        space |= *s == ' ';
        alt |= *s == '#';
        zero |= *s == '0';
    }
    int width = 0;
    while (*s >= '0' && *s <= '9')
        width = width * 10 + (*s++ - '0');
    int prec = 6;
    if (*s == '.')
    {
        ++s;
        prec = 0;
        while (*s >= '0' && *s <= '9')
            prec = prec * 10 + (*s++ - '0');
    }

    bool neg = n < 0;
    uint64_t m = moo_numabs(n);
    uint64_t ip = m >> 16;
    uint64_t frac16 = (m & 0xffff) * kPow5[16];

    // the exact value as a digit string: integer digits, then 16 fraction digits; point after nint digits
    char digits[40];
    char* e = putUint(digits, ip);
    int nint = int(e - digits);
    e = putUintPadded(e, frac16, 16);
    int ndig = int(e - digits);

    char body[160]; // the digits, the point and the exponent, without sign or padding
    int blen = 0;
    bool upper = conv == 'E' || conv == 'G';
    char kind = (conv == 'f' || conv == 'F') ? 'f' : (conv == 'e' || conv == 'E') ? 'e' : 'g';

    // the first `keep` digits rounded half to even into r; returns the count, keep + 1 when a carry adds a leading 1
    auto roundDigits = [&](int keep, char* r) -> int {
        for (int i = 0; i < keep; ++i)
            r[i] = i < ndig ? digits[i] : '0';
        bool up = false;
        if (keep < ndig)
        {
            char nd = digits[keep];
            bool rest = false;
            for (int i = keep + 1; i < ndig; ++i)
                rest |= digits[i] != '0';
            if (nd > '5' || (nd == '5' && rest))
                up = true;
            else if (nd == '5' && !rest)
                up = keep > 0 ? ((r[keep - 1] - '0') & 1) : false; // a tie: to even
        }
        int len = keep;
        if (up)
        {
            int i = keep - 1;
            while (i >= 0 && r[i] == '9')
                r[i--] = '0';
            if (i >= 0)
                r[i]++;
            else
            {
                memmove(r + 1, r, size_t(keep));
                r[0] = '1';
                len++;
            }
        }
        return len;
    };

    // fixed notation with p fraction digits
    auto emitFixed = [&](int p, bool stripZeros) {
        char r[160];
        int keep = nint + (p < 16 ? p : 16);
        int len = roundDigits(keep, r);
        int intDigits = nint + (len - keep);
        // strip leading zeros of the integer part, keep one
        int start = 0;
        while (start < intDigits - 1 && r[start] == '0')
            start++;
        for (int i = start; i < intDigits; ++i)
            body[blen++] = r[i];
        int fd = p;
        char fr[120];
        for (int i = 0; i < fd; ++i)
            fr[i] = (intDigits + i < len) ? r[intDigits + i] : '0';
        if (stripZeros)
            while (fd > 0 && fr[fd - 1] == '0')
                fd--;
        if (fd > 0 || alt)
            body[blen++] = '.';
        for (int i = 0; i < fd; ++i)
            body[blen++] = fr[i];
    };

    // the index of the leading significant digit in digits[]
    int lead = 0;
    while (lead < ndig && digits[lead] == '0')
        lead++;
    bool isZero = lead == ndig;

    // the p + 1 significant digits of scientific notation into r, and the decimal exponent
    auto sciParts = [&](int p, char* r, int* exp10) -> int {
        if (isZero)
        {
            for (int i = 0; i <= p; ++i)
                r[i] = '0';
            *exp10 = 0;
            return p + 1;
        }
        int x = nint - 1 - lead; // the exponent of digits[lead]
        char tmp[160];
        int keep = lead + p + 1;
        int len = roundDigits(keep, tmp);
        int first = lead;
        if (len > keep)
        {
            // the carry ran off the front (lead == 0): the digits moved right by one behind a new leading 1
            for (int i = 0; i < p + 1; ++i)
                r[i] = tmp[i];
            x += 1;
        }
        else
        {
            // a carry into the zero before the leading digit (lead > 0)
            if (first > 0 && tmp[first - 1] != '0')
            {
                first--;
                x += 1;
            }
            for (int i = 0; i < p + 1; ++i)
                r[i] = tmp[first + i];
        }
        *exp10 = x;
        return p + 1;
    };

    auto emitSci = [&](int p, bool stripZeros) {
        char r[160];
        int x;
        sciParts(p, r, &x);
        body[blen++] = r[0];
        int fd = p;
        if (stripZeros)
            while (fd > 0 && r[fd] == '0')
                fd--;
        if (fd > 0 || alt)
            body[blen++] = '.';
        for (int i = 1; i <= fd; ++i)
            body[blen++] = r[i];
        body[blen++] = upper ? 'E' : 'e';
        body[blen++] = x < 0 ? '-' : '+';
        int ax = x < 0 ? -x : x;
        if (ax < 10)
            body[blen++] = '0';
        char t[8];
        char* te = putUint(t, uint64_t(ax));
        for (char* q = t; q < te; ++q)
            body[blen++] = *q;
    };

    if (kind == 'f')
        emitFixed(prec, false);
    else if (kind == 'e')
        emitSci(prec, false);
    else
    {
        int P = prec == 0 ? 1 : prec;
        char r[160];
        int x;
        sciParts(P - 1, r, &x);
        if (P > x && x >= -4)
            emitFixed(P - 1 - x, !alt);
        else
            emitSci(P - 1, !alt);
    }

    // the sign is the value's, as in C: -0.01 with %.1f is "-0.0"
    char sign = neg ? '-' : plus ? '+' : space ? ' ' : 0;

    int total = blen + (sign ? 1 : 0);
    int pad = width > total ? width - total : 0;
    if (size_t(total + pad) + 1 > cap)
        return -1;
    char* o = out;
    if (!left && !zero)
        for (int i = 0; i < pad; ++i)
            *o++ = ' ';
    if (sign)
        *o++ = sign;
    if (!left && zero)
        for (int i = 0; i < pad; ++i)
            *o++ = '0';
    memcpy(o, body, size_t(blen));
    o += blen;
    if (left)
        for (int i = 0; i < pad; ++i)
            *o++ = ' ';
    *o = 0;
    return int(o - out);
}
