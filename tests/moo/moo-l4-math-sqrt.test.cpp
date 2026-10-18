// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// DESIGN.md "math library": sqrt from fixSqrt, exact, rounded toward zero; negative is a named error.
// Each result r of math.sqrt(a) must satisfy r*r <= a*2^16 < (r+1)*(r+1) in raw Q48.16 units, checked
// in 128-bit integer arithmetic (never by squaring in double). Exhaustive over raw inputs 0..2^24, plus
// 4096 seeded large inputs across the full range.
#include "lua.h"
#include "lualib.h"

#include "moo.h"
#include "moo_number.h"

#include "MooWide.h"
#include "MooHost.h"

#include "doctest.h"

#include <stdint.h>

namespace
{

std::string runScript(const char* source)
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    std::string result = mooRun(L, source);
    lua_close(L);
    return result;
}

// a deterministic generator for the large-input pool
struct Lcg
{
    uint64_t s = 0x9e3779b97f4a7c15ull;
    uint64_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return s ^ (s >> 29);
    }
};

// a >= 0, r >= 0: r*r <= a*2^16 < (r+1)*(r+1)
bool sqrtInvariant(int64_t a, int64_t r)
{
    if (a < 0 || r < 0)
        return false;
    MooWide X = MooWide::from(a).shl(16); // a * 2^16
    MooWide R2 = mooMulWide(r, r); // r*r
    MooWide R12 = mooMulWide(r + 1, r + 1); // (r+1)*(r+1)
    return !(X < R2) && X < R12;
}

struct Vm
{
    lua_State* L = nullptr;
    int fn = 0; // stack index of math.sqrt

    Vm()
    {
        L = luaL_newstate();
        REQUIRE(L != nullptr);
        luaL_openlibs(L);
        lua_getglobal(L, "math");
        lua_getfield(L, -1, "sqrt");
        lua_remove(L, -2);
        fn = lua_gettop(L); // math.sqrt sits here for the whole test
    }

    ~Vm()
    {
        lua_close(L);
    }

    // math.sqrt(a) through the C function; returns the raw result, or INT64_MIN on error
    int64_t sqrt(int64_t a)
    {
        lua_pushvalue(L, fn);
        lua_pushnumber(L, a);
        if (lua_pcall(L, 1, 1, 0) != 0)
            return INT64_MIN;
        int64_t r = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return r;
    }
};

} // namespace

TEST_SUITE_BEGIN("moo-l4-math-sqrt");

// Exhaustive over raw inputs 0..2^24 through the VM, each result checked in 128-bit integer arithmetic.
TEST_CASE("SqrtExhaustiveSmall")
{
    Vm vm;
    for (int64_t a = 0; a <= (int64_t(1) << 24); ++a)
    {
        int64_t r = vm.sqrt(a);
        CHECK_MESSAGE(r != INT64_MIN, "sqrt(" << a << ") raised");
        CHECK_MESSAGE(sqrtInvariant(a, r), "sqrt(" << a << ") = " << r);
    }
}

// 4096 seeded large inputs across the full non-negative range (raw up to INT64_MAX).
TEST_CASE("SqrtLargeSeeded")
{
    Vm vm;
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t a = int64_t(rng.next());
        if (a < 0)
            a = -a;
        int64_t r = vm.sqrt(a);
        CHECK_MESSAGE(r != INT64_MIN, "sqrt(" << a << ") raised");
        CHECK_MESSAGE(sqrtInvariant(a, r), "sqrt(" << a << ") = " << r);
    }

    // the top of the domain
    int64_t maxA = INT64_MAX;
    int64_t r = vm.sqrt(maxA);
    CHECK(r != INT64_MIN);
    CHECK(sqrtInvariant(maxA, r));
}

// Negative input is the named error, on both the regular call and the fastcall path.
TEST_CASE("SqrtNegativeError")
{
    // regular call path: through a local alias so the bytecode cannot take a fastcall
    CHECK(runScript("local s = math.sqrt local ok, err = pcall(function() return s(-1) end) return tostring(ok) .. '|' .. tostring(err)").find("false|") == 0);
    CHECK(runScript("local s = math.sqrt local ok, err = pcall(function() return s(-1) end) return tostring(err)").find("math.sqrt of a negative number") != std::string::npos);

    // fastcall path: a direct call the compiler may compile to a fastcall
    CHECK(runScript("local ok, err = pcall(function() return math.sqrt(-1) end) return tostring(ok) .. '|' .. tostring(err)").find("false|") == 0);
    CHECK(runScript("local ok, err = pcall(function() return math.sqrt(-1) end) return tostring(err)").find("math.sqrt of a negative number") != std::string::npos);
}

// Wiring: known results on both paths, and zero.
TEST_CASE("SqrtKnownValues")
{
    Vm vm;
    CHECK(vm.sqrt(0) == 0);
    CHECK(vm.sqrt(1) == 256); // sqrt(2^-16) = 2^-8
    CHECK(vm.sqrt(65536) == 65536); // sqrt(1) = 1
    CHECK(vm.sqrt(131072) == 92681); // sqrt(2)
    CHECK(vm.sqrt(262144) == 131072); // sqrt(4) = 2
    CHECK(vm.sqrt(16384) == 32768); // sqrt(0.25) = 0.5
    CHECK(vm.sqrt(32768) == 46340); // sqrt(0.5)

    // fastcall path: direct literal calls the compiler may compile to fastcalls
    CHECK(mooRun(vm.L, "assert(math.sqrt(4) == 2) return 'OK'") == "OK");
    CHECK(mooRun(vm.L, "assert(math.sqrt(9) == 3) return 'OK'") == "OK");
    CHECK(mooRun(vm.L, "assert(math.sqrt(0) == 0) return 'OK'") == "OK");
}

TEST_SUITE_END();
