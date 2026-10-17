// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Tests of moo's number operations (VM/src/moo_number.h) and the boundary (moo_fromdouble,
// moo_todouble), against independent references: 128-bit integer arithmetic, and for the boundary the
// FPU itself (scaling by 2^16 is exact, and nearbyint rounds ties to even in the default mode).
#include "moo.h"
#include "moo_number.h"

#include "doctest.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

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

int64_t clampRef(__int128 v)
{
    if (v > INT64_MAX)
        return INT64_MAX;
    if (v < -INT64_MAX)
        return -INT64_MAX;
    return int64_t(v);
}

__int128 floorDivRef(__int128 a, __int128 b)
{
    __int128 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    return q;
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
        CHECK(moo_numadd(a, b) == clampRef(__int128(a) + b));
        CHECK(moo_numsub(a, b) == clampRef(__int128(a) - b));
    }
    for (int64_t a : kEdges)
        for (int64_t b : kEdges)
        {
            CHECK(moo_numadd(a, b) == clampRef(__int128(a) + b));
            CHECK(moo_numsub(a, b) == clampRef(__int128(a) - b));
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
        __int128 p = __int128(a) * b + 32768;
        CHECK(moo_nummul(a, b) == clampRef(p >> 16));
        if (b != 0)
            CHECK(moo_numdiv(a, b) == clampRef((__int128(a) << 16) / b));
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
        __int128 q = floorDivRef(a, b);
        CHECK(moo_numidiv(a, b) == clampRef(q * 65536));
        CHECK(moo_nummod(a, b) == int64_t(__int128(a) - q * b));
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

TEST_SUITE_END();
