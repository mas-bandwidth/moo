// This file is part of moo, a fork of the Luau programming language; moo changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4 inverse trig (docs/DESIGN.md "math library"): math.atan2, math.atan, math.asin and math.acos
// on fixed, and their bytecode fastcalls. The library and the fastcall share one definition
// (VM/src/lmathlib.cpp), so they give the same bits; both must match fixed C directly. Out-of-domain
// |x| > 1 in asin and acos raises "math.asin domain" / "math.acos domain" on both paths.
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"
#include "moo_number.h"

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"

#include "MooHost.h"

#include "doctest.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

TEST_SUITE_BEGIN("moo-l4-math-inverse-trig");

namespace
{

constexpr int64_t kRawOne = 65536LL;
constexpr int64_t kRawHalf = 32768LL;
constexpr int64_t kRawMax = INT64_MAX;
constexpr int64_t kRawMin = -INT64_MAX;

struct Lcg
{
    uint64_t s = 0x6d6f6f5f696e7674ull;
    uint64_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return s ^ (s >> 29);
    }
    int64_t nextRaw()
    {
        int64_t n = int64_t(next());
        return n == INT64_MIN ? kRawMax : n;
    }
    int64_t nextUnitRaw()
    {
        // uniform in [-65536, 65536]
        return int64_t(next() % uint64_t(2 * kRawOne + 1)) - kRawOne;
    }
};

fixed_t refAtan2(fixed_t y, fixed_t x)
{
    return fixAtan2(y, x);
}

fixed_t refAtan(fixed_t x)
{
    return fixAtan2(x, kRawOne);
}

fixed_t refAsin(fixed_t x)
{
    return fixAtan2(x, fixSqrt(kRawOne - fixMul(x, x)));
}

fixed_t refAcos(fixed_t x)
{
    return fixAtan2(fixSqrt(kRawOne - fixMul(x, x)), x);
}

int compileFuncRef(lua_State* L, const char* source, int optLevel)
{
    lua_CompileOptions opts = {};
    opts.optimizationLevel = optLevel;
    opts.debugLevel = 1;
    size_t bcSize = 0;
    char* bc = luau_compile(source, strlen(source), &opts, &bcSize);
    REQUIRE(bc != nullptr);
    int loadStatus = luau_load(L, "=inv_trig", bc, bcSize, 0);
    free(bc);
    REQUIRE(loadStatus == 0);
    int callStatus = lua_pcall(L, 0, 1, 0);
    REQUIRE(callStatus == 0);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    return ref;
}

int64_t call1(lua_State* L, int ref, int64_t a)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    int st = lua_pcall(L, 1, 1, 0);
    REQUIRE_MESSAGE(st == 0, "lua_pcall error: " << lua_tostring(L, -1));
    int64_t r = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return r;
}

int64_t call2(lua_State* L, int ref, int64_t a, int64_t b)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    lua_pushnumber(L, b);
    int st = lua_pcall(L, 2, 1, 0);
    REQUIRE_MESSAGE(st == 0, "lua_pcall error: " << lua_tostring(L, -1));
    int64_t r = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return r;
}

std::vector<int64_t> unitInputs()
{
    std::vector<int64_t> v = {
        -kRawOne,
        -kRawOne + 1,
        -kRawHalf - 1,
        -kRawHalf,
        -kRawHalf + 1,
        -12345,
        -1,
        0,
        1,
        12345,
        kRawHalf - 1,
        kRawHalf,
        kRawHalf + 1,
        kRawOne - 1,
        kRawOne,
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
        v.push_back(rng.nextUnitRaw());
    return v;
}

std::vector<int64_t> fullInputs()
{
    std::vector<int64_t> v = {
        0,
        1,
        -1,
        kRawHalf,
        -kRawHalf,
        kRawOne,
        -kRawOne,
        kRawOne + 1,
        -(kRawOne + 1),
        102944,
        -102944,
        205887,
        -205887,
        int64_t(1) << 40,
        -(int64_t(1) << 40),
        kRawMax,
        kRawMin,
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
        v.push_back(rng.nextRaw());
    return v;
}

} // namespace

TEST_CASE("MathInverseTrigLibraryFastcallAndFixedC")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    int asinLib = compileFuncRef(L, "local f = math.asin; return function(x) return f(x) end", 0);
    int asinFc = compileFuncRef(L, "return function(x) return math.asin(x) end", 2);
    int acosLib = compileFuncRef(L, "local f = math.acos; return function(x) return f(x) end", 0);
    int acosFc = compileFuncRef(L, "return function(x) return math.acos(x) end", 2);
    int atanLib = compileFuncRef(L, "local f = math.atan; return function(x) return f(x) end", 0);
    int atanFc = compileFuncRef(L, "return function(x) return math.atan(x) end", 2);
    int atan2Lib = compileFuncRef(L, "local f = math.atan2; return function(y, x) return f(y, x) end", 0);
    int atan2Fc = compileFuncRef(L, "return function(y, x) return math.atan2(y, x) end", 2);

    for (int64_t x : unitInputs())
    {
        int64_t expAsin = refAsin(x);
        int64_t gotAsinLib = call1(L, asinLib, x);
        int64_t gotAsinFc = call1(L, asinFc, x);
        CHECK_MESSAGE(gotAsinLib == expAsin, "asin lib(" << x << "): got " << gotAsinLib << ", expected " << expAsin);
        CHECK_MESSAGE(gotAsinFc == expAsin, "asin fc(" << x << "): got " << gotAsinFc << ", expected " << expAsin);

        int64_t expAcos = refAcos(x);
        int64_t gotAcosLib = call1(L, acosLib, x);
        int64_t gotAcosFc = call1(L, acosFc, x);
        CHECK_MESSAGE(gotAcosLib == expAcos, "acos lib(" << x << "): got " << gotAcosLib << ", expected " << expAcos);
        CHECK_MESSAGE(gotAcosFc == expAcos, "acos fc(" << x << "): got " << gotAcosFc << ", expected " << expAcos);
    }

    for (int64_t x : fullInputs())
    {
        int64_t expAtan = refAtan(x);
        int64_t gotAtanLib = call1(L, atanLib, x);
        int64_t gotAtanFc = call1(L, atanFc, x);
        CHECK_MESSAGE(gotAtanLib == expAtan, "atan lib(" << x << "): got " << gotAtanLib << ", expected " << expAtan);
        CHECK_MESSAGE(gotAtanFc == expAtan, "atan fc(" << x << "): got " << gotAtanFc << ", expected " << expAtan);
    }

    const int64_t axes[] = {0, 1, -1, kRawHalf, -kRawHalf, kRawOne, -kRawOne, kRawMax, kRawMin};
    for (int64_t y : axes)
    {
        for (int64_t x : axes)
        {
            int64_t expAtan2 = refAtan2(y, x);
            int64_t gotAtan2Lib = call2(L, atan2Lib, y, x);
            int64_t gotAtan2Fc = call2(L, atan2Fc, y, x);
            CHECK_MESSAGE(gotAtan2Lib == expAtan2, "atan2 lib(" << y << ", " << x << "): got " << gotAtan2Lib << ", expected " << expAtan2);
            CHECK_MESSAGE(gotAtan2Fc == expAtan2, "atan2 fc(" << y << ", " << x << "): got " << gotAtan2Fc << ", expected " << expAtan2);
        }
    }

    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t y = rng.nextRaw();
        int64_t x = rng.nextRaw();
        int64_t expAtan2 = refAtan2(y, x);
        int64_t gotAtan2Lib = call2(L, atan2Lib, y, x);
        int64_t gotAtan2Fc = call2(L, atan2Fc, y, x);
        CHECK_MESSAGE(gotAtan2Lib == expAtan2, "atan2 lib(" << y << ", " << x << "): got " << gotAtan2Lib << ", expected " << expAtan2);
        CHECK_MESSAGE(gotAtan2Fc == expAtan2, "atan2 fc(" << y << ", " << x << "): got " << gotAtan2Fc << ", expected " << expAtan2);
    }

    lua_close(L);
}

TEST_CASE("MathInverseTrigDomainErrors")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    const int64_t outOfDomain[] = {
        kRawOne + 1,
        -(kRawOne + 1),
        2 * kRawOne,
        -2 * kRawOne,
        kRawMax,
        kRawMin,
    };

    for (int64_t x : outOfDomain)
    {
        std::string s = std::to_string(x);
        std::string asinLib = "local f = math.asin; local ok, err = pcall(f, moo_test.fromraw(\"" + s + "\")); return tostring(ok) .. \"|\" .. tostring(err)";
        CHECK_EQ(mooRun(L, asinLib.c_str()), "false|math.asin domain");

        std::string asinFc = "local ok, err = pcall(math.asin, moo_test.fromraw(\"" + s + "\")); return tostring(ok) .. \"|\" .. tostring(err)";
        CHECK_EQ(mooRun(L, asinFc.c_str()), "false|math.asin domain");

        std::string asinFcInl = "local x = moo_test.fromraw(\"" + s + "\"); local ok, err = pcall(function() return math.asin(x) end); return tostring(ok) .. \"|\" .. tostring(err)";
        CHECK_EQ(mooRun(L, asinFcInl.c_str()), "false|moo:1: math.asin domain");

        std::string acosLib = "local f = math.acos; local ok, err = pcall(f, moo_test.fromraw(\"" + s + "\")); return tostring(ok) .. \"|\" .. tostring(err)";
        CHECK_EQ(mooRun(L, acosLib.c_str()), "false|math.acos domain");

        std::string acosFc = "local ok, err = pcall(math.acos, moo_test.fromraw(\"" + s + "\")); return tostring(ok) .. \"|\" .. tostring(err)";
        CHECK_EQ(mooRun(L, acosFc.c_str()), "false|math.acos domain");

        std::string acosFcInl = "local x = moo_test.fromraw(\"" + s + "\"); local ok, err = pcall(function() return math.acos(x) end); return tostring(ok) .. \"|\" .. tostring(err)";
        CHECK_EQ(mooRun(L, acosFcInl.c_str()), "false|moo:1: math.acos domain");
    }

    lua_close(L);
}

TEST_CASE("MathInverseTrigConformanceScript")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    CHECK_EQ(mooRunFile(L, "l3/moo-l4-math-inverse-trig.luau"), "OK");

    lua_close(L);
}

TEST_SUITE_END();
