// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4 integer-exponent pow, math library side (docs/DESIGN.md "math library", "pow and ^"): math.pow
// and its fastcall call moo_pow_int (Common/include/moo_pow.h) when the exponent is integer-valued, so
// the library, the fastcall and the core give the same bits; moo: a fractional exponent uses fixed's
// fixPow, with a named error for a negative base. The library path is exercised through a local alias, which the
// compiler does not turn into a fastcall (the same trick tests/moo/moo-l4-math-trig.test.cpp uses).
#include "lua.h"
#include "lualib.h"
#include "moo.h"
#include "moo_number.h"
#include "moo_pow.h"
#include "fixed/fixed_math.h"

#include "MooHost.h"

#include "doctest.h"

#include <stdint.h>
#include <string>

TEST_SUITE_BEGIN("moo-l4-pow-integer-math");

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
TEST_CASE("MathPowIntegerMatchesCoreOnSeededPairs")
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
TEST_CASE("MathPowFractionalUsesFixed")
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
            std::string want = base < 0 ? "error: pow of a negative number to a fractional power"
                                        : std::to_string(static_cast<long long>(fixPow(base, exponent)));

            CHECK_MESSAGE(sameResult(runPow(L, base, exponent, false), want), "library " << base << " ^ " << exponent);
            CHECK_MESSAGE(sameResult(runPow(L, base, exponent, true), want), "fastcall " << base << " ^ " << exponent);
        }

    lua_close(L);
}

// The edges the note names: 0 ^ 0, 0 ^ positive, 0 ^ negative, the exact edge, saturation with its
// signs, and the integer-exponent answers that differ from fixed's truncating division.
TEST_CASE("MathPowIntegerEdges")
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

TEST_SUITE_END();
