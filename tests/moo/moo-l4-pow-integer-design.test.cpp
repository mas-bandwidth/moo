// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// One test per file: tests/moo/*.test.cpp is built into Moo.Test by the CMake glob, so a layer 4 card
// adds its cases in its own file and never edits tests/Moo.test.cpp (docs/layer3/README.md).
//
// moo_pow_int (Common/include/moo_pow.h; docs/DESIGN.md, "pow and ^"; docs/layer4/moo-l4-pow-integer.md):
// a Q48.16 base to an integer exponent, the exact power rounded once, half to even, saturating, with
// 0 ^ negative reported as division by zero. Every answer here is checked against an exact rational
// reference written in this file (Ref, unbounded integers in 32-bit limbs, sharing no code with
// moo_pow.h or fixed): the result V = N / D is exact, and the reference proves the returned raw R is
// round-half-even(V) by comparing 2N with (2R - 1) D and (2R + 1) D, and proves saturation by
// 2N >= (2^64 - 1) D (the rounded value is at least 2^63).
#include "moo_pow.h"

#include "doctest.h"

#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <thread>
#include <vector>

TEST_SUITE_BEGIN("moo-l4-pow-integer-design");

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

TEST_CASE("MooPowIntExhaustiveSmallBases")
{
    // every raw base with |base| <= 2^12 and every exponent -8..8 (the card's exhaustive range)
    Checker c;
    for (int64_t b = -4096; b <= 4096; ++b)
        for (int64_t n = -8; n <= 8; ++n)
            c.check(b, n);
    CHECK(c.cases == 8193 * 17);
    CHECK(c.failures == 0);
}

TEST_CASE("MooPowIntExhaustiveNearOne")
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

TEST_CASE("MooPowIntIntegerBases")
{
    // integer bases -64..64 for exponents -24..24: exact integers, exact reciprocals of powers of two, and
    // the edges of the range (2 ^ 46 is in range, 2 ^ 47 saturates)
    Checker c;
    for (int64_t i = -64; i <= 64; ++i)
        for (int64_t n = -24; n <= 24; ++n)
            c.check(i * 65536, n);
    CHECK(c.failures == 0);
}

TEST_CASE("MooPowIntSeeded")
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

TEST_CASE("MooPowIntLargeExponents")
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

TEST_CASE("MooPowIntReportsSaturationAndDivisionByZero")
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

TEST_CASE("MooPowIntRoundsOnceTiesToEven")
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

TEST_CASE("MooPowIntCertainSampled")
{
    // past the walks (docs/layer4/moo-l4-pow-integer.md, "Exponents past the walks are decided"): the nearest
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

// The proof that the interval path always decides (docs/layer4/moo-l4-pow-integer.md, "Why one rounding is
// exact"): for every |n| > MOO_POW_EXACT_MAX, every base whose result neither saturates nor rounds to 0 is
// visited, and the two bounds round to the same value on every one. The whole proof is about 7.7e7 calls (25 s of
// CPU), so by default the case walks one exponent in 4096 from 14 out past the last live one (0.3 s); MOO_POW_PROOF=1
// runs every exponent on 16 threads (as MOO_ORACLE_N and MOO_FORMAT_N widen their cases):
//   MOO_POW_PROOF=1 build/Moo.Test -ts=moo-l4-pow-integer-design -tc=MooPowIntCertainEverywhere
TEST_CASE("MooPowIntCertainEverywhere")
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

TEST_CASE("MooPowIntFrozenHash")
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

TEST_SUITE_END();
