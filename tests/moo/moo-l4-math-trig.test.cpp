// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4 trig (docs/DESIGN.md "math library"): math.sin, math.cos and math.tan on fixed, and their
// bytecode fastcalls. The library and the fastcall share one definition (VM/src/lmathlib.cpp), so they
// give the same bits; both must match fixed's C directly. Each function is checked at the multiples of
// pi/2, at zero, at the domain limits and on 4096 seeded inputs. The moo-l4-math-oracle vectors have not
// landed, so the maximum-error comparison is deferred to that card; here the reference is fixed's C.
#include "lua.h"
#include "lualib.h"
#include "moo.h"
#include "moo_number.h"

#include "fixed/fixed_math.h"

#include "MooHost.h"

#include "doctest.h"

#include <stdint.h>
#include <string>
#include <vector>

TEST_SUITE_BEGIN("moo-l4-math-trig");

namespace
{

const int64_t kHalfPi = 102944; // round(pi/2 * 2^16)

// 4 deterministic generator for the raw pool
struct Lcg
{
    uint64_t s = 0x9e3779b97f4a7c15ull;
    uint64_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return s ^ (s >> 29);
    }
};

// Runs a chunk that returns moo_test.raw(...) and returns the raw Q48.16 value as int64_t.
int64_t runRaw(lua_State* L, const std::string& chunk)
{
    std::string out = mooRun(L, chunk.c_str());
    CHECK_MESSAGE(out.rfind("error:", 0) != 0, "script error: " << out);
    return std::stoll(out);
}

// The library path: a local alias keeps the compiler from taking the fastcall.
int64_t libRaw(lua_State* L, const char* name, int64_t x)
{
    return runRaw(L, "local f = math." + std::string(name) + "\n"
                      "return moo_test.raw(f(moo_test.fromraw('" + std::to_string(x) + "')))");
}

// The fastcall path: the direct builtin call compiles to a fastcall when it can.
int64_t fcRaw(lua_State* L, const char* name, int64_t x)
{
    return runRaw(L, "return moo_test.raw(math." + std::string(name) + "(moo_test.fromraw('"
                          + std::to_string(x) + "')))");
}

// The reference from fixed's C, called directly.
int64_t refSin(int64_t x)
{
    return fixSin(x);
}

int64_t refCos(int64_t x)
{
    return fixCos(x);
}

int64_t refTan(int64_t x)
{
    fixCosSin cs = fixComputeCosSin(x);
    return cs.cosine == 0 ? 0 : moo_numdiv(cs.sine, cs.cosine);
}

bool tanDivByZero(int64_t x)
{
    return fixComputeCosSin(x).cosine == 0;
}

// The input pool: zero, multiples of pi/2, the domain limits, large values, and seeded noise.
std::vector<int64_t> inputs()
{
    std::vector<int64_t> v;
    v.push_back(0);
    for (int k = -8; k <= 8; ++k)
        v.push_back(k * kHalfPi);
    v.push_back(INT64_MAX);
    v.push_back(-INT64_MAX);
    v.push_back(int64_t(1) << 40);
    v.push_back(-(int64_t(1) << 40));
    v.push_back(65536);       // 1.0
    v.push_back(-65536);
    v.push_back(655);
    v.push_back(-655);
    Lcg r;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t n = int64_t(r.next());
        v.push_back(n == INT64_MIN ? INT64_MAX : n);
    }
    return v;
}

} // namespace

// The library and the fastcall give the same bits for every input.
TEST_CASE("MathTrigLibraryFastcallBitExact")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    const char* fns[] = {"sin", "cos", "tan"};
    for (const char* name : fns)
        for (int64_t x : inputs())
        {
            if (std::string(name) == "tan" && tanDivByZero(x))
                continue;
            std::string what = std::string(name) + "(" + std::to_string(x) + ")";
            CHECK_MESSAGE(libRaw(L, name, x) == fcRaw(L, name, x), what << ": library vs fastcall");
        }

    lua_close(L);
}

// The library matches fixed's C called directly.
TEST_CASE("MathTrigMatchesFixedC")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    for (int64_t x : inputs())
    {
        CHECK_MESSAGE(libRaw(L, "sin", x) == refSin(x), "sin(" << x << ")");
        CHECK_MESSAGE(libRaw(L, "cos", x) == refCos(x), "cos(" << x << ")");
        if (!tanDivByZero(x))
            CHECK_MESSAGE(libRaw(L, "tan", x) == refTan(x), "tan(" << x << ")");
    }

    lua_close(L);
}

// tan = sin / cos; a zero cosine is "division by zero", on the library and the fastcall path.
TEST_CASE("MathTrigDivisionByZero")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    for (int64_t x : {kHalfPi, -kHalfPi})
    {
        std::string lib = "local f = math.tan; local ok, err = pcall(f, moo_test.fromraw('"
                          + std::to_string(x) + "')) return tostring(ok) .. '|' .. tostring(err)";
        std::string out = mooRun(L, lib.c_str());
        CHECK_MESSAGE(out == "false|division by zero", "library tan(" << x << "): " << out);

        std::string fc = "local ok, err = pcall(math.tan, moo_test.fromraw('"
                          + std::to_string(x) + "')) return tostring(ok) .. '|' .. tostring(err)";
        out = mooRun(L, fc.c_str());
        CHECK_MESSAGE(out == "false|division by zero", "fastcall tan(" << x << "): " << out);
    }

    lua_close(L);
}

TEST_SUITE_END();
