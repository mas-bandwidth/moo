// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The boundary: a host double or float converts to Q48.16 by the one rule (nearest, ties to even,
// saturating, NaN to 0), and a number converts back the same way (docs/DESIGN.md "Boundary").

#include "moo_number.h"
#include "moo.h"
#include "MooWide.h"
#include "doctest.h"
#include "lualib.h"
#include "lgc.h"

#include <stdint.h>
#include <string.h>
#include <limits>
#include <string>

TEST_SUITE_BEGIN("boundary");

// The boundary out of the VM: moo_numtofloatbits / moo_numtodoublebits (Common/include/moo_number.h)
// and moo_tofloat / moo_todouble (VM/include/moo.h) encode a Q48.16 value to the nearest IEEE-754
// binary32 / binary64 bit pattern, ties to even, in integer arithmetic (docs/DESIGN.md, "Boundary").
// The oracle in this file recomputes the bits from first principles in MooWide 128-bit arithmetic
// (tests/MooWide.h), never __int128 and never the function under test.

namespace
{

int bitlen(uint64_t m)
{
    int b = 0;
    for (uint64_t x = m; x; x >>= 1)
        b += 1;
    return b;
}

// binary32 bits nearest m/2^16, ties to even, from first principles (exact rational comparison of the two
// candidate floats, in MooWide arithmetic)
uint32_t refFloatBits(uint64_t m, bool neg)
{
    if (m == 0)
        return 0;
    int E = bitlen(m) - 17;
    uint64_t S_lo = 7 - E >= 0 ? m << (7 - E) : m >> (E - 7);
    int cmp;
    if (E >= 8)
    {
        uint64_t rhs = (2 * S_lo + 1) << (E - 8);
        cmp = (m > rhs) - (m < rhs);
    }
    else
    {
        MooWide lhs = MooWide::from(int64_t(m)).shl(8 - E);
        MooWide rhs = MooWide::from(int64_t(2 * S_lo + 1));
        cmp = (rhs < lhs) - (lhs < rhs);
    }
    uint64_t S;
    if (cmp > 0)
        S = S_lo + 1;
    else if (cmp < 0)
        S = S_lo;
    else
        S = (S_lo & 1) ? S_lo + 1 : S_lo;
    if (S == (uint64_t(1) << 24))
    {
        S = uint64_t(1) << 23;
        E += 1;
    }
    uint32_t bits = uint32_t(E + 127) << 23 | uint32_t(S - (uint64_t(1) << 23));
    return neg ? bits | 0x80000000u : bits;
}

// binary64 bits nearest m/2^16, ties to even, from first principles
uint64_t refDoubleBits(uint64_t m, bool neg)
{
    if (m == 0)
        return 0;
    int E = bitlen(m) - 17;
    uint64_t S_lo = 36 - E >= 0 ? m << (36 - E) : m >> (E - 36);
    int cmp;
    if (E >= 37)
    {
        uint64_t rhs = (2 * S_lo + 1) << (E - 37);
        cmp = (m > rhs) - (m < rhs);
    }
    else
    {
        MooWide lhs = MooWide::from(int64_t(m)).shl(37 - E);
        MooWide rhs = MooWide::from(int64_t(2 * S_lo + 1));
        cmp = (rhs < lhs) - (lhs < rhs);
    }
    uint64_t S;
    if (cmp > 0)
        S = S_lo + 1;
    else if (cmp < 0)
        S = S_lo;
    else
        S = (S_lo & 1) ? S_lo + 1 : S_lo;
    if (S == (uint64_t(1) << 53))
    {
        S = uint64_t(1) << 52;
        E += 1;
    }
    uint64_t bits = uint64_t(E + 1023) << 52 | (S - (uint64_t(1) << 52));
    return neg ? bits | 0x8000000000000000ull : bits;
}

// the nearest Q48.16 value to the float whose bits are given, ties to even, exact (independent of moo_fromfloat)
int64_t refNearestQ(uint32_t bits)
{
    bool neg = bits >> 31;
    int E = int((bits >> 23) & 0xff) - 127;
    uint64_t S = (bits & 0x7fffffu) | 0x800000u;
    uint64_t q;
    if (E - 7 >= 0)
        q = S << (E - 7);
    else
    {
        int drop = 7 - E;
        q = S >> drop;
        uint64_t rest = S & ((uint64_t(1) << drop) - 1);
        uint64_t half = uint64_t(1) << (drop - 1);
        if (rest > half || (rest == half && (q & 1)))
            q += 1;
    }
    return neg ? -int64_t(q) : int64_t(q);
}

uint64_t rngState = 0x123456789abcdef0ull;
uint64_t rng()
{
    rngState = rngState * 6364136223846793005ull + 1442695040888963407ull;
    return rngState;
}

} // namespace

// DESIGN.md "Boundary" and "Layer 3 as built": the boundary rule out of the VM is the nearest float bit
// pattern, ties to even, in integer arithmetic; every Q48.16 value is finite in both formats.
TEST_CASE("BoundaryToFloatIsNearestTiesToEven")
{
    // exact values: 0, 1, -1, the domain ends, 1 raw, and every power of two raw
    struct
    {
        int64_t raw;
        uint32_t bits;
    } exact[] = {
        { 0, 0x00000000u },
        { MOO_NUMBER_ONE, 0x3F800000u }, // 1.0
        { -MOO_NUMBER_ONE, 0xBF800000u }, // -1.0
        { 1, 0x37800000u }, // 2^-16
        { MOO_NUMBER_MAX, 0x57000000u }, // 2^47
        { -MOO_NUMBER_MAX, 0xD7000000u }, // -2^47
    };
    for (const auto& c : exact)
    {
        uint64_t m = c.raw < 0 ? uint64_t(0) - uint64_t(c.raw) : uint64_t(c.raw);
        CHECK(moo_numtofloatbits(c.raw) == c.bits);
        CHECK(moo_numtofloatbits(c.raw) == refFloatBits(m, c.raw < 0));
        CHECK(moo_numtodoublebits(c.raw) == refDoubleBits(m, c.raw < 0));
    }

    for (int k = 0; k <= 62; ++k)
    {
        fixed_t raw = fixed_t(1) << k;
        uint64_t m = uint64_t(raw);
        CHECK(moo_numtofloatbits(raw) == refFloatBits(m, false));
        CHECK(moo_numtodoublebits(raw) == refDoubleBits(m, false));
    }

    // ties: a raw value exactly halfway between two floats rounds to the even mantissa. Construct the
    // midpoint of floats with significands S and S+1 at exponent E (E >= 8 so the midpoint is a raw
    // integer): raw = (2S+1) * 2^(E-8).
    for (int E = 8; E <= 16; ++E)
    {
        for (uint64_t S : {uint64_t(0x800000u), uint64_t(0x800001u), uint64_t(0x900000u),
                            uint64_t(0xFFFFFFu), uint64_t(0xFFFFFEu)})
        {
            fixed_t raw = fixed_t((2 * S + 1) << (E - 8));
            CHECK(moo_numtofloatbits(raw) == refFloatBits(uint64_t(raw < 0 ? -raw : raw), raw < 0));
        }
    }
}

// DESIGN.md "Boundary": for seeded raw values, moo_fromfloat(moo_tofloat(n)) is the nearest Q48.16 to the
// float and moo_tofloat(moo_fromfloat-result) is idempotent; the independent oracle agrees on every sample.
TEST_CASE("BoundaryToFloatSeeded")
{
    for (uint64_t i = 0; i < (uint64_t(1) << 20); ++i)
    {
        int64_t n = int64_t(rng() % uint64_t(INT64_MAX)) * (rng() & 1 ? 1 : -1);
        uint32_t bits = moo_numtofloatbits(n);
        uint64_t m = n < 0 ? uint64_t(0) - uint64_t(n) : uint64_t(n);
        CHECK(bits == refFloatBits(m, n < 0));

        float f = moo_tofloat(n);
        int64_t q = 0;
        int status = moo_fromfloat(f, &q);
        CHECK(status == MOO_NUMBER_OK);
        CHECK(q == refNearestQ(bits));
        CHECK(moo_tofloat(q) == f);
    }
}

// Layer 4: the temporary double bridge is deleted (docs/DESIGN.md "Layer 3 as built", "TEMPORARY bridges";
// docs/DETERMINISM.md item 7). The two helpers moo_numbridgefromdouble / moo_numbridgetodouble no longer
// exist; the remaining conversions are the permanent boundary rule (moo_number.h moo_numfromdouble,
// moo_numtodoublebits) and these cases pin the values MOO.Test can reach without the bridge.

namespace
{

constexpr int64_t kOne = 65536LL;
constexpr int64_t kMax = INT64_MAX;
constexpr int64_t kMin = -INT64_MAX;

} // namespace

TEST_CASE("BoundaryOutValues")
{
    // moo_numtodouble is the exact value raw / 2^16; 2^-16 is a power of two, so these are exact
    CHECK(moo_numtodouble(0) == 0.0);
    CHECK(moo_numtodouble(1) == 1.0 / 65536.0);
    CHECK(moo_numtodouble(-1) == -1.0 / 65536.0);
    CHECK(moo_numtodouble(kOne) == 1.0);
    CHECK(moo_numtodouble(-98304) == -1.5);
    CHECK(moo_numtodouble(kMax) == double(kMax) / 65536.0);
    CHECK(moo_numtodouble(kMin) == double(kMin) / 65536.0);
}

TEST_CASE("BoundaryInValues")
{
    // the boundary rule into the VM: nearest Q48.16, ties to even
    CHECK(moo_numfromdoublevalue(0.0) == 0);
    CHECK(moo_numfromdoublevalue(1.0) == kOne);
    CHECK(moo_numfromdoublevalue(-1.5) == -98304);
    CHECK(moo_numfromdoublevalue(0.1) == 6554); // 0.1 * 65536 = 6553.6, nearest is 6554
    CHECK(moo_numfromdoublevalue(-0.1) == -6554);
    CHECK(moo_numfromdoublevalue(1.5 / 65536.0) == 2);  // a tie, even
    CHECK(moo_numfromdoublevalue(2.5 / 65536.0) == 2);  // a tie, even
    CHECK(moo_numfromdoublevalue(3.5 / 65536.0) == 4);  // a tie, even
    CHECK(moo_numfromdoublevalue(140737488355328.0) == kMax);  // 2^47 is out of range
    CHECK(moo_numfromdoublevalue(-140737488355328.0) == kMin); // -2^47 is out of range
}

TEST_CASE("BoundaryNonFinite")
{
    // NaN gives 0, an infinity saturates (the boundary contract in moo.h)
    CHECK(moo_numfromdoublevalue(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(moo_numfromdoublevalue(std::numeric_limits<double>::infinity()) == kMax);
    CHECK(moo_numfromdoublevalue(-std::numeric_limits<double>::infinity()) == kMin);
}

TEST_CASE("BoundaryHeapDumpUsesExactFormatter")
{
    // moo: enumerate a live table's collectable values through lgcdebug.cpp's enumtable path.
    // The fractional edge catches a regression to %.14g: that would print 0.10000610351563.
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, lua_Number(-98304));
    lua_pushstring(L, "negative");
    lua_settable(L, -3);
    lua_pushnumber(L, lua_Number(6554));
    lua_pushstring(L, "fractional");
    lua_settable(L, -3);

    struct EdgeNames
    {
        bool negative = false;
        bool fractional = false;
    } names;
    luaC_enumheap(
        L, &names,
        [](void*, void*, uint8_t, uint8_t, size_t, const char*) {},
        [](void* context, void*, void*, const char* name)
        {
            EdgeNames& seen = *static_cast<EdgeNames*>(context);
            if (name && strcmp(name, "-1.5") == 0)
                seen.negative = true;
            if (name && strcmp(name, "0.1") == 0)
                seen.fractional = true;
        });
    CHECK(names.negative);
    CHECK(names.fractional);
    lua_close(L);
}

TEST_SUITE_END();
