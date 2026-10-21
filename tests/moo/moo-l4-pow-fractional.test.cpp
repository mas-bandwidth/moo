// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4's fractional-exponent pow (docs/DESIGN.md "math library", "pow and ^"): an integer-valued exponent
// is moo_pow_int (moo-l4-pow-integer and its VM/math cards), any other exponent is fixPow = fixExp2(exponent *
// fixLog2(base)). A negative base with a fractional exponent is the named error "pow of a negative number to a
// fractional power"; 0 ^ positive fraction = 0. The `^` opcode (luai_numpow) cannot raise, so it releases
// fixPow's value (0 for a base <= 0); the named error surfaces in math.pow and its fastcall.
#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "fixed/fixed_math.h"

#include "MooHost.h"

#include "doctest.h"

#include <math.h>
#include <stdint.h>

#include <map>
#include <string>

TEST_SUITE_BEGIN("moo-l4-pow-fractional");

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

// 2^12 seeded (base, fractional exponent) pairs through the `^` opcode (two-operand and POWK), matched bit for
// bit to fixPow. A negative base with a fractional exponent releases fixPow's 0 on the opcode path; the named
// error is math.pow's (below).
TEST_CASE("MooPowFractionalVmMatchesFixPow")
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

        int64_t ref = fixPow(a, b);
        std::string got = runPow(L, a, b);
        CAPTURE(a);
        CAPTURE(b);
        CHECK_MESSAGE(got == std::to_string(ref), "vm " << a << " ^ " << b << ": " << got << " ref " << ref);
    }

    // 0 ^ positive fraction = 0, on both the opcode and math.pow paths
    CHECK(runPow(L, 0, 1) == "0");
    CHECK(runPow(L, 0, MOO_NUMBER_ONE / 2) == "0");
    CHECK(runMathPow(L, 0, MOO_NUMBER_ONE / 2) == "0");
    CHECK(runMathPow(L, 0, MOO_NUMBER_ONE + MOO_NUMBER_ONE / 2) == "0");

    // a negative base with a fractional exponent: the opcode releases fixPow's 0 (it cannot raise)
    CHECK(runPow(L, -MOO_NUMBER_ONE, MOO_NUMBER_ONE / 2) == "0");
    CHECK(runPow(L, -4 * MOO_NUMBER_ONE, MOO_NUMBER_ONE / 2) == "0");

    lua_close(L);
}

// math.pow and its fastcall raise the named error for a negative base with a fractional exponent. The fastcall
// returns -1 and the library path raises, so both go through math.pow.
TEST_CASE("MooPowFractionalNegativeBaseError")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    auto err = [&](const char* src) {
        return mooRun(L, src).find("pow of a negative number to a fractional power") != std::string::npos;
    };
    CHECK(err("return math.pow(-1, 0.5)"));
    CHECK(err("return math.pow(-4, 0.5)"));
    CHECK(err("return math.pow(-65536, -1.5)"));
    CHECK(err("return math.pow(-2, 1.25)"));

    // an integer exponent is still moo_pow_int, no error
    CHECK(mooRun(L, "return moo_test.raw(math.pow(moo_test.fromraw('-131072'), moo_test.fromraw('196608')))") == "-524288");
    CHECK(mooRun(L, "return moo_test.raw(math.pow(moo_test.fromraw('-131072'), moo_test.fromraw('131072')))") == "262144");

    lua_close(L);
}

// math.pow's fractional path matches fixPow for positive bases, including 0 ^ positive fraction = 0.
TEST_CASE("MooPowFractionalMathPowMatchesFixPow")
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
// table; the recorded maximum is the layer-4 bound (docs/layer4/moo-l4-pow-fractional.md).
TEST_CASE("MooPowFractionalRelativeError")
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

TEST_SUITE_END();
