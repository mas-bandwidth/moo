// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Tests of moo's number operations (VM/src/moo_number.h) and the boundary (moo_fromdouble,
// moo_todouble), against independent references: two-word integer arithmetic (MooWide.h), and for the boundary the
// FPU itself (scaling by 2^16 is exact, and nearbyint rounds ties to even in the default mode).
#include "moo.h"
#include "moo_number.h"
#include "moo_parse.h"
#include "moo_format.h"

#include "MooWide.h"

#include "doctest.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

namespace
{

// a deterministic generator for test inputs
struct Lcg
{
    uint64_t s = 0x9e3779b97f4a7c15ull;
    uint64_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return s ^ (s >> 29);
    }
};

int64_t addRef(int64_t a, int64_t b)
{
    return mooClampWide(MooWide::from(a) + MooWide::from(b));
}

int64_t subRef(int64_t a, int64_t b)
{
    return mooClampWide(MooWide::from(a) - MooWide::from(b));
}

int64_t floorDivRef(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    return q;
}

MooWide absWide(MooWide v)
{
    return v.negative() ? -v : v;
}

// fixDiv's contract, checked without a wide division: q truncates (a * 2^16) / b toward zero, saturating
bool divIsTruncation(int64_t a, int64_t b, int64_t q)
{
    MooWide n = absWide(MooWide::from(a).shl(16));
    MooWide ub = absWide(MooWide::from(b));
    MooWide uq = absWide(MooWide::from(q));
    bool neg = (a < 0) != (b < 0);
    if (a != 0 && q != 0 && (q < 0) != neg)
        return false;
    // |q| * |b| <= |n|, unless saturated
    MooWide low = mooMulWide(int64_t(uq.lo), int64_t(ub.lo));
    if (q == INT64_MAX || q == -INT64_MAX)
        return low <= n;
    // |n| < (|q| + 1) * |b|
    MooWide high = low + ub;
    return low <= n && n < high;
}

const int64_t kEdges[] = {0, 1, -1, 2, -2, 65535, 65536, -65536, 65537, 1ll << 31, -(1ll << 31), 1ll << 47, -(1ll << 47), (1ll << 62) + 12345,
    -(1ll << 62), INT64_MAX, -INT64_MAX, INT64_MAX - 1, -INT64_MAX + 1, 3037000499ll, -3037000499ll, 3037000500ll << 8};

} // namespace

TEST_SUITE_BEGIN("moo");

TEST_CASE("NumberAddSubSaturate")
{
    Lcg r;
    for (int i = 0; i < 200000; ++i)
    {
        int64_t a = int64_t(r.next()), b = int64_t(r.next());
        if (a == INT64_MIN || b == INT64_MIN)
            continue;
        if (i % 3 == 0)
            b >>= 20;
        CHECK(moo_numadd(a, b) == addRef(a, b));
        CHECK(moo_numsub(a, b) == subRef(a, b));
    }
    for (int64_t a : kEdges)
        for (int64_t b : kEdges)
        {
            CHECK(moo_numadd(a, b) == addRef(a, b));
            CHECK(moo_numsub(a, b) == subRef(a, b));
        }
    // the domain never reaches INT64_MIN
    CHECK(moo_numadd(-INT64_MAX, -1) == -INT64_MAX);
    CHECK(moo_numunm(-INT64_MAX) == INT64_MAX);
    CHECK(moo_numunm(INT64_MAX) == -INT64_MAX);
}

TEST_CASE("NumberMulDivAreFixed")
{
    // round half up, saturating; truncating divide
    CHECK(moo_nummul(3 * 65536 / 2, 5 * 65536 / 2) == 15 * 65536 / 4);
    CHECK(moo_nummul(1, 32768) == 1);   // 0.5 unit rounds up
    CHECK(moo_nummul(-1, 32768) == 0);  // -0.5 unit rounds up, toward +inf
    CHECK(moo_nummul(INT64_MAX, 2 * 65536) == INT64_MAX);
    CHECK(moo_nummul(INT64_MAX, -2 * 65536) == -INT64_MAX);
    CHECK(moo_numdiv(65536, 3 * 65536) == 21845);
    CHECK(moo_numdiv(-65536, 3 * 65536) == -21845);
    CHECK(moo_numdiv(INT64_MAX, 1) == INT64_MAX);

    Lcg r;
    for (int i = 0; i < 200000; ++i)
    {
        int64_t a = int64_t(r.next()) >> (r.next() % 40), b = int64_t(r.next()) >> (r.next() % 40);
        if (a == INT64_MIN || b == INT64_MIN)
            continue;
        MooWide p = mooMulWide(a, b) + MooWide::from(32768);
        CHECK(moo_nummul(a, b) == mooClampWide(p.shr(16)));
        if (b != 0)
        {
            int64_t q = moo_numdiv(a, b);
            CHECK(divIsTruncation(a, b, q));
            // the reference refuses a quotient one unit away from the truncated one
            if (q != INT64_MAX && q != -INT64_MAX && q != 0)
                CHECK(!divIsTruncation(a, b, q > 0 ? q + 1 : q - 1));
        }
    }
}

TEST_CASE("NumberFloorDivModAreExact")
{
    Lcg r;
    for (int i = 0; i < 200000; ++i)
    {
        int64_t a = int64_t(r.next()) >> (r.next() % 50), b = int64_t(r.next()) >> (r.next() % 60);
        if (a == INT64_MIN || b == INT64_MIN || b == 0)
            continue;
        int64_t q = floorDivRef(a, b);
        CHECK(moo_numidiv(a, b) == mooClampWide(mooMulWide(q, 65536)));
        CHECK(moo_nummod(a, b) == mooClampWide(MooWide::from(a) - mooMulWide(q, b)));
        // the remainder has the sign of the divisor and is smaller than it
        int64_t m = moo_nummod(a, b);
        CHECK((m == 0 || (m < 0) == (b < 0)));
        CHECK((m < 0 ? -m : m) < (b < 0 ? -b : b));
    }
    // small integers agree with floor on doubles
    for (int a = -40; a <= 40; ++a)
        for (int b = -9; b <= 9; ++b)
        {
            if (b == 0)
                continue;
            CHECK(moo_numidiv(int64_t(a) * 65536, int64_t(b) * 65536) == int64_t(floor(double(a) / b)) * 65536);
            CHECK(moo_nummod(int64_t(a) * 65536, int64_t(b) * 65536) == int64_t(a - floor(double(a) / b) * b) * 65536);
        }
    CHECK(moo_numidiv(INT64_MAX, 1) == INT64_MAX); // the quotient is beyond the integer range: saturates
    CHECK(moo_numisinteger(3 * 65536));
    CHECK(!moo_numisinteger(3 * 65536 + 1));
    CHECK(moo_numisinteger(-65536));
}

TEST_CASE("BoundaryRoundTrip")
{
    // every raw value near zero and near one, and random raws below 2^53, survive a round trip
    for (int64_t n = -(1 << 20); n <= (1 << 20); ++n)
    {
        int64_t back = 0;
        REQUIRE(moo_fromdouble(moo_todouble(n), &back) == MOO_NUMBER_OK);
        REQUIRE(back == n);
    }
    Lcg r;
    for (int i = 0; i < 200000; ++i)
    {
        int64_t n = int64_t(r.next() >> 11) - (int64_t(1) << 52);
        int64_t back = 0;
        CHECK(moo_fromdouble(moo_todouble(n), &back) == MOO_NUMBER_OK);
        CHECK(back == n);
        CHECK(moo_todouble(n) == double(n) / 65536.0);
    }
}

TEST_CASE("BoundaryRoundsTiesToEven")
{
    // against the FPU: below 2^37 the product x * 2^16 is exact and nearbyint rounds ties to even
    Lcg r;
    for (int i = 0; i < 400000; ++i)
    {
        uint64_t bits = r.next();
        double x;
        memcpy(&x, &bits, sizeof(x));
        if (!isfinite(x) || fabs(x) >= 137438953472.0)
            continue;
        int64_t got = 0;
        REQUIRE(moo_fromdouble(x, &got) == MOO_NUMBER_OK);
        CHECK(got == int64_t(nearbyint(x * 65536.0)));
    }
    // exact half units
    for (int64_t k = -1000; k <= 1000; ++k)
    {
        int64_t got = 0;
        REQUIRE(moo_fromdouble((double(k) + 0.5) / 65536.0, &got) == MOO_NUMBER_OK);
        int64_t even = (k % 2 == 0) ? k : k + 1;
        CHECK(got == even);
    }
    int64_t v = 7;
    CHECK(moo_fromdouble(-0.0, &v) == MOO_NUMBER_OK);
    CHECK(v == 0);
    CHECK(moo_fromdouble(5e-324, &v) == MOO_NUMBER_OK); // the smallest subnormal
    CHECK(v == 0);
    CHECK(moo_fromdouble(0.1, &v) == MOO_NUMBER_OK);
    CHECK(v == 6554); // 0.1 * 65536 = 6553.6
    CHECK(moo_fromfloat(1.5f, &v) == MOO_NUMBER_OK);
    CHECK(v == 98304);
}

TEST_CASE("BoundaryRefusesByName")
{
    int64_t v = 123;
    CHECK(moo_fromdouble(NAN, &v) == MOO_NUMBER_NAN);
    CHECK(moo_fromdouble(INFINITY, &v) == MOO_NUMBER_INFINITE);
    CHECK(moo_fromdouble(-INFINITY, &v) == MOO_NUMBER_INFINITE);
    CHECK(moo_fromdouble(140737488355328.0, &v) == MOO_NUMBER_OUT_OF_RANGE);  // 2^47
    CHECK(moo_fromdouble(-140737488355328.0, &v) == MOO_NUMBER_OUT_OF_RANGE); // -2^47 is INT64_MIN raw, outside
    CHECK(moo_fromdouble(1e300, &v) == MOO_NUMBER_OUT_OF_RANGE);
    CHECK(v == 123); // untouched on refusal
    // the largest double below 2^47 is in range
    CHECK(moo_fromdouble(nextafter(140737488355328.0, 0.0), &v) == MOO_NUMBER_OK);
    CHECK(v == int64_t(nextafter(140737488355328.0, 0.0) * 65536.0));
}

namespace
{

// the exact decimal of raw / 2^16: at most 16 fraction digits, no trailing zeros
std::string exactDecimal(int64_t raw, int extraHalf = 0)
{
    // extraHalf adds half a unit: then the denominator is 2^17
    bool neg = raw < 0;
    uint64_t m = neg ? uint64_t(0) - uint64_t(raw) : uint64_t(raw);
    int bits = 16;
    if (extraHalf)
    {
        m = m * 2 + 1;
        bits = 17;
    }
    uint64_t ip = m >> bits;
    uint64_t frac = m & ((uint64_t(1) << bits) - 1);
    std::string out = (neg ? "-" : "") + std::to_string(ip);
    if (frac)
    {
        out += ".";
        while (frac)
        {
            frac *= 10;
            out += char('0' + (frac >> bits));
            frac &= (uint64_t(1) << bits) - 1;
        }
    }
    return out;
}

int64_t parsed(const std::string& text, int* status = nullptr)
{
    fixed_t v = -12345;
    bool neg = !text.empty() && text[0] == '-';
    int st = moo_numparse(text.c_str() + (neg ? 1 : 0), &v);
    if (status)
        *status = st;
    return neg ? -v : v;
}

} // namespace

// DESIGN.md "Literals": the exact rational, rounded once, ties to even, no libc
TEST_CASE("LiteralsAreExact")
{
    Lcg r;
    for (int i = 0; i < 20000; ++i)
    {
        int64_t raw = int64_t(r.next() >> (1 + r.next() % 63));
        int st = -1;
        REQUIRE(parsed(exactDecimal(raw), &st) == raw);
        CHECK((st == MOO_PARSE_OK || (raw == 0 && st == MOO_PARSE_OK)));

        if (raw < INT64_MAX - 1)
        {
            // exactly half way between raw and raw + 1: to the even one
            int64_t even = (raw & 1) ? raw + 1 : raw;
            CHECK(parsed(exactDecimal(raw, 1)) == even);
            // a hair above half rounds up, a hair below rounds down
            CHECK(parsed(exactDecimal(raw, 1) + (exactDecimal(raw, 1).find('.') == std::string::npos ? ".000000000000000000000001" : "000000000000000000000001")) == raw + 1);
        }
    }

    // the examples DESIGN.md gives, and the exponent forms
    CHECK(parsed("0.1") == 6554);         // 6553.6 rounds up
    CHECK(parsed("3.25") == 3 * 65536 + 16384);
    CHECK(parsed("1e3") == 1000 * 65536);
    CHECK(parsed("2.5e-3") == 164);       // 163.84
    CHECK(parsed("1E+2") == 100 * 65536);
    CHECK(parsed(".5") == 32768);
    CHECK(parsed("5.") == 5 * 65536);
    CHECK(parsed("0x10") == 16 * 65536);
    CHECK(parsed("0XfF") == 255 * 65536);
    CHECK(parsed("0b1010") == 10 * 65536);
    CHECK(parsed("000000000000000000000000000000000000012") == 12 * 65536);
    // 2^-17 exactly is half a unit: ties to even gives 0
    int st = -1;
    CHECK(parsed("0.00000762939453125", &st) == 0);
    CHECK(st == MOO_PARSE_BELOW_RESOLUTION);
    CHECK(parsed("0.0000076293945312500001") == 1);
    CHECK(parsed("1e-40", &st) == 0);
    CHECK(st == MOO_PARSE_BELOW_RESOLUTION);
    CHECK(parsed("0e100", &st) == 0);
    CHECK(st == MOO_PARSE_OK);

    // the range: the largest number, and the first beyond it
    CHECK(parsed(exactDecimal(INT64_MAX)) == INT64_MAX);
    parsed("140737488355328", &st);
    CHECK(st == MOO_PARSE_OUT_OF_RANGE);
    parsed("1e15", &st);
    CHECK(st == MOO_PARSE_OUT_OF_RANGE);
    parsed("0x800000000000", &st); // 2^47
    CHECK(st == MOO_PARSE_OUT_OF_RANGE);
    CHECK(parsed("0x7fffffffffff") == (int64_t(0x7fffffffffff) << 16));
    // half way between the two largest numbers is the even one; half way past the largest rounds out of range
    CHECK(parsed(exactDecimal(INT64_MAX - 1, 1)) == INT64_MAX - 1);
    parsed(exactDecimal(INT64_MAX, 1), &st);
    CHECK(st == MOO_PARSE_OUT_OF_RANGE);

    // malformed
    for (const char* bad : {"", ".", "e5", "1e", "1e+", "0x", "0b2", "0xg", "1.2.3", "1 ", "+1", "1_0", "inf", "nan", "0x1.8"})
    {
        fixed_t v = 77;
        CHECK_MESSAGE(moo_numparse(bad, &v) == MOO_PARSE_MALFORMED, bad);
    }
}

// DESIGN.md "Number formatting": the shortest decimal that reads back to exactly the value
TEST_CASE("TostringIsShortestRoundTrip")
{
    char buf[64];
    auto text = [&](int64_t n) { return std::string(buf, moo_numtostr(buf, n)); };
    auto check = [&](int64_t n) {
        std::string t = text(n);
        int st = -1;
        REQUIRE_MESSAGE(parsed(t, &st) == n, t);
        // no shorter decimal reads back: dropping the last fraction digit (rounded either way) gives another value
        size_t dot = t.find('.');
        if (dot != std::string::npos)
        {
            std::string shorter = t.substr(0, t.size() - 1);
            if (shorter.back() == '.')
                shorter.pop_back();
            CHECK_MESSAGE(parsed(shorter) != n, t);
        }
    };
    for (int64_t n = -66000; n <= 66000; n += 3)
        check(n);
    Lcg r;
    for (int i = 0; i < 20000; ++i)
        check(int64_t(r.next()) >> (1 + r.next() % 63));
    check(INT64_MAX);
    check(-INT64_MAX);

    CHECK(text(6554) == "0.1");
    CHECK(text(6554 + 13107) == "0.3"); // 0.1 + 0.2 reads back as 0.3
    CHECK(text(21845) == "0.33333");
    CHECK(text(3 * 65536) == "3");
    CHECK(text(-32768) == "-0.5");
    CHECK(text(0) == "0");
    CHECK(text(1) == "0.00002");
    CHECK(text(INT64_MAX) == "140737488355327.99998");
}

// DESIGN.md "string.format": C's %f %e %g on the exact value. Below 2^37 the double raw / 2^16 is the exact value, and
// the C library prints a double exactly, rounding half to even, so it is the oracle.
TEST_CASE("FormatMatchesCOnTheExactValue")
{
    const char* specs[] = {"", ".0", ".1", ".2", ".3", ".5", ".10", ".16", ".20", "10.3", "-12.4", "+.2", " .3", "012.5", "#.0", "#",
        "-+15.6", "1", "25", ".40"};
    const char convs[] = {'f', 'e', 'E', 'g', 'G'};
    Lcg r;
    char mine[256], theirs[256], form[64];
    int checked = 0;
    for (int i = 0; i < 6000; ++i)
    {
        int64_t n = int64_t(r.next()) >> (27 + r.next() % 36); // |n| < 2^37 raw: exact as a double
        if (i % 7 == 0)
            n = int64_t(r.next() % 200000) - 100000;
        double d = double(n) / 65536.0;
        for (const char* spec : specs)
            for (char conv : convs)
            {
                snprintf(form, sizeof(form), "%%%s%c", spec, conv);
                snprintf(theirs, sizeof(theirs), form, d);
                REQUIRE(moo_numformat(mine, sizeof(mine), spec, conv, n) >= 0);
                CHECK_MESSAGE(std::string(mine) == std::string(theirs), form << " of raw " << n << ": moo " << mine << " C " << theirs);
                checked++;
            }
    }
    CHECK(checked > 500000);

    // ties to even on the exact value
    CHECK(moo_numformat(mine, sizeof(mine), ".0", 'f', 32768) >= 0);
    CHECK(std::string(mine) == "0");
    CHECK(moo_numformat(mine, sizeof(mine), ".0", 'f', 3 * 32768) >= 0);
    CHECK(std::string(mine) == "2");
    // the largest number, beyond the double oracle's exact range
    CHECK(moo_numformat(mine, sizeof(mine), ".16", 'f', INT64_MAX) >= 0);
    CHECK(std::string(mine) == "140737488355327.9999847412109375");
    CHECK(moo_numformat(mine, sizeof(mine), ".3", 'e', -INT64_MAX) >= 0);
    CHECK(std::string(mine) == "-1.407e+14");
}

TEST_SUITE_END();
