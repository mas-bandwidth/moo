// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// pow and ^: moo_pow_int is correctly rounded for an integer exponent, the compiler folds it, the VM
// and math.pow use it, and a fractional exponent uses fixPow (docs/DESIGN.md "math library").

#include "moo_pow.h"
#include "doctest.h"
#include "moo_format.h"
#include "lua.h"
#include "lualib.h"
#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"
#include "moo.h"
#include "moo_number.h"
#include "fixed/fixed_math.h"
#include "MooHost.h"

#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <vector>
#include <math.h>
#include <map>

TEST_SUITE_BEGIN("pow");

namespace
{

// The reference status and release value of base ^ exponent for any exponent, from moo_pow_int and fixPow
// directly: a negative base with a fractional exponent is its own status, and fixPow's MOO_NUMBER_MAX is
// saturation (docs/DESIGN.md "pow and ^").
int powReference(fixed_t base, fixed_t exponent, fixed_t* out)
{
    if (moo_numisinteger(exponent))
        return moo_pow_int(base, exponent >> MOO_NUMBER_FRACTION_BITS, out);
    if (base < 0)
    {
        *out = 0;
        return MOO_POW_NEGATIVE_BASE;
    }
    *out = fixPow(base, exponent);
    return *out == MOO_NUMBER_MAX ? MOO_POW_SATURATED : MOO_POW_OK;
}

// What every pow path gives for a status in this build, as mooRun prints it for a one-line chunk: the raw value,
// or the error. Written out here, never read from the VM's moo_numpowerror.
std::string powExpected(int status, fixed_t value)
{
    if (status == MOO_POW_DIVISION_BY_ZERO)
        return "error: moo:1: division by zero";
    if (status == MOO_POW_NEGATIVE_BASE)
        return "error: moo:1: pow of a negative number to a fractional power";
#if MOO_OVERFLOW_ERRORS
    if (status == MOO_POW_SATURATED)
        return "error: moo:1: number overflow in '^'";
#endif
    return std::to_string(static_cast<long long>(value));
}

// powExpected for base ^ exponent, from powReference
std::string powExpectedOf(fixed_t base, fixed_t exponent)
{
    fixed_t value = 0;
    int status = powReference(base, exponent, &value);
    return powExpected(status, value);
}

} // namespace

// moo_pow_int (Common/include/moo_pow.h; docs/DESIGN.md, "pow and ^"; docs/history/layer4/moo-l4-pow-integer.md):
// a Q48.16 base to an integer exponent, the exact power rounded once, half to even, saturating, with
// 0 ^ negative reported as division by zero. Every answer here is checked against an exact rational
// reference written in this file (Ref, unbounded integers in 32-bit limbs, sharing no code with
// moo_pow.h or fixed): the result V = N / D is exact, and the reference proves the returned raw R is
// round-half-even(V) by comparing 2N with (2R - 1) D and (2R + 1) D, and proves saturation by
// 2N >= (2^64 - 1) D (the rounded value is at least 2^63).
namespace pow_integer
{

namespace
{

// an unbounded unsigned integer, little-endian 32-bit limbs, no leading zero limbs
struct Ref
{
    std::vector<uint32_t> w;

    static Ref fromU64(uint64_t v)
    {
        Ref r;
        while (v)
        {
            r.w.push_back(uint32_t(v));
            v >>= 32;
        }
        return r;
    }

    void trim()
    {
        while (!w.empty() && w.back() == 0)
            w.pop_back();
    }

    void mulU32(uint32_t m)
    {
        uint64_t carry = 0;
        for (uint32_t& limb : w)
        {
            uint64_t t = uint64_t(limb) * m + carry;
            limb = uint32_t(t);
            carry = t >> 32;
        }
        if (carry)
            w.push_back(uint32_t(carry));
        trim();
    }

    static Ref shl(const Ref& a, uint64_t bits)
    {
        Ref r;
        if (a.w.empty())
            return r;
        size_t limbs = size_t(bits / 32);
        int s = int(bits % 32);
        r.w.assign(limbs, 0);
        uint32_t carry = 0;
        for (uint32_t limb : a.w)
        {
            r.w.push_back(s ? (limb << s) | carry : limb);
            carry = s ? limb >> (32 - s) : 0;
        }
        if (carry)
            r.w.push_back(carry);
        r.trim();
        return r;
    }

    static Ref add(const Ref& a, const Ref& b)
    {
        Ref r;
        size_t n = a.w.size() > b.w.size() ? a.w.size() : b.w.size();
        uint64_t carry = 0;
        for (size_t i = 0; i < n; ++i)
        {
            uint64_t t = carry + (i < a.w.size() ? a.w[i] : 0) + (i < b.w.size() ? b.w[i] : 0);
            r.w.push_back(uint32_t(t));
            carry = t >> 32;
        }
        if (carry)
            r.w.push_back(uint32_t(carry));
        r.trim();
        return r;
    }

    // a * m for any 64-bit m
    static Ref mulU64(const Ref& a, uint64_t m)
    {
        Ref lo = a, hi = a;
        lo.mulU32(uint32_t(m));
        hi.mulU32(uint32_t(m >> 32));
        return add(lo, shl(hi, 32));
    }

    static int cmp(const Ref& a, const Ref& b)
    {
        if (a.w.size() != b.w.size())
            return a.w.size() < b.w.size() ? -1 : 1;
        for (size_t i = a.w.size(); i-- > 0;)
            if (a.w[i] != b.w[i])
                return a.w[i] < b.w[i] ? -1 : 1;
        return 0;
    }

    static Ref power(uint64_t a, uint64_t n)
    {
        Ref r = fromU64(1);
        for (uint64_t k = 0; k < n; ++k)
            r = mulU64(r, a);
        return r;
    }
};

// What the reference proves about one call: an empty string when moo_pow_int's answer is the exact one,
// otherwise what is wrong. n is limited to what the reference can expand (|n| up to a few thousand).
std::string verify(int64_t base, int64_t exponent, int status, int64_t out)
{
    if (exponent == 0)
        return status == MOO_POW_OK && out == MOO_NUMBER_ONE ? "" : "x ^ 0 is not 1";

    bool neg = exponent < 0;
    uint64_t n = neg ? uint64_t(0) - uint64_t(exponent) : uint64_t(exponent);
    uint64_t a = base < 0 ? uint64_t(0) - uint64_t(base) : uint64_t(base);
    bool negative = base < 0 && (n & 1);

    if (a == 0)
    {
        if (neg)
            return status == MOO_POW_DIVISION_BY_ZERO && out == 0 ? "" : "0 ^ negative is not division by zero with 0";
        return status == MOO_POW_OK && out == 0 ? "" : "0 ^ positive is not 0";
    }
    if (status == MOO_POW_DIVISION_BY_ZERO)
        return "division by zero for a nonzero base";

    // V = N / D exactly
    Ref num, den;
    if (!neg)
    {
        num = Ref::power(a, n);
        den = Ref::shl(Ref::fromU64(1), 16 * (n - 1));
    }
    else
    {
        num = Ref::shl(Ref::fromU64(1), 16 * (n + 1));
        den = Ref::power(a, n);
    }
    Ref twoN = Ref::shl(num, 1);

    bool saturates = Ref::cmp(twoN, Ref::mulU64(den, ~uint64_t(0))) >= 0; // round(V) >= 2^63
    if (status == MOO_POW_SATURATED)
    {
        if (!saturates)
            return "saturated, but the rounded value is in range";
        return out == (negative ? MOO_NUMBER_MIN : MOO_NUMBER_MAX) ? "" : "saturated to the wrong edge";
    }
    if (status != MOO_POW_OK)
        return "unknown status";
    if (saturates)
        return "in range, but the rounded value saturates";

    uint64_t r = out < 0 ? uint64_t(0) - uint64_t(out) : uint64_t(out);
    if (r != 0 && (out < 0) != negative)
        return "wrong sign";

    // (2r - 1) D <= 2N <= (2r + 1) D, a tie only to an even r
    Ref upper = Ref::mulU64(den, 2 * r + 1);
    int c = Ref::cmp(twoN, upper);
    if (c > 0 || (c == 0 && (r & 1)))
        return "rounded too low";
    if (r > 0)
    {
        Ref lower = Ref::mulU64(den, 2 * r - 1);
        int d = Ref::cmp(twoN, lower);
        if (d < 0 || (d == 0 && (r & 1)))
            return "rounded too high";
    }
    return "";
}

// counts each wrong answer and shows the first few
struct Checker
{
    int failures = 0;
    int cases = 0;

    void check(int64_t base, int64_t exponent)
    {
        fixed_t out = 12345;
        int status = moo_pow_int(base, exponent, &out);
        std::string why = verify(base, exponent, status, out);
        cases++;
        if (!why.empty())
        {
            if (failures < 10)
                MESSAGE("moo_pow_int(" << base << ", " << exponent << ") = " << out << " status " << status << ": " << why);
            failures++;
        }
    }
};

// splitmix64: the seeded cases are the same on every platform
struct Seeded
{
    uint64_t state;

    uint64_t next()
    {
        uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    // a base near 1 whose power by n is likely in range: |x - 1| up to about 48 ln 2 / |n|
    int64_t nearOne(uint64_t n)
    {
        int64_t span = int64_t((uint64_t(1) << 16) * 48 / (n ? n : 1));
        int64_t a = (int64_t(1) << 16) + int64_t(next() % uint64_t(2 * span + 1)) - span;
        if (a < 1)
            a = 1;
        return (next() & 1) ? -a : a;
    }

    // a base of any size: a random bit length, then random bits
    int64_t anySize()
    {
        int bits = int(next() % 64); // 0..63
        uint64_t a = bits ? next() >> (64 - bits) : 0;
        return (next() & 1) ? -int64_t(a) : int64_t(a);
    }
};

} // namespace

TEST_CASE("PowIntExhaustiveSmallBases")
{
    // every raw base with |base| <= 2^12 and every exponent -8..8 (the exhaustive range)
    Checker c;
    for (int64_t b = -4096; b <= 4096; ++b)
        for (int64_t n = -8; n <= 8; ++n)
            c.check(b, n);
    CHECK(c.cases == 8193 * 17);
    CHECK(c.failures == 0);
}

TEST_CASE("PowIntExhaustiveNearOne")
{
    // every raw base within 2^12 of +-1 for exponents -8..8, and within 2^10 of +-1 for exponents 14..40 and
    // -40..-14 (the interval path, where the values that do not saturate or round to 0 live)
    Checker c;
    for (int64_t d = -4096; d <= 4096; ++d)
        for (int64_t n = -8; n <= 8; ++n)
        {
            c.check(65536 + d, n);
            c.check(-65536 - d, n);
        }
    for (int64_t d = -1024; d <= 1024; ++d)
        for (int64_t n = MOO_POW_EXACT_MAX + 1; n <= 40; ++n)
        {
            c.check(65536 + d, n);
            c.check(65536 + d, -n);
            c.check(-65536 - d, n);
            c.check(-65536 - d, -n);
        }
    // INT64_MIN, outside the domain (|base| = 2^63), is still answered exactly
    for (int64_t n = -16; n <= 16; ++n)
        c.check(INT64_MIN, n);
    CHECK(c.failures == 0);
}

TEST_CASE("PowIntIntegerBases")
{
    // integer bases -64..64 for exponents -24..24: exact integers, exact reciprocals of powers of two, and
    // the edges of the range (2 ^ 46 is in range, 2 ^ 47 saturates)
    Checker c;
    for (int64_t i = -64; i <= 64; ++i)
        for (int64_t n = -24; n <= 24; ++n)
            c.check(i * 65536, n);
    CHECK(c.failures == 0);
}

TEST_CASE("PowIntSeeded")
{
    // 2^16 seeded cases: exponents -64..64, a quarter with bases of any size, the rest near 1
    Seeded rng{0x6d6f6f706f77ull};
    Checker c;
    for (int k = 0; k < 65536; ++k)
    {
        int64_t n = int64_t(rng.next() % 129) - 64;
        uint64_t m = n < 0 ? uint64_t(-n) : uint64_t(n);
        int64_t base = (rng.next() & 3) == 0 ? rng.anySize() : rng.nearOne(m);
        c.check(base, n);
    }
    CHECK(c.cases == 65536);
    CHECK(c.failures == 0);
}

TEST_CASE("PowIntLargeExponents")
{
    // 256 seeded cases with |n| from 14 to 1013, bases near 1, against the exact reference
    Seeded rng{0x6c61726765ull};
    Checker c;
    for (int k = 0; k < 256; ++k)
    {
        uint64_t m = 14 + rng.next() % 1000;
        int64_t n = (rng.next() & 1) ? -int64_t(m) : int64_t(m);
        c.check(rng.nearOne(m), n);
    }
    CHECK(c.failures == 0);

    // exponents past what the reference expands, with answers known exactly
    fixed_t out = 0;
    const int64_t e40 = int64_t(1) << 40;
    CHECK(moo_pow_int(65537, e40, &out) == MOO_POW_SATURATED); // (1 + 2^-16) ^ 2^40 >= 2^47
    CHECK(out == MOO_NUMBER_MAX);
    CHECK(moo_pow_int(65537, -e40, &out) == MOO_POW_OK);
    CHECK(out == 0);
    CHECK(moo_pow_int(65535, e40, &out) == MOO_POW_OK); // (1 - 2^-16) ^ 2^40 is far below 2^-17
    CHECK(out == 0);
    CHECK(moo_pow_int(65535, -e40, &out) == MOO_POW_SATURATED);
    CHECK(out == MOO_NUMBER_MAX);
    CHECK(moo_pow_int(-65537, e40 + 1, &out) == MOO_POW_SATURATED); // odd: the negative edge
    CHECK(out == MOO_NUMBER_MIN);
    CHECK(moo_pow_int(-65536, INT64_MAX, &out) == MOO_POW_OK); // (-1) ^ odd
    CHECK(out == -65536);
    CHECK(moo_pow_int(-65536, INT64_MIN, &out) == MOO_POW_OK); // (-1) ^ even, |n| = 2^63
    CHECK(out == 65536);
    CHECK(moo_pow_int(MOO_NUMBER_MAX, INT64_MAX, &out) == MOO_POW_SATURATED);
    CHECK(out == MOO_NUMBER_MAX);
    CHECK(moo_pow_int(MOO_NUMBER_MAX, INT64_MIN, &out) == MOO_POW_OK);
    CHECK(out == 0);
    CHECK(moo_pow_int(1, INT64_MIN, &out) == MOO_POW_SATURATED); // (2^-16) ^ -2^63
    CHECK(moo_pow_int(1, INT64_MAX, &out) == MOO_POW_OK);
    CHECK(out == 0);
}

TEST_CASE("PowIntReportsSaturationAndDivisionByZero")
{
    fixed_t out = 7;

    // 0 ^ negative is division by zero (docs/DESIGN.md, "Division by zero"); 0 ^ 0 = 1; 0 ^ positive = 0
    CHECK(moo_pow_int(0, -1, &out) == MOO_POW_DIVISION_BY_ZERO);
    CHECK(out == 0);
    CHECK(moo_pow_int(0, INT64_MIN, &out) == MOO_POW_DIVISION_BY_ZERO);
    CHECK(moo_pow_int(0, 0, &out) == MOO_POW_OK);
    CHECK(out == MOO_NUMBER_ONE);
    CHECK(moo_pow_int(0, 3, &out) == MOO_POW_OK);
    CHECK(out == 0);
    CHECK(moo_pow_int(0, INT64_MAX, &out) == MOO_POW_OK);
    CHECK(out == 0);

    // a nonzero base never divides by zero, however small the result: (2^-16) ^ -1 = 2^16 and 2 ^ -17 = 2^-17,
    // a tie that rounds to 0, the even neighbour
    CHECK(moo_pow_int(1, -1, &out) == MOO_POW_OK);
    CHECK(out == MOO_NUMBER_ONE * 65536);
    CHECK(moo_pow_int(2 * 65536, -16, &out) == MOO_POW_OK);
    CHECK(out == 1);
    CHECK(moo_pow_int(2 * 65536, -17, &out) == MOO_POW_OK);
    CHECK(out == 0);

    // saturation is the rounded value beyond the domain, to the edge of the result's sign (docs/DESIGN.md,
    // "Overflow"); the release value and the checked build's "number overflow in '^'" both come from this status
    CHECK(moo_pow_int(2 * 65536, 46, &out) == MOO_POW_OK);
    CHECK(out == int64_t(1) << 62);
    CHECK(moo_pow_int(2 * 65536, 47, &out) == MOO_POW_SATURATED);
    CHECK(out == MOO_NUMBER_MAX);
    CHECK(moo_pow_int(-2 * 65536, 47, &out) == MOO_POW_SATURATED);
    CHECK(out == MOO_NUMBER_MIN);
    CHECK(moo_pow_int(-2 * 65536, 48, &out) == MOO_POW_SATURATED);
    CHECK(out == MOO_NUMBER_MAX);
    CHECK(moo_pow_int(MOO_NUMBER_MAX, 2, &out) == MOO_POW_SATURATED);
    CHECK(moo_pow_int(1, -4, &out) == MOO_POW_SATURATED); // (2^-16) ^ -4 = 2^64

    // an exact edge is not saturation, as MAX * 1 is not
    CHECK(moo_pow_int(MOO_NUMBER_MAX, 1, &out) == MOO_POW_OK);
    CHECK(out == MOO_NUMBER_MAX);
    CHECK(moo_pow_int(MOO_NUMBER_MIN, 1, &out) == MOO_POW_OK);
    CHECK(out == MOO_NUMBER_MIN);
}

TEST_CASE("PowIntRoundsOnceTiesToEven")
{
    fixed_t out = 0;

    // 1.5 ^ 17 = 3^17 / 2 exactly, a tie on the interval path: 64570081.5 raw rounds to the even 64570082
    CHECK(moo_pow_int(3 * 32768, 17, &out) == MOO_POW_OK);
    CHECK(out == 64570082);
    // 0.5 ^ 17 = 2^-17: half a raw unit, the tie to 0
    CHECK(moo_pow_int(32768, 17, &out) == MOO_POW_OK);
    CHECK(out == 0);
    CHECK(moo_pow_int(32768, 16, &out) == MOO_POW_OK);
    CHECK(out == 1);

    // a negative exponent is the reciprocal of the exact power, rounded once: 3 ^ -1 = 21845.33 raw, and
    // (1/3 rounded) ^ 2 would differ from 3 ^ -2 = 7281.78 raw, which rounds to 7282
    CHECK(moo_pow_int(3 * 65536, -1, &out) == MOO_POW_OK);
    CHECK(out == 21845);
    CHECK(moo_pow_int(3 * 65536, -2, &out) == MOO_POW_OK);
    CHECK(out == 7282);
    // x ^ -1 rounds half to even, where 1 / x (fixDiv) truncates: the two differ when the remainder is over half
    CHECK(moo_pow_int(2 * 65536, -1, &out) == MOO_POW_OK);
    CHECK(out == 32768);
    CHECK(moo_pow_int(43691, -1, &out) == MOO_POW_OK); // 2^32 / 43691 = 98303.25..., to 98303
    CHECK(out == 98303);
    CHECK(moo_pow_int(65536 * 7, -1, &out) == MOO_POW_OK); // 9362.2857 raw: fixDiv gives 9362 too
    CHECK(out == 9362);
    CHECK(moo_pow_int(65536 * 6, -1, &out) == MOO_POW_OK); // 10922.67 raw: rounds to 10923, where fixDiv gives 10922
    CHECK(out == 10923);
}

namespace
{

// one walk away from 1 at exponent n: base 2^16 + dir, + 2 dir, ... until the result saturates or rounds to 0,
// counting every call whose two bounds disagree
void walk(int64_t n, int64_t dir, uint64_t* visited, uint64_t* uncertain, int64_t* firstBase, int64_t* firstExponent)
{
    for (int64_t a = 65536 + dir; a > 0; a += dir)
    {
        fixed_t out = 0;
        bool certain = false;
        int status = moo_pow_int_certain(a, n, &out, &certain);
        (*visited)++;
        if (!certain)
        {
            if (*uncertain == 0)
            {
                *firstBase = a;
                *firstExponent = n;
            }
            (*uncertain)++;
        }
        if (status == MOO_POW_SATURATED || out == 0)
            return;
    }
}

struct WalkTotals
{
    uint64_t visited = 0;
    uint64_t uncertain = 0;
    int64_t firstBase = 0;
    int64_t firstExponent = 0;
    int64_t lastLive = 0; // the largest |n| at which some walk visited more than its first base
};

// every walk for |n| = from, from + step, ... up to the |n| at which all four walks stop at once (both signs of
// the exponent, bases above and below 1)
WalkTotals walks(int64_t from, int64_t step)
{
    WalkTotals t;
    int quiet = 0;
    for (int64_t n = from; quiet < 64; n += step)
    {
        uint64_t before = t.visited;
        walk(n, 1, &t.visited, &t.uncertain, &t.firstBase, &t.firstExponent);
        walk(n, -1, &t.visited, &t.uncertain, &t.firstBase, &t.firstExponent);
        walk(-n, 1, &t.visited, &t.uncertain, &t.firstBase, &t.firstExponent);
        walk(-n, -1, &t.visited, &t.uncertain, &t.firstBase, &t.firstExponent);
        if (t.visited - before > 4)
        {
            t.lastLive = n;
            quiet = 0;
        }
        else
            quiet++;
    }
    return t;
}

} // namespace

TEST_CASE("PowIntCertainSampled")
{
    // past the walks (docs/history/layer4/moo-l4-pow-integer.md, "Exponents past the walks are decided"): the nearest
    // bases each side of 1 at exponents 2^22 .. 2^62 and their neighbours, both signs, are decided at the edge
    for (int k = 22; k <= 62; ++k)
        for (int64_t j = -1; j <= 1; ++j)
            for (int64_t a : {int64_t(65535), int64_t(65537), int64_t(65534), int64_t(65538)})
                for (int64_t sign : {int64_t(1), int64_t(-1)})
                {
                    int64_t n = sign * ((int64_t(1) << k) + j);
                    fixed_t out = 7;
                    bool certain = false;
                    int status = moo_pow_int_certain(a, n, &out, &certain);
                    CHECK(certain);
                    CHECK((status == MOO_POW_SATURATED || (status == MOO_POW_OK && out == 0)));
                    CHECK(status == (((a > 65536) == (n > 0)) ? MOO_POW_SATURATED : MOO_POW_OK));
                }

    // every exponent 14..20 in full, above 1 for n > 0 and below 1 for n < 0 (the widest walks)
    for (int64_t n = MOO_POW_EXACT_MAX + 1; n <= 20; ++n)
    {
        uint64_t visited = 0, uncertain = 0;
        int64_t fb = 0, fe = 0;
        walk(n, 1, &visited, &uncertain, &fb, &fe);
        walk(-n, -1, &visited, &uncertain, &fb, &fe);
        CHECK(uncertain == 0);
    }
}

// The proof that the interval path always decides (docs/history/layer4/moo-l4-pow-integer.md, "Why one rounding is
// exact"): for every |n| > MOO_POW_EXACT_MAX, every base whose result neither saturates nor rounds to 0 is
// visited, and the two bounds round to the same value on every one. The whole proof is about 7.7e7 calls (25 s of
// CPU), so by default the case walks one exponent in 4096 from 14 out past the last live one (0.3 s); MOO_POW_PROOF=1
// runs every exponent on 16 threads (as MOO_ORACLE_N and MOO_FORMAT_N widen their cases):
//   MOO_POW_PROOF=1 build/Moo.Test -ts=pow -tc=PowIntCertainEverywhere
TEST_CASE("PowIntCertainEverywhere")
{
    const char* proof = getenv("MOO_POW_PROOF");
    const bool full = proof && proof[0] == '1' && proof[1] == 0;
    const int threads = full ? 16 : 1;
    const int64_t step = full ? 16 : 4096;
    std::vector<WalkTotals> totals(threads);
    std::vector<std::thread> pool;
    for (int k = 0; k < threads; ++k)
        pool.emplace_back([k, step, &totals]() {
            totals[k] = walks(MOO_POW_EXACT_MAX + 1 + k, step);
        });
    for (std::thread& t : pool)
        t.join();

    WalkTotals all;
    for (const WalkTotals& t : totals)
    {
        all.visited += t.visited;
        all.uncertain += t.uncertain;
        if (t.lastLive > all.lastLive)
            all.lastLive = t.lastLive;
        if (t.uncertain && !all.firstBase)
        {
            all.firstBase = t.firstBase;
            all.firstExponent = t.firstExponent;
        }
    }
    MESSAGE(std::string(full ? "full proof: " : "sampled: ") << "visited " << all.visited << " calls, " << all.uncertain << " uncertain (first: base " << all.firstBase << " exponent "
                       << all.firstExponent << "); last exponent with a live base " << all.lastLive);
    CHECK(all.uncertain == 0);
    CHECK(all.lastLive > 2000000);
}

TEST_CASE("PowIntFrozenHash")
{
    // the same bits on every compiler and platform: an FNV-1a hash of (status, result) over the exhaustive small
    // bases and the seeded cases, pinned. gcc and clang builds both pass this constant.
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i)
        {
            h ^= (v >> (8 * i)) & 0xff;
            h *= 0x100000001b3ull;
        }
    };
    fixed_t out = 0;
    for (int64_t b = -4096; b <= 4096; ++b)
        for (int64_t n = -8; n <= 8; ++n)
        {
            mix(uint64_t(moo_pow_int(b, n, &out)));
            mix(uint64_t(out));
        }
    Seeded rng{0x686173680ull};
    for (int k = 0; k < 65536; ++k)
    {
        uint64_t m = rng.next() % 2048;
        int64_t n = (rng.next() & 1) ? -int64_t(m) : int64_t(m);
        int64_t base = (rng.next() & 3) == 0 ? rng.anySize() : rng.nearOne(m);
        mix(uint64_t(moo_pow_int(base, n, &out)));
        mix(uint64_t(out));
    }
    MESSAGE("moo_pow_int hash " << h);
    CHECK(h == 11023186236270382986ull);
}

} // namespace pow_integer

// The compiler's constant folding of `^` with an integer exponent (docs/history/layer4/moo-l4-pow-integer-fold.md;
// docs/DESIGN.md "Layer 3 as built", "The compiler"; "math library", "pow and ^"): ConstantFolding.cpp calls
// moo_pow_int (Common/include/moo_pow.h), so a folded constant is bit-identical to the run time's result. A
// fractional exponent stays unfolded, and 0 ^ negative is never folded because the run time raises
// "division by zero".
namespace pow_fold
{

namespace
{

std::string compileDump(const char* source)
{
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code | Luau::BytecodeBuilder::Dump_Constants);
    Luau::compileOrThrow(bcb, source);
    return bcb.dumpFunction(0);
}

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

// The dump prints an integer-valued number in the int16 range as LOADN's scaled immediate, and any other number as
// moo_numtostr's shortest decimal (BytecodeBuilder::dumpConstant, docs/DESIGN.md "Number formatting"). Either text
// reads back to the raw value, so the dump holds the folded raw when it holds that text.
bool dumpHoldsRaw(const std::string& dump, fixed_t raw)
{
    int64_t i = 0;
    if (moo_numisinteger(raw) && (i = moo_numtrunc(raw)) >= -32768 && i <= 32767)
        return contains(dump, ("LOADN R0 " + std::to_string(i)).c_str());

    char buffer[40];
    char* end = moo_numtostr(buffer, raw);
    return contains(dump, ("[" + std::string(buffer, end) + "]").c_str());
}

// Compiles and runs source; returns the result's raw Q48.16 value as a decimal string, or "error: ...".
std::string runRaw(const char* source)
{
    std::string bytecode = Luau::compile(source);

    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    if (luau_load(L, "=moo", bytecode.data(), bytecode.size(), 0) != 0)
    {
        lua_close(L);
        return "error: load";
    }

    std::string result;
    if (lua_pcall(L, 0, 1, 0) != 0)
        result = std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    else
        result = std::to_string(static_cast<long long>(lua_tonumber(L, -1))); // the raw Q48.16 value

    lua_close(L);
    return result;
}

} // namespace

// DESIGN.md "Layer 3 as built", "The compiler": `^` with an integer exponent folds through moo_pow_int, and
// the bytecode holds that raw constant (no POWK), equal to the run time's result.
TEST_CASE("PowIntFoldsToPowInt")
{
    const struct
    {
        const char* source;
        fixed_t base;
        int64_t exponent;
    } cases[] = {
        {"return 3 ^ 4", 3 * MOO_NUMBER_ONE, 4},
        {"return 2 ^ -3", 2 * MOO_NUMBER_ONE, -3},
        {"return 1.5 ^ 7", 98304, 7},
        {"return 10 ^ 2", 10 * MOO_NUMBER_ONE, 2},
    };

    for (const auto& c : cases)
    {
        fixed_t expected = 0;
        REQUIRE_MESSAGE(moo_pow_int(c.base, c.exponent, &expected) == MOO_POW_OK, c.source);

        std::string raw = std::to_string(static_cast<long long>(expected));
        std::string dump = compileDump(c.source);
        CHECK_MESSAGE(!contains(dump, "POWK"), c.source << " was not folded:\n" << dump);
        CHECK_MESSAGE(dumpHoldsRaw(dump, expected), c.source << " bytecode does not hold raw " << raw << ":\n" << dump);
        CHECK_MESSAGE(runRaw(c.source) == raw, c.source << " is not moo_pow_int's raw result " << raw << ":\n" << dump);
    }
}

// DESIGN.md "Division by zero" and "Layer 3 as built", "The compiler": 0 ^ negative stays unfolded, and a
// fractional exponent stays unfolded, including one that raises, because neither is moo_pow_int's MOO_POW_OK.
TEST_CASE("PowIntFoldLeavesDivisionByZeroAndFractionalUnfolded")
{
    std::string zero = compileDump("return 0 ^ -1");
    CHECK_MESSAGE(contains(zero, "POWK"), "0 ^ -1 must not fold:\n" << zero);

    std::string fractional = compileDump("return 2 ^ 0.5");
    CHECK_MESSAGE(contains(fractional, "POWK"), "2 ^ 0.5 must not fold:\n" << fractional);

    std::string negative = compileDump("return (-4) ^ 0.5");
    CHECK_MESSAGE(contains(negative, "POWK"), "(-4) ^ 0.5 must not fold:\n" << negative);

    std::string saturating = compileDump("return 2 ^ 47.5");
    CHECK_MESSAGE(contains(saturating, "POWK"), "2 ^ 47.5 must not fold:\n" << saturating);
}

// DESIGN.md "Overflow" and "Layer 3 as built", "The compiler": a power that saturates (moo_pow_int returns
// MOO_POW_SATURATED) is never folded, in release or checked builds, so the run time's saturating value and its
// "number overflow in '^'" error in a checked build are unchanged.
TEST_CASE("PowIntFoldLeavesSaturationUnfolded")
{
    std::string saturating = compileDump("return 10 ^ 100000");
    CHECK_MESSAGE(contains(saturating, "POWK"), "10 ^ 100000 must not fold:\n" << saturating);
}

} // namespace pow_fold

// Layer 4 integer-exponent pow, math library side (docs/DESIGN.md "math library", "pow and ^"): math.pow
// and its fastcall call moo_pow_int (Common/include/moo_pow.h) when the exponent is integer-valued, so
// the library, the fastcall and the core give the same bits; moo: a fractional exponent uses fixed's
// fixPow, with a named error for a negative base. The library path is exercised through a local alias, which the
// compiler does not turn into a fastcall (the same trick tests/moo/math.test.cpp uses).
namespace pow_library
{

namespace
{

// deterministic generator for the seeded pairs
struct SplitMix
{
    uint64_t s;
    uint64_t next()
    {
        s += 0x9e3779b97f4a7c15ull;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
};

// The raw result of math.pow(base, exponent), or "error: " and the message. With fastcall true the
// call is the direct builtin call the compiler fastcalls; with false a local alias keeps it a call.
std::string runPow(lua_State* L, int64_t base, int64_t exponent, bool fastcall)
{
    std::string args = "moo_test.fromraw('" + std::to_string(base) + "'), moo_test.fromraw('" + std::to_string(exponent) + "')";
    std::string source = fastcall ? "return moo_test.raw(math.pow(" + args + "))"
                                  : "local f = math.pow return moo_test.raw(f(" + args + "))";
    return mooRun(L, source.c_str());
}

// mooRun prefixes a raised error with the chunk and line ("error: moo:1: ..."), so an expected error
// matches on the message.
bool sameResult(const std::string& got, const std::string& want)
{
    if (want.rfind("error: ", 0) == 0)
        return got.rfind("error: ", 0) == 0 && got.find(want.substr(7)) != std::string::npos;
    return got == want;
}

// What the core says math.pow must give: the raw value, or the named error.
std::string expectedPow(int64_t base, int64_t exponent)
{
    fixed_t out = 0;
    int status = moo_pow_int(base, exponent, &out);
    if (status == MOO_POW_DIVISION_BY_ZERO)
        return "error: division by zero";
#if MOO_OVERFLOW_ERRORS
    if (status == MOO_POW_SATURATED)
        return "error: number overflow in '^'";
#endif
    return std::to_string(static_cast<long long>(out));
}

} // namespace

// 2^12 seeded pairs: the library, the fastcall and moo_pow_int agree bit for bit, including saturation
// and 0 ^ negative. The exponents cover the exact path and the interval path, and a quarter of the bases
// sit within 2 of 1, where the interval path has to decide.
TEST_CASE("PowLibraryIntegerMatchesCoreOnSeededPairs")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    const int64_t bigExponents[] = {14, 17, 47, 100, 1000, 1 << 20, -14, -17, -100, -1000};
    SplitMix rng{0x6d6f6f2d706f7721ull};

    for (int i = 0; i < 4096; ++i)
    {
        int64_t base;
        if ((i & 3) == 0)
            base = static_cast<int64_t>(rng.next() % 131073) - 65536; // near 1
        else
        {
            base = static_cast<int64_t>(rng.next());
            if (base == INT64_MIN) // outside the number domain
                base = 0;
        }

        int64_t n = (i % 13) == 0 ? bigExponents[(rng.next() >> 1) % 10] : static_cast<int64_t>(rng.next() % 129) - 64;
        int64_t exponent = n * MOO_NUMBER_ONE;

        std::string want = expectedPow(base, n);
        std::string gotLibrary = runPow(L, base, exponent, false);
        std::string gotFastcall = runPow(L, base, exponent, true);

        CHECK_MESSAGE(sameResult(gotLibrary, want), "library " << base << " ^ " << n << ": " << gotLibrary << " want " << want);
        CHECK_MESSAGE(sameResult(gotFastcall, want), "fastcall " << base << " ^ " << n << ": " << gotFastcall << " want " << want);
    }

    lua_close(L);
}

// moo: a non-integer exponent takes fixPow; math.pow and its fastcall both raise the named
// negative-base error instead of accepting a host libm NaN.
TEST_CASE("PowLibraryFractionalUsesFixed")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    const int64_t bases[] = {0, 65536, -65536, 12345, 3 * 65536, MOO_NUMBER_MAX, MOO_NUMBER_MIN};
    const int64_t exponents[] = {0x8000, 0x18000, -0x8000, 0x1234, 0x12345};

    for (int64_t base : bases)
        for (int64_t exponent : exponents)
        {
            REQUIRE(!moo_numisinteger(exponent));
            std::string want = powExpectedOf(base, exponent);
            if (base < 0)
                CHECK(want == "error: moo:1: pow of a negative number to a fractional power");

            CHECK_MESSAGE(sameResult(runPow(L, base, exponent, false), want), "library " << base << " ^ " << exponent);
            CHECK_MESSAGE(sameResult(runPow(L, base, exponent, true), want), "fastcall " << base << " ^ " << exponent);
        }

    lua_close(L);
}

// The edges the note names: 0 ^ 0, 0 ^ positive, 0 ^ negative, the exact edge, saturation with its
// signs, and the integer-exponent answers that differ from fixed's truncating division.
TEST_CASE("PowLibraryIntegerEdges")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    CHECK(runPow(L, 0, 0, false) == std::to_string(static_cast<long long>(MOO_NUMBER_ONE)));
    CHECK(runPow(L, 0, 0, true) == std::to_string(static_cast<long long>(MOO_NUMBER_ONE)));
    CHECK(runPow(L, 0, 3 * MOO_NUMBER_ONE, false) == "0");
    CHECK(runPow(L, 0, 3 * MOO_NUMBER_ONE, true) == "0");

    CHECK(sameResult(runPow(L, 0, -MOO_NUMBER_ONE, false), "error: division by zero"));
    CHECK(sameResult(runPow(L, 0, -MOO_NUMBER_ONE, true), "error: division by zero"));

    // MOO_NUMBER_MAX ^ 1 is exact, not saturation
    CHECK(runPow(L, MOO_NUMBER_MAX, MOO_NUMBER_ONE, false) == std::to_string(static_cast<long long>(MOO_NUMBER_MAX)));
    CHECK(runPow(L, MOO_NUMBER_MAX, MOO_NUMBER_ONE, true) == std::to_string(static_cast<long long>(MOO_NUMBER_MAX)));

    // 6 ^ -1 is 10923 raw, the rounding of 65536 / 6, where 1 / 6 (fixDiv) truncates to 10922.
    CHECK(runPow(L, 6 * MOO_NUMBER_ONE, -MOO_NUMBER_ONE, false) == "10923");
    CHECK(runPow(L, 6 * MOO_NUMBER_ONE, -MOO_NUMBER_ONE, true) == "10923");
    // 3 ^ -2 is 7282 raw, the rounding of 65536 / 9.
    CHECK(runPow(L, 3 * MOO_NUMBER_ONE, -2 * MOO_NUMBER_ONE, false) == "7282");
    CHECK(runPow(L, 3 * MOO_NUMBER_ONE, -2 * MOO_NUMBER_ONE, true) == "7282");

#if MOO_OVERFLOW_ERRORS
    CHECK(sameResult(runPow(L, 2 * MOO_NUMBER_ONE, 47 * MOO_NUMBER_ONE, false), "error: number overflow in '^'"));
    CHECK(sameResult(runPow(L, 2 * MOO_NUMBER_ONE, 47 * MOO_NUMBER_ONE, true), "error: number overflow in '^'"));
#else
    CHECK(runPow(L, 2 * MOO_NUMBER_ONE, 47 * MOO_NUMBER_ONE, false) == std::to_string(static_cast<long long>(MOO_NUMBER_MAX)));
    CHECK(runPow(L, 2 * MOO_NUMBER_ONE, 47 * MOO_NUMBER_ONE, true) == std::to_string(static_cast<long long>(MOO_NUMBER_MAX)));
    CHECK(runPow(L, -2 * MOO_NUMBER_ONE, 47 * MOO_NUMBER_ONE, false) == std::to_string(static_cast<long long>(MOO_NUMBER_MIN)));
    CHECK(runPow(L, -2 * MOO_NUMBER_ONE, 47 * MOO_NUMBER_ONE, true) == std::to_string(static_cast<long long>(MOO_NUMBER_MIN)));
#endif

    lua_close(L);
}

} // namespace pow_library

// Layer 4 integer-exponent pow in the VM (docs/DESIGN.md "math library", "pow and ^"): luai_numpow calls
// moo_pow_int for an integer-valued exponent, and moo: a fractional exponent uses fixed's fixPow. The
// scripts run through the VM (`^` and the constant-exponent opcode); the reference is moo_pow_int called
// directly and fixPow itself.
namespace pow_vm
{

namespace
{

uint64_t mix(uint64_t& s)
{
    s += 0x9e3779b97f4a7c15ull;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// the two-operand path: a local function keeps both operands out of constant folding
std::string runPow(lua_State* L, fixed_t a, fixed_t b)
{
    return mooRun(L, ("local function f(a, b) return a ^ b end return moo_test.raw(f(moo_test.fromraw('" + std::to_string(a) +
                            "'), moo_test.fromraw('" + std::to_string(b) + "')))").c_str());
}

// the constant-exponent path (POWK: the exponent is a literal)
std::string runPowK(lua_State* L, fixed_t a, int64_t n)
{
    return mooRun(L, ("local function f(a) return a ^ " + std::to_string(n) +
                            " end return moo_test.raw(f(moo_test.fromraw('" + std::to_string(a) + "')))").c_str());
}

} // namespace

// 2^12 seeded (base, integer exponent) pairs: the VM's bits are moo_pow_int's bits, and where its status is an
// error in this build the VM raises it (powExpected).
TEST_CASE("PowIntVmMatchesMooPowInt")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    uint64_t seed = 0x6d6f6f2d70347731ull;
    for (int i = 0; i < 4096; ++i)
    {
        fixed_t a = fixed_t(mix(seed));
        int64_t n = int64_t(mix(seed) % 4001) - 2000;
        fixed_t b = fixed_t(n) * MOO_NUMBER_ONE;

        std::string want = powExpectedOf(a, b);
        std::string got = runPow(L, a, b);
        CAPTURE(a);
        CAPTURE(n);
        CHECK_MESSAGE(got == want, a << " ^ " << n << ": vm " << got << " want " << want);

        got = runPowK(L, a, n);
        CHECK_MESSAGE(got == want, "powk " << a << " ^ " << n << ": vm " << got << " want " << want);
    }

    lua_close(L);
}

// moo: the opcode's fractional exponent uses fixPow: 0 for a zero base, and a negative base raises the named error.
TEST_CASE("PowIntVmFractionalUsesFixed")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    fixed_t bases[] = {0, MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE, 3 * MOO_NUMBER_ONE, 10 * MOO_NUMBER_ONE, 1234567, -MOO_NUMBER_ONE, -5 * MOO_NUMBER_ONE};
    fixed_t exponents[] = {MOO_NUMBER_ONE / 2, MOO_NUMBER_ONE + MOO_NUMBER_ONE / 2, -MOO_NUMBER_ONE / 2, MOO_NUMBER_ONE / 4,
        3 * MOO_NUMBER_ONE / 2, -(MOO_NUMBER_ONE + MOO_NUMBER_ONE / 2), 7 * MOO_NUMBER_ONE / 8};

    for (fixed_t a : bases)
        for (fixed_t b : exponents)
        {
            std::string want = powExpectedOf(a, b);
            std::string got = runPow(L, a, b);
            CAPTURE(a);
            CAPTURE(b);
            CHECK_MESSAGE(got == want, "fixed: vm " << got << " want " << want);
            if (a < 0)
                CHECK(got == "error: moo:1: pow of a negative number to a fractional power");
        }

    lua_close(L);
}

// 0 ^ negative is "division by zero" in every build, as for x / 0 and math.pow: through LOP_POW, LOP_POWK and a
// coerced operand (luaV_doarithimpl).
TEST_CASE("PowIntVmZeroToNegativeIsDivisionByZero")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    const std::string error = "error: moo:1: division by zero";
    for (fixed_t n : {fixed_t(-MOO_NUMBER_ONE), fixed_t(-2 * MOO_NUMBER_ONE), fixed_t(-1000 * MOO_NUMBER_ONE)})
    {
        CAPTURE(n);
        CHECK(mooRun(L, ("local function f(a, b) return a ^ b end return moo_test.raw(f(moo_test.fromraw('0'), moo_test.fromraw('" +
                             std::to_string(n) + "')))").c_str()) == error);
        CHECK(mooRun(L, ("local function f(a) return a ^ " + std::to_string(n / MOO_NUMBER_ONE) +
                             " end return moo_test.raw(f(moo_test.fromraw('0')))").c_str()) == error);
        CHECK(mooRun(L, ("local function f(a, b) return a ^ b end return moo_test.raw(f('0', moo_test.fromraw('" + std::to_string(n) +
                             "')))").c_str()) == error);
    }

    CHECK(mooRun(L, "return moo_test.raw(0 ^ 0)") == "65536");
    CHECK(mooRun(L, "return moo_test.raw(0 ^ 2)") == "0");

    lua_close(L);
}

} // namespace pow_vm

// Layer 4's fractional-exponent pow (docs/DESIGN.md "math library", "pow and ^"): an integer-valued exponent
// is moo_pow_int (moo-l4-pow-integer and its VM/math cards), any other exponent is fixPow = fixExp2(exponent *
// fixLog2(base)). A negative base with a fractional exponent is the named error "pow of a negative number to a
// fractional power", from the `^` opcode, math.pow and its fastcall alike; 0 ^ positive fraction = 0.
namespace pow_fractional
{

namespace
{

uint64_t mix(uint64_t& s)
{
    s += 0x9e3779b97f4a7c15ull;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// the two-operand path: a local function keeps both operands out of constant folding
std::string runPow(lua_State* L, int64_t a, int64_t b)
{
    return mooRun(L, ("local function f(a, b) return a ^ b end return moo_test.raw(f(moo_test.fromraw('" + std::to_string(a) +
                            "'), moo_test.fromraw('" + std::to_string(b) + "')))").c_str());
}

std::string runMathPow(lua_State* L, int64_t a, int64_t b)
{
    return mooRun(L, ("return moo_test.raw(math.pow(moo_test.fromraw('" + std::to_string(a) + "'), moo_test.fromraw('" +
                            std::to_string(b) + "')))").c_str());
}

// a fractional exponent: not an integer-valued Q48.16 value
bool fractional(int64_t b)
{
    return (b & 0xffff) != 0;
}

} // namespace

// 2^12 seeded (base, fractional exponent) pairs through the `^` opcode, matched bit for bit to fixPow; a power
// that saturates raises in a checked build (powExpected). A negative base raises the named error, as math.pow.
TEST_CASE("PowFractionalVmMatchesFixPow")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    uint64_t seed = 0x6d6f6f2d66726331ull;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t a = int64_t(mix(seed));
        int64_t b = int64_t(mix(seed));
        if (!fractional(b))
            b |= 1;
        if (a <= 0)
            a = -a + 1;

        std::string want = powExpectedOf(a, b);
        std::string got = runPow(L, a, b);
        CAPTURE(a);
        CAPTURE(b);
        CHECK_MESSAGE(got == want, "vm " << a << " ^ " << b << ": " << got << " want " << want);
    }

    // 0 ^ positive fraction = 0, on both the opcode and math.pow paths
    CHECK(runPow(L, 0, 1) == "0");
    CHECK(runPow(L, 0, MOO_NUMBER_ONE / 2) == "0");
    CHECK(runMathPow(L, 0, MOO_NUMBER_ONE / 2) == "0");
    CHECK(runMathPow(L, 0, MOO_NUMBER_ONE + MOO_NUMBER_ONE / 2) == "0");

    // a negative base with a fractional exponent: the opcode raises math.pow's named error
    CHECK(runPow(L, -MOO_NUMBER_ONE, MOO_NUMBER_ONE / 2) == "error: moo:1: pow of a negative number to a fractional power");
    CHECK(runPow(L, -4 * MOO_NUMBER_ONE, MOO_NUMBER_ONE / 2) == "error: moo:1: pow of a negative number to a fractional power");

    lua_close(L);
}

// math.pow, its fastcall and the ^ operator raise the named error for a negative base with a fractional exponent.
// The fastcall returns -1 and the library raises; the operator raises in luaV_doarithimpl.
TEST_CASE("PowFractionalNegativeBaseError")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    auto err = [&](const char* src) {
        return mooRun(L, src) == "error: moo:1: pow of a negative number to a fractional power";
    };
    CHECK(err("return math.pow(-1, 0.5)"));
    CHECK(err("return math.pow(-4, 0.5)"));
    CHECK(err("return math.pow(-65536, -1.5)"));
    CHECK(err("return math.pow(-2, 1.25)"));
    CHECK(err("return (-1) ^ 0.5"));
    CHECK(err("return (-4) ^ 0.5"));
    CHECK(err("return (-65536) ^ -1.5"));
    CHECK(err("local t = {-2, 1.25} return t[1] ^ t[2]"));

    // an integer exponent is still moo_pow_int, no error
    CHECK(mooRun(L, "return moo_test.raw(math.pow(moo_test.fromraw('-131072'), moo_test.fromraw('196608')))") == "-524288");
    CHECK(mooRun(L, "return moo_test.raw(math.pow(moo_test.fromraw('-131072'), moo_test.fromraw('131072')))") == "262144");

    lua_close(L);
}

// math.pow's fractional path matches fixPow for positive bases, including 0 ^ positive fraction = 0.
TEST_CASE("PowFractionalMathPowMatchesFixPow")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    uint64_t seed = 0x6d6f6f2d66726332ull;
    for (int i = 0; i < 2048; ++i)
    {
        int64_t a = int64_t(mix(seed) % (8 * MOO_NUMBER_ONE)) + 1;
        int64_t b = int64_t(mix(seed) % (8 * MOO_NUMBER_ONE));
        if (!fractional(b))
            b |= 1;
        int64_t ref = fixPow(a, b);
        std::string got = runMathPow(L, a, b);
        CAPTURE(a);
        CAPTURE(b);
        CHECK_MESSAGE(got == std::to_string(ref), "math.pow " << a << " ^ " << b << ": " << got << " ref " << ref);
    }

    lua_close(L);
}

// The relative error of fixPow as a function of the exponent (docs/DESIGN.md "pow and ^": the error is relative
// and grows with the exponent), measured against an exact reference on 2^16 seeded points. This only prints the
// table; the recorded maximum is the layer-4 bound (docs/history/layer4/moo-l4-pow-fractional.md).
TEST_CASE("PowFractionalRelativeError")
{
    std::map<int64_t, double> worstRelative;
    std::map<int64_t, int64_t> worstAbs;
    std::map<int64_t, uint64_t> count;

    const int64_t one = MOO_NUMBER_ONE;
    uint64_t seed = 0x6d6f6f2d66726333ull;
    for (int i = 0; i < 65536; ++i)
    {
        int64_t a = int64_t(mix(seed) % (16 * one)) + 1;
        int64_t b = int64_t(mix(seed) % (16 * one));
        if (!fractional(b))
            b |= 1;

        double baseD = double(a) / double(one);
        double expD = double(b) / double(one);
        double exact = pow(baseD, expD);
        int64_t ideal = mooHostNumber(exact);
        int64_t got = fixPow(a, b);
        if (ideal == 0)
            continue;

        int64_t abserr = got > ideal ? got - ideal : ideal - got;
        double rel = double(abserr) / double(ideal < 0 ? -ideal : ideal);

        int64_t bucket = b / (one / 2); // exponent in half-integer buckets
        if (bucket < 0)
            bucket = -bucket;
        count[bucket]++;
        if (abserr > worstAbs[bucket])
        {
            worstAbs[bucket] = abserr;
            worstRelative[bucket] = rel;
        }
    }

    printf("moo-pow-fractional relative error by |exponent| bucket (raw units, and relative to |ideal|):\n");
    for (const auto& [bucket, n] : count)
        printf("  |exp| ~ %.2f: n=%llu worst_abs=%lld worst_rel=%.6g\n", double(bucket) / 2.0, (unsigned long long)n,
            (long long)worstAbs[bucket], worstRelative[bucket]);
}

} // namespace pow_fractional

// The ^ operator is math.pow (docs/DESIGN.md "pow and ^"): one definition, one result, one set of errors. Every
// case runs through the operator with registers, the operator with a constant exponent (POWK), the operator with
// both operands constant (folded when moo_pow_int is MOO_POW_OK, left to run time otherwise), math.pow's fastcall
// and the library function, at -O0, -O1 and -O2, and each gives powExpected for the build it is in.
namespace pow_operator
{

namespace
{

const fixed_t kOne = MOO_NUMBER_ONE;

const fixed_t kBases[] = {0, kOne, -kOne, 2 * kOne, -2 * kOne, 6 * kOne, 10 * kOne, kOne + kOne / 2, kOne / 2, 1, MOO_NUMBER_MAX,
    MOO_NUMBER_MIN};

const fixed_t kExponents[] = {0, kOne, -kOne, 3 * kOne, -3 * kOne, 46 * kOne, 47 * kOne, 100000 * kOne, -1000 * kOne, kOne / 2,
    -kOne / 2, 3 * kOne / 2, 47 * kOne + kOne / 2};

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

// a literal that reads back to raw, in parentheses when negative
std::string literal(fixed_t raw)
{
    char buffer[40];
    char* end = moo_numtostr(buffer, raw);
    std::string text(buffer, end);
    return raw < 0 ? "(" + text + ")" : text;
}

Luau::CompileOptions at(int level)
{
    Luau::CompileOptions options;
    options.optimizationLevel = level;
    return options;
}

std::string run(const std::string& source, int level)
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    std::string bytecode = Luau::compile(source, at(level));
    int status = luau_load(L, "=moo", bytecode.data(), bytecode.size(), 0);
    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);

    std::string result = status != 0 ? std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?")
                                     : std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    lua_close(L);
    return result;
}

std::string dump(const std::string& source, int level)
{
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code);
    Luau::compileOrThrow(bcb, source, at(level));
    return bcb.dumpFunction(0);
}

// The operator as it was before it raised: moo_pow_int's *out, or fixPow's value, and no error.
std::string oldOperator(fixed_t base, fixed_t exponent)
{
    fixed_t out = 0;
    if (moo_numisinteger(exponent))
        moo_pow_int(base, exponent >> MOO_NUMBER_FRACTION_BITS, &out);
    else
        out = fixPow(base, exponent);
    return std::to_string(static_cast<long long>(out));
}

} // namespace

TEST_CASE("PowOperatorMatchesMathPow")
{
    int errors = 0;
    for (fixed_t base : kBases)
        for (fixed_t exponent : kExponents)
        {
            fixed_t ref = 0;
            int status = powReference(base, exponent, &ref);
            std::string want = powExpected(status, ref);
            errors += want.rfind("error: ", 0) == 0;

            std::string b = literal(base), e = literal(exponent);
            const std::string sources[] = {
                "local t = {" + b + ", " + e + "} return moo_test.raw(t[1] ^ t[2])",
                "local t = {" + b + "} return moo_test.raw(t[1] ^ " + e + ")",
                "return moo_test.raw(" + b + " ^ " + e + ")",
                "local t = {" + b + ", " + e + "} return moo_test.raw(math.pow(t[1], t[2]))",
                "local p = rawget(math, 'pow') local t = {" + b + ", " + e + "} return moo_test.raw(p(t[1], t[2]))",
            };

            for (int level = 0; level <= 2; ++level)
            {
                CAPTURE(level);
                for (const std::string& source : sources)
                    CHECK_MESSAGE(run(source, level) == want, source << " -O" << level << ": want " << want);

                if (level == 0)
                    continue;

                // the constant path folds exactly when moo_pow_int is MOO_POW_OK, so the fold never hides an error
                bool folds = moo_numisinteger(exponent) && status == MOO_POW_OK;
                std::string constant = dump("local x = " + b + " ^ " + e + " return x", level);
                CHECK_MESSAGE(contains(constant, "POW") == !folds, b << " ^ " << e << " -O" << level << (folds ? " must" : " must not")
                                                                     << " fold:\n" << constant);

                std::string fastcall = dump(sources[3], level);
                CHECK_MESSAGE(contains(fastcall, "FASTCALL2"), "math.pow is not a fastcall at -O" << level << ":\n" << fastcall);
            }
        }

    // every kind of error is in the table
    CHECK(errors >= 4);
}

// Reversed witness: under the old silent operator (moo_pow_int's *out or fixPow's value, no error) the table above
// goes red. Every case whose expected result is an error differs from the old operator's value, and the table has
// cases for division by zero, a negative base and, in a checked build, saturation.
TEST_CASE("PowOperatorGoesRedWithTheOldSilentRule")
{
    int divisionByZero = 0, negativeBase = 0, saturated = 0;
    for (fixed_t base : kBases)
        for (fixed_t exponent : kExponents)
        {
            fixed_t ref = 0;
            int status = powReference(base, exponent, &ref);
            std::string want = powExpected(status, ref);
            if (want.rfind("error: ", 0) != 0)
            {
                CHECK(oldOperator(base, exponent) == want); // the two rules agree where nothing raises
                continue;
            }

            std::string b = literal(base), e = literal(exponent);
            std::string got = run("local t = {" + b + ", " + e + "} return moo_test.raw(t[1] ^ t[2])", 1);
            CHECK_MESSAGE(got == want, b << " ^ " << e);
            CHECK_MESSAGE(oldOperator(base, exponent) != want, b << " ^ " << e << ": the old rule would pass");

            divisionByZero += status == MOO_POW_DIVISION_BY_ZERO;
            negativeBase += status == MOO_POW_NEGATIVE_BASE;
            saturated += status == MOO_POW_SATURATED;
        }

    CHECK(divisionByZero > 0);
    CHECK(negativeBase > 0);
#if MOO_OVERFLOW_ERRORS
    CHECK(saturated > 0);
#else
    CHECK(saturated == 0);
#endif
}

} // namespace pow_operator

TEST_SUITE_END();
