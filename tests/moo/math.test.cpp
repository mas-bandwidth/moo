// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The math library on fixed: exact operations, exp and log, trig and inverse trig, sqrt and noise,
// with the library and fastcall paths giving the same bits (docs/DESIGN.md "math library").

#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"
#include "MooHost.h"
#include "fixed/fixed_int128.h"
#include "doctest.h"
#include "moo_number.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed.h"
#include "MooWide.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include <stdio.h>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

TEST_SUITE_BEGIN("math");

// Layer 4 exact math operations (docs/DESIGN.md "math library"):
// - fmod(a, b): raw truncated remainder (b == 0 raises "division by zero")
// - modf(a): raw integer and fraction parts
// - frexp(a): exact binary mantissa in [0.5, 1) (raw [32768, 65535]) and exponent of raw value
// - ldexp(a, e): exact shift, saturating; non-integer e raises "number has no integer representation"
// - deg(a), rad(a): one fixMul by the nearest Q48.16 of 180/pi (3754936) and pi/180 (1144)
// - lerp(a, b, t): t == 1 ? b : a + (b - a) * t on moo fixed operators
// - math.pi (205887) and math.huge (INT64_MAX)
namespace math_exact_ops
{

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

// Independent 128-bit integer reference operations (never calling moo_number.h or fixed's fixed-point ops; only plain 128-bit integer arithmetic).
int64_t refAdd(int64_t a, int64_t b)
{
    fixInt128 s = fixInt128Add(fixInt128FromI64(a), fixInt128FromI64(b));
    if (fixInt128Gt(s, fixInt128FromI64(kRawMax)))
        return kRawMax;
    if (fixInt128Lt(s, fixInt128FromI64(kRawMin)))
        return kRawMin;
    return fixInt128ToI64(s);
}

int64_t refSub(int64_t a, int64_t b)
{
    fixInt128 d = fixInt128Sub(fixInt128FromI64(a), fixInt128FromI64(b));
    if (fixInt128Gt(d, fixInt128FromI64(kRawMax)))
        return kRawMax;
    if (fixInt128Lt(d, fixInt128FromI64(kRawMin)))
        return kRawMin;
    return fixInt128ToI64(d);
}

int64_t refMul(int64_t a, int64_t b)
{
    fixInt128 prod = fixInt128Add(fixInt128MulI64(a, b), fixInt128FromI64(kRawHalf));
    fixInt128 shifted = fixInt128Shr(prod, 16);
    if (fixInt128Gt(shifted, fixInt128FromI64(kRawMax)))
        return kRawMax;
    if (fixInt128Lt(shifted, fixInt128FromI64(kRawMin)))
        return kRawMin;
    return fixInt128ToI64(shifted);
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

TEST_CASE("MathConstants")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    CHECK_EQ(mooRun(L, "return moo_test.raw(math.pi)"), std::to_string(kRawPi));
    CHECK_EQ(mooRun(L, "return moo_test.raw(math.huge)"), std::to_string(kRawMax));

    lua_close(L);
}

TEST_CASE("MathFmod")
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

TEST_CASE("MathModf")
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

TEST_CASE("MathFrexp")
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

TEST_CASE("MathLdexp")
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

TEST_CASE("MathDegRad")
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

TEST_CASE("MathLerp")
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

TEST_CASE("MathScript")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    CHECK_EQ(mooRunFile(L, "spec/math.luau"), "OK");

    lua_close(L);
}

} // namespace math_exact_ops

// Layer 4 exp/log family (docs/DESIGN.md, "math library"): fixed composition through fixExp2/fixLog2.
// Each library function and its bytecode fastcall must return the same Q48.16 bits; edge cases and
// 4096 seeded inputs also pin the documented scaling, saturation, and named log-domain errors.
namespace math_exp_log
{

namespace
{

constexpr fixed_t kLog2E = 94548;          // nearest Q48.16: (1 / ln(2)) * 65536 = 94548.4621996991...
constexpr fixed_t kInvLog2Ten = 19728;     // nearest Q48.16: (ln(2) / ln(10)) * 65536 = 19728.3017958347...
constexpr fixed_t kRawRange = 64 * MOO_NUMBER_ONE;

struct Lcg
{
    uint64_t state = 0x6d6f6f5f6c345f65ull;
    uint64_t next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state ^ (state >> 29);
    }
};

int compileFuncRef(lua_State* L, const std::string& source)
{
    Luau::CompileOptions options;
    options.optimizationLevel = 2;
    std::string bytecode = Luau::compile(source, options);
    REQUIRE(luau_load(L, "=math-exp-log", bytecode.data(), bytecode.size(), 0) == 0);
    REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    return ref;
}

int64_t call1(lua_State* L, int ref, int64_t a)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    int status = lua_pcall(L, 1, 1, 0);
    REQUIRE_MESSAGE(status == 0, "math call failed: " << lua_tostring(L, -1));
    int64_t result = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return result;
}

int64_t call2(lua_State* L, int ref, int64_t a, int64_t b)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    lua_pushnumber(L, b);
    int status = lua_pcall(L, 2, 1, 0);
    REQUIRE_MESSAGE(status == 0, "math call failed: " << lua_tostring(L, -1));
    int64_t result = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return result;
}

fixed_t expRef(fixed_t x)
{
    return fixExp2(fixMul(x, kLog2E));
}

// Divide the exponent by two before fixExp2, keeping each intermediate representable through sinh/cosh saturation.
fixed_t halfExpRef(fixed_t x)
{
    return fixExp2(moo_numsub(fixMul(x, kLog2E), MOO_NUMBER_ONE));
}

fixed_t sinhRef(fixed_t x)
{
    return moo_numsub(halfExpRef(x), halfExpRef(moo_numunm(x)));
}

fixed_t coshRef(fixed_t x)
{
    return moo_numadd(halfExpRef(x), halfExpRef(moo_numunm(x)));
}

fixed_t tanhRef(fixed_t x)
{
    fixed_t magnitude = x < 0 ? moo_numunm(x) : x;
    fixed_t twice = moo_numadd(magnitude, magnitude);
    fixed_t inverse = expRef(moo_numunm(twice));
    fixed_t result = moo_numdiv(moo_numsub(MOO_NUMBER_ONE, inverse), moo_numadd(MOO_NUMBER_ONE, inverse));
    return x < 0 ? moo_numunm(result) : result;
}

fixed_t logRef(fixed_t x)
{
    return moo_numdiv(fixLog2(x), kLog2E);
}

fixed_t log10Ref(fixed_t x)
{
    return fixMul(fixLog2(x), kInvLog2Ten);
}

fixed_t logBaseRef(fixed_t x, fixed_t base)
{
    if (base == 2 * MOO_NUMBER_ONE)
        return fixLog2(x);
    if (base == 10 * MOO_NUMBER_ONE)
        return log10Ref(x);
    return moo_numdiv(fixLog2(x), fixLog2(base));
}

std::vector<int64_t> signedInputs()
{
    std::vector<int64_t> values = {
        MOO_NUMBER_MIN, MOO_NUMBER_MAX, -40 * MOO_NUMBER_ONE, 40 * MOO_NUMBER_ONE,
        -10 * MOO_NUMBER_ONE, 10 * MOO_NUMBER_ONE, -MOO_NUMBER_ONE, MOO_NUMBER_ONE,
        -1, 0, 1,
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t raw = int64_t(rng.next() % uint64_t(2 * kRawRange + 1)) - kRawRange;
        values.push_back(raw);
    }
    return values;
}

std::vector<int64_t> positiveInputs()
{
    std::vector<int64_t> values = {1, MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE, 10 * MOO_NUMBER_ONE, MOO_NUMBER_MAX};
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
        values.push_back(1 + int64_t(rng.next() % uint64_t(kRawRange)));
    return values;
}

std::vector<std::pair<int64_t, int64_t>> positivePairs()
{
    std::vector<std::pair<int64_t, int64_t>> values = {
        {MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE},
        {8 * MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE},
        {100 * MOO_NUMBER_ONE, 10 * MOO_NUMBER_ONE},
        {MOO_NUMBER_MAX, MOO_NUMBER_ONE + 1},
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t x = 1 + int64_t(rng.next() % uint64_t(kRawRange));
        int64_t base = 2 * MOO_NUMBER_ONE + int64_t(rng.next() % uint64_t(kRawRange));
        values.push_back({x, base});
    }
    return values;
}

template<typename Ref>
void checkUnary(lua_State* L, const char* name, Ref reference, const std::vector<int64_t>& values)
{
    int library = compileFuncRef(L, "local f = math." + std::string(name) + "; return function(x) return f(x) end");
    int fastcall = compileFuncRef(L, "return function(x) return math." + std::string(name) + "(x) end");
    unsigned mismatches = 0;
    std::string first;
    for (int64_t x : values)
    {
        int64_t expected = reference(x);
        int64_t lib = call1(L, library, x);
        int64_t fc = call1(L, fastcall, x);
        if (lib != expected || fc != expected)
        {
            if (mismatches++ == 0)
                first = " x=" + std::to_string(x) + " expected=" + std::to_string(expected) +
                    " library=" + std::to_string(lib) + " fastcall=" + std::to_string(fc);
        }
    }
    CHECK_MESSAGE(mismatches == 0, name << ": " << mismatches << " mismatches;" << first);
    lua_unref(L, library);
    lua_unref(L, fastcall);
}

void checkLogBase(lua_State* L)
{
    int library = compileFuncRef(L, "local f = math.log; return function(x, b) return f(x, b) end");
    int fastcall = compileFuncRef(L, "return function(x, b) return math.log(x, b) end");
    unsigned mismatches = 0;
    std::string first;
    for (const auto& value : positivePairs())
    {
        int64_t expected = logBaseRef(value.first, value.second);
        int64_t lib = call2(L, library, value.first, value.second);
        int64_t fc = call2(L, fastcall, value.first, value.second);
        if (lib != expected || fc != expected)
        {
            if (mismatches++ == 0)
                first = " x=" + std::to_string(value.first) + " base=" + std::to_string(value.second) +
                    " expected=" + std::to_string(expected) + " library=" + std::to_string(lib) +
                    " fastcall=" + std::to_string(fc);
        }
    }
    CHECK_MESSAGE(mismatches == 0, "log(x, base): " << mismatches << " mismatches;" << first);
    lua_unref(L, library);
    lua_unref(L, fastcall);
}

struct OracleRecord
{
    int64_t a = 0;
    int64_t b = 0;
    int64_t ideal = 0;
};

uint64_t fnv1a(const std::vector<unsigned char>& bytes, size_t length)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool readOracleVarint(const std::vector<unsigned char>& bytes, size_t& pos, size_t limit, int64_t& value)
{
    uint64_t encoded = 0;
    for (unsigned shift = 0; shift < 70; shift += 7)
    {
        if (pos >= limit)
            return false;
        unsigned char byte = bytes[pos++];
        if (shift == 63 && byte > 1)
            return false;
        encoded |= uint64_t(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0)
        {
            value = int64_t((encoded >> 1) ^ (uint64_t(0) - (encoded & 1)));
            return true;
        }
    }
    return false;
}

bool readOracleU32(const std::vector<unsigned char>& bytes, size_t& pos, size_t limit, uint32_t& value)
{
    if (limit - pos < 4)
        return false;
    value = 0;
    for (int i = 0; i < 4; ++i)
        value |= uint32_t(bytes[pos++]) << (8 * i);
    return true;
}

bool readOracleU64(const std::vector<unsigned char>& bytes, size_t& pos, size_t limit, uint64_t& value)
{
    if (limit - pos < 8)
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value |= uint64_t(bytes[pos++]) << (8 * i);
    return true;
}

std::string oraclePath()
{
#ifdef LUAU_CONFORMANCE_SOURCE_DIR
    return std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/../moo/vectors/math-oracle.bin";
#else
    return "tests/moo/vectors/math-oracle.bin";
#endif
}

bool loadOracle(std::unordered_map<std::string, std::vector<OracleRecord>>& records, std::string& error)
{
    std::ifstream input(oraclePath(), std::ios::binary);
    if (!input)
    {
        error = "cannot open " + oraclePath();
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() < 28 || std::string(bytes.begin(), bytes.begin() + 8) != "MOOMATH1")
    {
        error = "short file or bad magic";
        return false;
    }
    size_t end = bytes.size() - 8;
    size_t checksumPos = end;
    uint64_t checksum = 0;
    if (!readOracleU64(bytes, checksumPos, bytes.size(), checksum) || checksum != fnv1a(bytes, end))
    {
        error = "bad fnv1a trailer";
        return false;
    }

    size_t pos = 8;
    uint32_t version = 0, headerSize = 0, functionCount = 0;
    if (!readOracleU32(bytes, pos, end, version) || version != 1 ||
        !readOracleU32(bytes, pos, end, headerSize) || headerSize > end - pos)
    {
        error = "bad version or header";
        return false;
    }
    pos += headerSize;
    if (!readOracleU32(bytes, pos, end, functionCount))
    {
        error = "missing function count";
        return false;
    }

    for (uint32_t fi = 0; fi < functionCount; ++fi)
    {
        if (pos >= end)
        {
            error = "truncated function";
            return false;
        }
        unsigned nameSize = bytes[pos++];
        if (nameSize > end - pos)
        {
            error = "truncated name";
            return false;
        }
        std::string name(bytes.begin() + pos, bytes.begin() + pos + nameSize);
        pos += nameSize;
        if (end - pos < 2)
        {
            error = "truncated arity";
            return false;
        }
        unsigned arity = bytes[pos++];
        ++pos; // exhaustive flag
        uint64_t count = 0, payloadSize = 0;
        if ((arity != 1 && arity != 2) || !readOracleU64(bytes, pos, end, count) ||
            !readOracleU64(bytes, pos, end, payloadSize) || payloadSize > end - pos)
        {
            error = name + ": bad block header";
            return false;
        }
        size_t payloadEnd = pos + size_t(payloadSize);
        int64_t a = 0, b = 0, ideal = 0;
        uint64_t seen = 0;
        while (pos < payloadEnd)
        {
            int64_t da = 0, db = 0, di = 0;
            if (!readOracleVarint(bytes, pos, payloadEnd, da) ||
                (arity == 2 && !readOracleVarint(bytes, pos, payloadEnd, db)) ||
                !readOracleVarint(bytes, pos, payloadEnd, di))
            {
                error = name + ": bad record";
                return false;
            }
            a += da;
            b += db;
            ideal += di;
            if (name == "exp" || name == "log" || name == "log10" || name == "sinh" || name == "cosh" || name == "tanh")
                records[name].push_back({a, b, ideal});
            ++seen;
        }
        if (pos != payloadEnd || seen != count)
        {
            error = name + ": payload/count mismatch";
            return false;
        }
    }
    return pos == end;
}

fixed_t oracleRef(const std::string& name, fixed_t x)
{
    if (name == "exp")
        return expRef(x);
    if (name == "log")
        return logRef(x);
    if (name == "log10")
        return log10Ref(x);
    if (name == "sinh")
        return sinhRef(x);
    if (name == "cosh")
        return coshRef(x);
    return tanhRef(x);
}

} // namespace

TEST_CASE("MathExpLogLibraryFastcallAndComposition")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    const std::vector<int64_t> signedValues = signedInputs();
    const std::vector<int64_t> positiveValues = positiveInputs();
    checkUnary(L, "exp", expRef, signedValues);
    checkUnary(L, "sinh", sinhRef, signedValues);
    checkUnary(L, "cosh", coshRef, signedValues);
    checkUnary(L, "tanh", tanhRef, signedValues);
    checkUnary(L, "log", logRef, positiveValues);
    checkUnary(L, "log10", log10Ref, positiveValues);
    checkLogBase(L);

    lua_close(L);
}

TEST_CASE("MathExpLogNamedDomainErrors")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* scripts[] = {
        "local ok, err = pcall(function() return math.log(0) end); return tostring(ok) .. '|' .. tostring(err)",
        "local f = math.log; local ok, err = pcall(function() return f(-1) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log(1, 0) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log(1, -1) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log10(0) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log10(-1) end); return tostring(ok) .. '|' .. tostring(err)",
    };
    for (const char* script : scripts)
    {
        std::string result = mooRun(L, script);
        bool matched = result.rfind("false|", 0) == 0 &&
            result.find("math.log of a non-positive number") != std::string::npos;
        CHECK_MESSAGE(matched, result);
    }

    const char* division[] = {
        "local ok, err = pcall(function() return math.log(1, 1) end); return tostring(ok) .. '|' .. tostring(err)",
        "local f = math.log; local ok, err = pcall(function() return f(1, 1) end); return tostring(ok) .. '|' .. tostring(err)",
    };
    for (const char* script : division)
    {
        std::string result = mooRun(L, script);
        bool matched = result.rfind("false|", 0) == 0 && result.find("division by zero") != std::string::npos;
        CHECK_MESSAGE(matched, result);
    }

    lua_close(L);
}

TEST_CASE("MathExpLogOracleMaximumErrors")
{
    std::unordered_map<std::string, std::vector<OracleRecord>> records;
    std::string error;
    REQUIRE_MESSAGE(loadOracle(records, error), error);
    for (const char* name : {"exp", "log", "log10", "sinh", "cosh", "tanh"})
    {
        const auto found = records.find(name);
        REQUIRE(found != records.end());
        uint64_t maximum = 0;
        int64_t at = 0, got = 0, ideal = 0;
        for (const OracleRecord& record : found->second)
        {
            int64_t result = oracleRef(name, record.a);
            uint64_t magnitude = result < record.ideal ? uint64_t(record.ideal) - uint64_t(result)
                                                        : uint64_t(result) - uint64_t(record.ideal);
            if (magnitude > maximum)
            {
                maximum = magnitude;
                at = record.a;
                got = result;
                ideal = record.ideal;
            }
        }
        printf("moo-math-exp-log-oracle %s samples=%zu max_err=%llu at=%lld got=%lld ideal=%lld\n", name,
            found->second.size(), (unsigned long long)maximum, (long long)at, (long long)got, (long long)ideal);
    }
}

} // namespace math_exp_log

// Layer 4 inverse trig (docs/DESIGN.md "math library"): math.atan2, math.atan, math.asin and math.acos
// on fixed, and their bytecode fastcalls. The library and the fastcall share one definition
// (VM/src/lmathlib.cpp), so they give the same bits; both must match fixed C directly. Out-of-domain
// |x| > 1 in asin and acos raises "math.asin domain" / "math.acos domain" on both paths.
namespace math_inverse_trig
{

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

} // namespace math_inverse_trig

// Layer 4 trig (docs/DESIGN.md "math library"): math.sin, math.cos and math.tan on fixed, and their
// bytecode fastcalls. The library and the fastcall share one definition (VM/src/lmathlib.cpp), so they
// give the same bits; both must match fixed's C directly. Each function is checked at the multiples of
// pi/2, at zero, at the domain limits and on 4096 seeded inputs. The moo-l4-math-oracle vectors have not
// landed, so the maximum-error comparison is deferred to that card; here the reference is fixed's C.
namespace math_trig
{

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

} // namespace math_trig

// DESIGN.md "math library": sqrt from fixSqrt, exact, rounded toward zero; negative is a named error.
// Each result r of math.sqrt(a) must satisfy r*r <= a*2^16 < (r+1)*(r+1) in raw Q48.16 units, checked
// in 128-bit integer arithmetic (never by squaring in double). Exhaustive over raw inputs 0..2^24, plus
// 4096 seeded large inputs across the full range.
namespace math_sqrt
{

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


// Exhaustive over raw inputs 0..2^24 through the VM, each result checked in 128-bit integer arithmetic.
TEST_CASE("MathSqrtExhaustiveSmall")
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
TEST_CASE("MathSqrtLargeSeeded")
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
TEST_CASE("MathSqrtNegativeError")
{
    // regular call path: through a local alias so the bytecode cannot take a fastcall
    CHECK(runScript("local s = math.sqrt local ok, err = pcall(function() return s(-1) end) return tostring(ok) .. '|' .. tostring(err)").find("false|") == 0);
    CHECK(runScript("local s = math.sqrt local ok, err = pcall(function() return s(-1) end) return tostring(err)").find("math.sqrt of a negative number") != std::string::npos);

    // fastcall path: a direct call the compiler may compile to a fastcall
    CHECK(runScript("local ok, err = pcall(function() return math.sqrt(-1) end) return tostring(ok) .. '|' .. tostring(err)").find("false|") == 0);
    CHECK(runScript("local ok, err = pcall(function() return math.sqrt(-1) end) return tostring(err)").find("math.sqrt of a negative number") != std::string::npos);
}

// Wiring: known results on both paths, and zero.
TEST_CASE("MathSqrtKnownValues")
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

} // namespace math_sqrt

// Layer 4 math.noise (docs/DESIGN.md "math library"): this test-only float implementation is
// Luau's upstream reference and never runs inside the VM.
extern fixed_t moo_math_noise(fixed_t x, fixed_t y, fixed_t z);
namespace math_noise
{

namespace
{
const unsigned char kFloatPerlinHash[257] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,
    190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,
    125, 136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,
    41,  55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130,
    116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207,
    206, 59,  227, 47,  16,  58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,
    172, 9,   129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,  242, 193, 238, 210, 144,
    12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,
    127, 4,   150, 254, 138, 236, 205, 93,  222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180, 151
};

const float kFloatPerlinGrad[16][3] = {
    {1, 1, 0},
    {-1, 1, 0},
    {1, -1, 0},
    {-1, -1, 0},
    {1, 0, 1},
    {-1, 0, 1},
    {1, 0, -1},
    {-1, 0, -1},
    {0, 1, 1},
    {0, -1, 1},
    {0, 1, -1},
    {0, -1, -1},
    {1, 1, 0},
    {0, -1, 1},
    {-1, 1, 0},
    {0, -1, -1}
};


float floatFade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float floatLerp(float t, float a, float b) { return a + t * (b - a); }
float floatGrad(int h, float x, float y, float z)
{
    const float* g = kFloatPerlinGrad[h & 15];
    return g[0] * x + g[1] * y + g[2] * z;
}
float floatNoise(float x, float y, float z)
{
    float xflr = floorf(x), yflr = floorf(y), zflr = floorf(z);
    int xi = int(xflr) & 255, yi = int(yflr) & 255, zi = int(zflr) & 255;
    float xf = x - xflr, yf = y - yflr, zf = z - zflr;
    float u = floatFade(xf), v = floatFade(yf), w = floatFade(zf);
    const unsigned char* p = kFloatPerlinHash;
    int a = (p[xi] + yi) & 255, aa = (p[a] + zi) & 255, ab = (p[a + 1] + zi) & 255;
    int b = (p[xi + 1] + yi) & 255, ba = (p[b] + zi) & 255, bb = (p[b + 1] + zi) & 255;
    float la = floatLerp(u, floatGrad(p[aa], xf, yf, zf), floatGrad(p[ba], xf - 1, yf, zf));
    float lb = floatLerp(u, floatGrad(p[ab], xf, yf - 1, zf), floatGrad(p[bb], xf - 1, yf - 1, zf));
    float la1 = floatLerp(u, floatGrad(p[aa + 1], xf, yf, zf - 1), floatGrad(p[ba + 1], xf - 1, yf, zf - 1));
    float lb1 = floatLerp(u, floatGrad(p[ab + 1], xf, yf - 1, zf - 1), floatGrad(p[bb + 1], xf - 1, yf - 1, zf - 1));
    return floatLerp(w, floatLerp(v, la, lb), floatLerp(v, la1, lb1));
}
struct Lcg
{
    uint64_t s = 0x6d6f6f5f6c345f6eull;
    uint64_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return s ^ (s >> 29); }
    int64_t nextRaw() { return int64_t(next() % (uint64_t(512) * 65536)) - int64_t(256 * 65536); }
};
uint64_t hashRaw(uint64_t h, int64_t raw)
{
    uint64_t u = uint64_t(raw);
    for (int i = 0; i < 8; ++i) { h ^= uint8_t(u >> (8 * i)); h *= 1099511628211ull; }
    return h;
}
std::array<int64_t, 3> sample(Lcg& r) { return {r.nextRaw(), r.nextRaw(), r.nextRaw()}; }
} // namespace


TEST_CASE("MathNoiseFrozen64AndLatticeZeros")
{
    Lcg rng;
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 64; ++i)
    {
        auto p = sample(rng);
        int64_t out = moo_math_noise(p[0], p[1], p[2]);
        CHECK(out >= -65536); CHECK(out <= 65536);
        hash = hashRaw(hash, out);
    }
    INFO("frozen raw hash: 0x" << std::hex << hash);
    CHECK(hash == 12883597645287845634ull); // FNV-1a of 64 reviewed raw outputs
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            for (int z = -2; z <= 2; ++z)
                CHECK(moo_math_noise(int64_t(x) * 65536, int64_t(y) * 65536, int64_t(z) * 65536) == 0);
}

TEST_CASE("MathNoiseMaximumDifferenceFromLuauFloat")
{
    Lcg rng;
    double maximum = 0.0;
    for (int i = 0; i < 65536; ++i)
    {
        auto p = sample(rng);
        float x = float(double(p[0]) / 65536.0), y = float(double(p[1]) / 65536.0), z = float(double(p[2]) / 65536.0);
        double fixed = double(moo_math_noise(p[0], p[1], p[2])) / 65536.0;
        maximum = std::max(maximum, std::abs(fixed - double(floatNoise(x, y, z))));
    }
    std::cout << "MathNoiseMaximumDifferenceFromLuauFloat=" << maximum << "\n";
    CHECK(maximum < 0.001);
}

TEST_CASE("MathNoiseLibraryFastcallBitExact")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    Lcg rng;
    for (int i = 0; i < 64; ++i)
    {
        auto p = sample(rng);
        std::string arguments = "moo_test.fromraw('" + std::to_string(p[0]) + "'), moo_test.fromraw('" + std::to_string(p[1]) +
            "'), moo_test.fromraw('" + std::to_string(p[2]) + "')";
        std::string direct = "return moo_test.raw(math.noise(" + arguments + "))";
        std::string library = "local f = math.noise; return moo_test.raw(f(" + arguments + "))";
        int64_t expected = moo_math_noise(p[0], p[1], p[2]);
        CHECK(std::stoll(mooRun(L, direct.c_str())) == expected);
        CHECK(std::stoll(mooRun(L, library.c_str())) == expected);
    }

    lua_close(L);
}

} // namespace math_noise

TEST_SUITE_END();
