// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4 integer-exponent pow in the VM (docs/DESIGN.md "math library", "pow and ^"): luai_numpow and the
// ^ opcodes call moo_pow_int for an integer-valued exponent, and a fractional exponent stays on the libm
// bridge. The scripts run through the VM (`^` and the constant-exponent opcode); the reference is
// moo_pow_int called directly and the bridge itself.
#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "moo_pow.h"

#include "MooHost.h"

#include "doctest.h"

#include <math.h>
#include <stdint.h>

#include <string>
#include <vector>

TEST_SUITE_BEGIN("moo-l4-pow-integer-vm");

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

// 2^12 seeded (base, integer exponent) pairs: the VM's bits are moo_pow_int's bits, status for status.
TEST_CASE("MooPowIntVmMatchesMooPowInt")
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

        fixed_t ref = 0;
        int status = moo_pow_int(a, n, &ref);
        std::string got = runPow(L, a, b);
        CAPTURE(a);
        CAPTURE(n);

        if (status == MOO_POW_DIVISION_BY_ZERO)
        {
            CHECK_MESSAGE(got == "error: moo:1: division by zero", "0 ^ " << n << ": " << got);
        }
        else
        {
#if MOO_OVERFLOW_ERRORS
            if (status == MOO_POW_SATURATED)
                CHECK_MESSAGE(got == "error: moo:1: number overflow in '^'", a << " ^ " << n << ": " << got);
            else
#endif
                CHECK_MESSAGE(got == std::to_string(ref), a << " ^ " << n << ": vm " << got << " ref " << ref);
        }

        got = runPowK(L, a, n);
        if (status == MOO_POW_DIVISION_BY_ZERO)
            CHECK_MESSAGE(got == "error: moo:1: division by zero", "powk 0 ^ " << n << ": " << got);
        else
#if MOO_OVERFLOW_ERRORS
            if (status == MOO_POW_SATURATED)
                CHECK_MESSAGE(got == "error: moo:1: number overflow in '^'", "powk " << a << " ^ " << n << ": " << got);
            else
#endif
                CHECK_MESSAGE(got == std::to_string(ref), "powk " << a << " ^ " << n << ": vm " << got << " ref " << ref);
    }

    lua_close(L);
}

// a fractional exponent still takes the libm bridge
TEST_CASE("MooPowIntVmFractionalBridge")
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
            fixed_t ref = moo_numbridgefromdouble(pow(moo_numbridgetodouble(a), moo_numbridgetodouble(b)));
            std::string got = runPow(L, a, b);
            CAPTURE(a);
            CAPTURE(b);
            CHECK_MESSAGE(got == std::to_string(ref), "bridge: vm " << got << " ref " << ref);
        }

    lua_close(L);
}

// 0 ^ negative is the named error "division by zero", on both opcode paths and through string coercion
TEST_CASE("MooPowIntVmZeroToNegativeRaises")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    for (fixed_t n : {fixed_t(-MOO_NUMBER_ONE), fixed_t(-2 * MOO_NUMBER_ONE), fixed_t(-1000 * MOO_NUMBER_ONE)})
    {
        CAPTURE(n);
        CHECK(mooRun(L, ("local function f(a, b) return a ^ b end return moo_test.raw(f(moo_test.fromraw('0'), moo_test.fromraw('" +
                             std::to_string(n) + "')))").c_str()) == "error: moo:1: division by zero");
        CHECK(mooRun(L, ("local function f(a) return a ^ moo_test.fromraw('" + std::to_string(n) +
                             "') end return moo_test.raw(f(moo_test.fromraw('0')))").c_str()) == "error: moo:1: division by zero");
        CHECK(mooRun(L, ("local function f(a, b) return a ^ b end return moo_test.raw(f('0', moo_test.fromraw('" + std::to_string(n) +
                             "')))").c_str()) == "error: moo:1: division by zero");
    }

    CHECK(mooRun(L, "return moo_test.raw(0 ^ 0)") == "65536");
    CHECK(mooRun(L, "return moo_test.raw(0 ^ 2)") == "0");

    lua_close(L);
}
