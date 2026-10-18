// This file is part of moo, a fork of the Luau programming language; moo changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4 exact math operations (docs/DESIGN.md "math library"):
// - fmod(a, b): raw truncated remainder (b == 0 raises "division by zero")
// - modf(a): raw integer and fraction parts
// - frexp(a): exact binary mantissa in [0.5, 1) (raw [32768, 65535]) and exponent of raw value
// - ldexp(a, e): exact shift, saturating; non-integer e raises "number has no integer representation"
// - deg(a), rad(a): one fixMul by the nearest Q48.16 of 180/pi (3754936) and pi/180 (1144)
// - lerp(a, b, t): t == 1 ? b : a + (b - a) * t on moo fixed operators
// - math.pi (205887) and math.huge (INT64_MAX)
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"

#include "MooHost.h"

#include "doctest.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

TEST_SUITE_BEGIN("moo-l4-math-exact-ops");

namespace
{

constexpr int64_t kRawOne = 65536LL;
constexpr int64_t kRawHalf = 32768LL;
constexpr int64_t kRawMax = INT64_MAX;
constexpr int64_t kRawMin = -INT64_MAX;
constexpr int64_t kRawPi = 205887LL;         // nearest Q48.16 to pi (3.14159265358979323846 * 65536 = 205887.416...)
constexpr int64_t kRawDegPerRad = 3754936LL; // nearest Q48.16 to 180/pi (57.29577951308232... * 65536 = 3754936.206...)
constexpr int64_t kRawRadPerDeg = 1144LL;    // nearest Q48.16 to pi/180 (0.017453292519943295... * 65536 = 1143.818...)

struct Lcg
{
    uint64_t s = 0x6d6f6f5f6c345f65ull;
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
};

// Independent 128-bit integer reference operations (never calling moo_number.h or fixed).
int64_t refAdd(int64_t a, int64_t b)
{
    __int128 s = (__int128)a + (__int128)b;
    if (s > (__int128)kRawMax)
        return kRawMax;
    if (s < (__int128)kRawMin)
        return kRawMin;
    return (int64_t)s;
}

int64_t refSub(int64_t a, int64_t b)
{
    __int128 d = (__int128)a - (__int128)b;
    if (d > (__int128)kRawMax)
        return kRawMax;
    if (d < (__int128)kRawMin)
        return kRawMin;
    return (int64_t)d;
}

int64_t refMul(int64_t a, int64_t b)
{
    __int128 prod = (__int128)a * (__int128)b + (__int128)kRawHalf;
    __int128 shifted = prod >> 16;
    if (shifted > (__int128)kRawMax)
        return kRawMax;
    if (shifted < (__int128)kRawMin)
        return kRawMin;
    return (int64_t)shifted;
}

int64_t refFmod(int64_t a, int64_t b)
{
    return b == -1 ? 0 : (a % b);
}

void refModf(int64_t a, int64_t* ip, int64_t* fp)
{
    uint64_t u = a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
    uint64_t f = u & 0xffffu;
    uint64_t i = u - f;
    *ip = a < 0 ? -int64_t(i) : int64_t(i);
    *fp = a < 0 ? -int64_t(f) : int64_t(f);
}

void refFrexp(int64_t a, int64_t* m, int* e)
{
    if (a == 0)
    {
        *m = 0;
        *e = 0;
        return;
    }
    uint64_t u = a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
    int msb = 0;
    for (uint64_t x = u >> 1; x != 0; x >>= 1)
        msb += 1;
    uint64_t mag = msb <= 15 ? (u << (15 - msb)) : (u >> (msb - 15));
    *m = a < 0 ? -int64_t(mag) : int64_t(mag);
    *e = msb - 15;
}

int64_t refLdexp(int64_t a, int e)
{
    if (a == 0 || e == 0)
        return a;
    uint64_t u = a < 0 ? uint64_t(0) - uint64_t(a) : uint64_t(a);
    if (e > 0)
    {
        if (e >= 63 || u > (uint64_t(kRawMax) >> e))
            return a < 0 ? kRawMin : kRawMax;
        uint64_t r = u << e;
        return a < 0 ? -int64_t(r) : int64_t(r);
    }
    uint64_t s = uint64_t(0) - uint64_t(int64_t(e));
    if (s >= 63)
        return 0;
    uint64_t r = u >> s;
    return a < 0 ? -int64_t(r) : int64_t(r);
}

int64_t refDeg(int64_t a)
{
    return refMul(a, kRawDegPerRad);
}

int64_t refRad(int64_t a)
{
    return refMul(a, kRawRadPerDeg);
}

int64_t refLerp(int64_t a, int64_t b, int64_t t)
{
    return t == kRawOne ? b : refAdd(a, refMul(refSub(b, a), t));
}

int compileFuncRef(lua_State* L, const char* source, int optLevel)
{
    lua_CompileOptions opts = {};
    opts.optimizationLevel = optLevel;
    opts.debugLevel = 1;
    size_t bcSize = 0;
    char* bc = luau_compile(source, strlen(source), &opts, &bcSize);
    REQUIRE(bc != nullptr);
    int loadStatus = luau_load(L, "=exact_ops", bc, bcSize, 0);
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

std::pair<int64_t, int64_t> call1Ret2(lua_State* L, int ref, int64_t a)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    int st = lua_pcall(L, 1, 2, 0);
    REQUIRE_MESSAGE(st == 0, "lua_pcall error: " << lua_tostring(L, -1));
    int64_t r1 = lua_tonumber(L, -2);
    int64_t r2 = lua_tonumber(L, -1);
    lua_pop(L, 2);
    return {r1, r2};
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

int64_t call3(lua_State* L, int ref, int64_t a, int64_t b, int64_t c)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    lua_pushnumber(L, b);
    lua_pushnumber(L, c);
    int st = lua_pcall(L, 3, 1, 0);
    REQUIRE_MESSAGE(st == 0, "lua_pcall error: " << lua_tostring(L, -1));
    int64_t r = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return r;
}

std::vector<int64_t> scalarPool()
{
    std::vector<int64_t> v = {
        0,
        1,
        -1,
        2,
        -2,
        kRawHalf - 1,
        -(kRawHalf - 1),
        kRawHalf,
        -kRawHalf,
        kRawHalf + 1,
        -(kRawHalf + 1),
        kRawOne - 1,
        -(kRawOne - 1),
        kRawOne,
        -kRawOne,
        kRawOne + 1,
        -(kRawOne + 1),
        kRawOne + kRawHalf,
        -(kRawOne + kRawHalf),
        2 * kRawOne - 1,
        -(2 * kRawOne - 1),
        kRawPi,
        -kRawPi,
        90 * kRawOne,
        -90 * kRawOne,
        180 * kRawOne,
        -180 * kRawOne,
        (int64_t(1) << 56) + 1,
        -((int64_t(1) << 56) + 1),
        kRawMax - 1,
        kRawMin + 1,
        kRawMax,
        kRawMin,
    };
    for (int k = 0; k < 63; ++k)
    {
        int64_t p = int64_t(uint64_t(1) << k);
        v.push_back(p);
        v.push_back(-p);
    }
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
        v.push_back(rng.nextRaw());
    return v;
}

} // namespace

TEST_CASE("MathExactOpsConstants")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    CHECK_EQ(mooRun(L, "return moo_test.raw(math.pi)"), std::to_string(kRawPi));
    CHECK_EQ(mooRun(L, "return moo_test.raw(math.huge)"), std::to_string(kRawMax));

    lua_close(L);
}

TEST_CASE("MathExactOpsFmod")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    CHECK_EQ(mooRun(L, "local f = math.fmod; local ok, err = pcall(function() return f(1, 0) end); return tostring(ok) .. \"|\" .. tostring(err)"),
        "false|moo:1: division by zero");
    CHECK_EQ(mooRun(L, "local ok, err = pcall(function() return math.fmod(1, 0) end); return tostring(ok) .. \"|\" .. tostring(err)"),
        "false|moo:1: division by zero");

    int libRef = compileFuncRef(L, "local f = math.fmod; return function(a, b) return f(a, b) end", 0);
    int fcRef = compileFuncRef(L, "return function(a, b) return math.fmod(a, b) end", 2);

    std::vector<std::pair<int64_t, int64_t>> pairs = {
        {0, 1},
        {0, -1},
        {3 * kRawOne, 2 * kRawOne},
        {-3 * kRawOne, 2 * kRawOne},
        {3 * kRawOne, -2 * kRawOne},
        {-3 * kRawOne, -2 * kRawOne},
        {(int64_t(1) << 56) + 1, kRawOne},
        {-((int64_t(1) << 56) + 1), kRawOne},
        {kRawMax, kRawOne},
        {kRawMin, kRawOne},
        {kRawMax, kRawMax},
        {kRawMin, kRawMax},
        {kRawMax, -1},
        {kRawMin, -1},
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t a = rng.nextRaw();
        int64_t b = rng.nextRaw();
        if (b == 0)
            b = 1;
        pairs.push_back({a, b});
    }

    for (const auto& p : pairs)
    {
        int64_t a = p.first;
        int64_t b = p.second;
        int64_t expected = refFmod(a, b);
        int64_t gotLib = call2(L, libRef, a, b);
        int64_t gotFc = call2(L, fcRef, a, b);
        CHECK_MESSAGE(gotLib == expected, "fmod lib(" << a << ", " << b << "): got " << gotLib << ", expected " << expected);
        CHECK_MESSAGE(gotFc == expected, "fmod fc(" << a << ", " << b << "): got " << gotFc << ", expected " << expected);
    }

    lua_close(L);
}

TEST_CASE("MathExactOpsModf")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    int libRef = compileFuncRef(L, "local f = math.modf; return function(a) return f(a) end", 0);
    int fcRef = compileFuncRef(L, "return function(a) return math.modf(a) end", 2);

    for (int64_t a : scalarPool())
    {
        int64_t expIp = 0, expFp = 0;
        refModf(a, &expIp, &expFp);
        std::pair<int64_t, int64_t> libRes = call1Ret2(L, libRef, a);
        std::pair<int64_t, int64_t> fcRes = call1Ret2(L, fcRef, a);
        int64_t libIp = libRes.first, libFp = libRes.second;
        int64_t fcIp = fcRes.first, fcFp = fcRes.second;
        CHECK_MESSAGE((libIp == expIp && libFp == expFp),
            "modf lib(" << a << "): got (" << libIp << ", " << libFp << "), expected (" << expIp << ", " << expFp << ")");
        CHECK_MESSAGE((fcIp == expIp && fcFp == expFp),
            "modf fc(" << a << "): got (" << fcIp << ", " << fcFp << "), expected (" << expIp << ", " << expFp << ")");
        CHECK_EQ(expIp + expFp, a);
    }

    lua_close(L);
}

TEST_CASE("MathExactOpsFrexp")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    int libRef = compileFuncRef(L, "local f = math.frexp; return function(a) return f(a) end", 0);
    int fcRef = compileFuncRef(L, "return function(a) return math.frexp(a) end", 2);

    for (int64_t a : scalarPool())
    {
        int64_t expM = 0;
        int expE = 0;
        refFrexp(a, &expM, &expE);
        int64_t expERaw = int64_t(expE) * kRawOne;

        std::pair<int64_t, int64_t> libRes = call1Ret2(L, libRef, a);
        std::pair<int64_t, int64_t> fcRes = call1Ret2(L, fcRef, a);
        int64_t libM = libRes.first, libE = libRes.second;
        int64_t fcM = fcRes.first, fcE = fcRes.second;

        CHECK_MESSAGE((libM == expM && libE == expERaw),
            "frexp lib(" << a << "): got (" << libM << ", " << libE << "), expected (" << expM << ", " << expERaw << ")");
        CHECK_MESSAGE((fcM == expM && fcE == expERaw),
            "frexp fc(" << a << "): got (" << fcM << ", " << fcE << "), expected (" << expM << ", " << expERaw << ")");

        if (a == 0)
        {
            CHECK_EQ(libM, 0);
            CHECK_EQ(libE, 0);
        }
        else
        {
            int64_t mag = libM < 0 ? -libM : libM;
            CHECK_GE(mag, kRawHalf);
            CHECK_LT(mag, kRawOne);
        }
    }

    lua_close(L);
}

TEST_CASE("MathExactOpsLdexp")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    for (const char* expExpr : {"1.5", "-0.5", "moo_test.fromraw(\"1\")"})
    {
        std::string libChunk = std::string("local f = math.ldexp; local ok, err = pcall(f, 1, ") + expExpr +
                               "); return tostring(ok) .. \"|\" .. tostring(err)";
        std::string libOut = mooRun(L, libChunk.c_str());
        CHECK_MESSAGE(libOut.find("number has no integer representation") != std::string::npos, "lib ldexp(" << expExpr << "): " << libOut);

        std::string fcChunk = std::string("local ok, err = pcall(function() return math.ldexp(1, ") + expExpr +
                              ") end); return tostring(ok) .. \"|\" .. tostring(err)";
        std::string fcOut = mooRun(L, fcChunk.c_str());
        CHECK_MESSAGE(fcOut.find("number has no integer representation") != std::string::npos, "fc ldexp(" << expExpr << "): " << fcOut);
    }

    int libRef = compileFuncRef(L, "local f = math.ldexp; return function(a, e) return f(a, e) end", 0);
    int fcRef = compileFuncRef(L, "return function(a, e) return math.ldexp(a, e) end", 2);

    const int edgeExps[] = {-100, -64, -63, -62, -32, -16, -1, 0, 1, 16, 32, 46, 47, 62, 63, 64, 100};
    const int64_t edgeVals[] = {0, 1, -1, kRawHalf, -kRawHalf, kRawOne, -kRawOne, kRawPi, -kRawPi, kRawMax, kRawMin};

    for (int64_t a : edgeVals)
    {
        for (int e : edgeExps)
        {
            int64_t expected = refLdexp(a, e);
            int64_t eRaw = int64_t(e) * kRawOne;
            int64_t gotLib = call2(L, libRef, a, eRaw);
            int64_t gotFc = call2(L, fcRef, a, eRaw);
            CHECK_MESSAGE(gotLib == expected, "ldexp lib(" << a << ", " << e << "): got " << gotLib << ", expected " << expected);
            CHECK_MESSAGE(gotFc == expected, "ldexp fc(" << a << ", " << e << "): got " << gotFc << ", expected " << expected);
        }
    }

    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t a = rng.nextRaw();
        int e = int(int64_t(rng.next() % 161) - 80); // [-80, 80]
        int64_t expected = refLdexp(a, e);
        int64_t eRaw = int64_t(e) * kRawOne;
        int64_t gotLib = call2(L, libRef, a, eRaw);
        int64_t gotFc = call2(L, fcRef, a, eRaw);
        CHECK_MESSAGE(gotLib == expected, "ldexp lib(" << a << ", " << e << "): got " << gotLib << ", expected " << expected);
        CHECK_MESSAGE(gotFc == expected, "ldexp fc(" << a << ", " << e << "): got " << gotFc << ", expected " << expected);
    }

    lua_close(L);
}

TEST_CASE("MathExactOpsDegRad")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    int degLibRef = compileFuncRef(L, "local f = math.deg; return function(a) return f(a) end", 0);
    int degFcRef = compileFuncRef(L, "return function(a) return math.deg(a) end", 2);
    int radLibRef = compileFuncRef(L, "local f = math.rad; return function(a) return f(a) end", 0);
    int radFcRef = compileFuncRef(L, "return function(a) return math.rad(a) end", 2);

    for (int64_t a : scalarPool())
    {
        int64_t expDeg = refDeg(a);
        int64_t gotDegLib = call1(L, degLibRef, a);
        int64_t gotDegFc = call1(L, degFcRef, a);
        CHECK_MESSAGE(gotDegLib == expDeg, "deg lib(" << a << "): got " << gotDegLib << ", expected " << expDeg);
        CHECK_MESSAGE(gotDegFc == expDeg, "deg fc(" << a << "): got " << gotDegFc << ", expected " << expDeg);

        int64_t expRad = refRad(a);
        int64_t gotRadLib = call1(L, radLibRef, a);
        int64_t gotRadFc = call1(L, radFcRef, a);
        CHECK_MESSAGE(gotRadLib == expRad, "rad lib(" << a << "): got " << gotRadLib << ", expected " << expRad);
        CHECK_MESSAGE(gotRadFc == expRad, "rad fc(" << a << "): got " << gotRadFc << ", expected " << expRad);
    }

    lua_close(L);
}

TEST_CASE("MathExactOpsLerp")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    int libRef = compileFuncRef(L, "local f = math.lerp; return function(a, b, t) return f(a, b, t) end", 0);
    int fcRef = compileFuncRef(L, "return function(a, b, t) return math.lerp(a, b, t) end", 2);

    struct Triple
    {
        int64_t a;
        int64_t b;
        int64_t t;
    };
    std::vector<Triple> cases = {
        {0, 0, 0},
        {kRawOne, 5 * kRawOne, 0},
        {kRawOne, 5 * kRawOne, kRawOne},
        {kRawOne, 5 * kRawOne, kRawHalf},
        {kRawOne, 5 * kRawOne, -kRawHalf},
        {kRawOne, 5 * kRawOne, kRawOne + kRawHalf},
        {1, 4, kRawHalf},
        {(int64_t(1) << 56) + 1, -((int64_t(1) << 56) + 1), kRawOne},
        {(int64_t(1) << 56) + 1, -((int64_t(1) << 56) + 1), 0},
        {kRawMax, kRawMin, kRawOne},
        {kRawMin, kRawMax, kRawOne},
        {kRawMax, kRawMin, 0},
        {kRawMin, kRawMax, 0},
        {kRawMax, kRawMin, kRawHalf},
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t a = rng.nextRaw();
        int64_t b = rng.nextRaw();
        int64_t t = (i % 16 == 0) ? kRawOne : (i % 16 == 1) ? 0 : rng.nextRaw();
        cases.push_back({a, b, t});
    }

    for (const Triple& c : cases)
    {
        int64_t expected = refLerp(c.a, c.b, c.t);
        int64_t gotLib = call3(L, libRef, c.a, c.b, c.t);
        int64_t gotFc = call3(L, fcRef, c.a, c.b, c.t);
        CHECK_MESSAGE(gotLib == expected,
            "lerp lib(" << c.a << ", " << c.b << ", " << c.t << "): got " << gotLib << ", expected " << expected);
        CHECK_MESSAGE(gotFc == expected,
            "lerp fc(" << c.a << ", " << c.b << ", " << c.t << "): got " << gotFc << ", expected " << expected);
    }

    lua_close(L);
}

TEST_CASE("MathExactOpsConformanceScript")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    CHECK_EQ(mooRunFile(L, "l3/moo-l4-math-exact-ops.luau"), "OK");

    lua_close(L);
}

TEST_SUITE_END();
