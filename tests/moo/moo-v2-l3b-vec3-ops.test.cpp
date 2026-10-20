// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec3 operators and library of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)",
// "Operations"). Each case compares the bits the VM produces with the fixed C function called directly here on
// 1024 seeded inputs; the saturation case pins the edge of + and -; the library case also runs the expected-
// behaviour script through mooRunFile.
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include "moo_number.h"
#include "moo_vec3.h"

#include "Luau/Compiler.h"

#include "MooHost.h"

#include "doctest.h"

#include <stdint.h>
#include <string.h>

#include <string>

TEST_SUITE_BEGIN("moo-v2-l3b-vec3-ops");

namespace
{

// splitmix64 with a fixed seed: the same 1024 inputs on every platform, no clock and no rand().
struct SplitMix
{
    uint64_t state;

    uint64_t next()
    {
        uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    // a component in [-2^30, 2^30]: every product below stays well inside Q48.16, so the bit comparison is
    // against fixed's ordinary result and the saturation case is the only edge case.
    fixed_t component()
    {
        return fixed_t(next() & ((uint64_t(1) << 30) - 1)) - fixed_t(uint64_t(1) << 29);
    }
};

struct TestState
{
    lua_State* L;

    TestState()
    {
        L = luaL_newstate();
        REQUIRE(L != nullptr);
        luaL_openlibs(L);
        mooOpenTestHelpers(L);
    }

    ~TestState()
    {
        lua_close(L);
    }

    void load(const char* source)
    {
        std::string bytecode = Luau::compile(source);
        REQUIRE(luau_load(L, "=moo-vec3-ops", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }

    // calls the loaded dispatch with a vec3 and a vec3 or a number; leaves the result on the stack
    bool callVec(int kind, fixed_t ax, fixed_t ay, fixed_t az, fixed_t bx, fixed_t by, fixed_t bz, bool secondIsNumber, fixed_t scalar)
    {
        lua_getglobal(L, "moo_vec_op");
        lua_pushinteger(L, kind);
        moo_pushvec3(L, ax, ay, az);
        if (secondIsNumber)
            lua_pushnumber(L, scalar);
        else
            moo_pushvec3(L, bx, by, bz);
        if (lua_pcall(L, 3, 1, 0) != 0)
        {
            lua_pop(L, 1);
            return false;
        }
        return true;
    }
};

void readVec(lua_State* L, fixed_t out[3])
{
    REQUIRE(moo_tovec3(L, -1, out) == 1);
    lua_pop(L, 1);
}

} // namespace

// docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations": a + b, a - b, a * b, a * s, s * a and -a
// are fixed's fixVecAdd/fixVecSub/fixVecMul/fixMulSV/fixVecNeg (with moo's saturating add and sub); each is
// compared component for component on 1024 seeded inputs, and each result is a new object.
TEST_CASE("Vec3OperatorsMatchFixed1024")
{
    TestState s;
    s.load("local function f(k, a, b) "
           "if k == 1 then return a + b elseif k == 2 then return a - b elseif k == 3 then return a * b "
           "elseif k == 4 then return a * b elseif k == 5 then return b * a elseif k == 6 then return -a end "
           "end moo_vec_op = f");

    SplitMix rng{0x6a09e667f3bcc909ull};
    for (int i = 0; i < 1024; ++i)
    {
        fixed_t ax = rng.component(), ay = rng.component(), az = rng.component();
        fixed_t bx = rng.component(), by = rng.component(), bz = rng.component();
        fixed_t scalar = rng.component();
        fixVec3 a = FIX_LITERAL(fixVec3){ax, ay, az};
        fixVec3 b = FIX_LITERAL(fixVec3){bx, by, bz};

        struct Op
        {
            int kind;
            fixVec3 want;
            bool secondIsNumber;
            fixed_t scalar;
        };
        const Op ops[] = {
            {1, fixVecAdd(a, b), false, 0},
            {2, fixVecSub(a, b), false, 0},
            {3, fixVecMul(a, b), false, 0},
            {4, fixMulSV(scalar, a), true, scalar},
            {5, fixMulSV(scalar, a), true, scalar},
            {6, fixVecNeg(a), false, 0},
        };

        for (const Op& op : ops)
        {
            CAPTURE(i);
            CAPTURE(op.kind);
            REQUIRE(s.callVec(op.kind, ax, ay, az, bx, by, bz, op.secondIsNumber, op.scalar));
            fixed_t got[3];
            readVec(s.L, got);
            CHECK(got[0] == op.want.x);
            CHECK(got[1] == op.want.y);
            CHECK(got[2] == op.want.z);
        }

        // every operation allocates a new object (docs/DESIGN.md "vec3, quat and transform (layer 3b)")
        lua_getglobal(s.L, "moo_vec_op");
        lua_pushinteger(s.L, 1);
        moo_pushvec3(s.L, ax, ay, az);
        moo_pushvec3(s.L, 0, 0, 0);
        REQUIRE(lua_pcall(s.L, 3, 1, 0) == 0);
        lua_pushvalue(s.L, -2);
        lua_pushvalue(s.L, -2);
        CHECK(lua_rawequal(s.L, -1, -2) == 0);
        lua_pop(s.L, 3);
    }
}

// docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations": "vec3 == vec3 | component equality", on
// 1024 seeded pairs, through the VM's == rather than rawequal.
TEST_CASE("Vec3EqualityIsByValue1024")
{
    TestState s;
    s.load("local function f(a, b) return a == b end moo_vec_eq = f");

    SplitMix rng{0xbb67ae8584caa73bull};
    for (int i = 0; i < 1024; ++i)
    {
        fixed_t ax = rng.component(), ay = rng.component(), az = rng.component();
        fixed_t bx = i % 2 ? ax : rng.component();
        fixed_t by = i % 2 ? ay : rng.component();
        fixed_t bz = i % 2 ? az : rng.component();

        lua_getglobal(s.L, "moo_vec_eq");
        moo_pushvec3(s.L, ax, ay, az);
        moo_pushvec3(s.L, bx, by, bz);
        REQUIRE(lua_pcall(s.L, 2, 1, 0) == 0);
        bool got = lua_toboolean(s.L, -1) != 0;
        lua_pop(s.L, 1);

        bool want = ax == bx && ay == by && az == bz;
        CAPTURE(i);
        CHECK(got == want);
    }
}

// docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations": dot, cross, length, normalize and the
// Luau aliases create/magnitude call fixed's fixDot/fixCross/fixLength/fixNormalize on 1024 seeded inputs.
TEST_CASE("Vec3LibraryMatchesFixed1024")
{
    TestState s;
    s.load("local function f(k, a, b) "
           "if k == 1 then return vec3.dot(a, b) elseif k == 2 then return vec3.cross(a, b) "
           "elseif k == 3 then return vec3.length(a) elseif k == 4 then return vec3.normalize(a) "
           "elseif k == 5 then return vec3.magnitude(a) end "
           "end moo_vec_lib = f");

    SplitMix rng{0x3c6ef372fe94f82bull};
    for (int i = 0; i < 1024; ++i)
    {
        fixed_t ax = rng.component(), ay = rng.component(), az = rng.component();
        fixed_t bx = rng.component(), by = rng.component(), bz = rng.component();
        if (ax == 0 && ay == 0 && az == 0)
            ax = MOO_NUMBER_ONE;
        fixVec3 a = FIX_LITERAL(fixVec3){ax, ay, az};
        fixVec3 b = FIX_LITERAL(fixVec3){bx, by, bz};

        auto call = [&](int kind) {
            lua_getglobal(s.L, "moo_vec_lib");
            lua_pushinteger(s.L, kind);
            moo_pushvec3(s.L, ax, ay, az);
            moo_pushvec3(s.L, bx, by, bz);
            REQUIRE(lua_pcall(s.L, 3, 1, 0) == 0);
        };

        call(1);
        CHECK(lua_tonumber(s.L, -1) == fixDot(a, b));
        lua_pop(s.L, 1);
        call(2);
        {
            fixed_t got[3];
            readVec(s.L, got);
            fixVec3 want = fixCross(a, b);
            CHECK(got[0] == want.x);
            CHECK(got[1] == want.y);
            CHECK(got[2] == want.z);
        }
        call(3);
        CHECK(lua_tonumber(s.L, -1) == fixLength(a));
        lua_pop(s.L, 1);
        call(4);
        {
            fixed_t got[3];
            readVec(s.L, got);
            fixVec3 want = fixNormalize(a);
            CHECK(got[0] == want.x);
            CHECK(got[1] == want.y);
            CHECK(got[2] == want.z);
        }
        call(5);
        CHECK(lua_tonumber(s.L, -1) == fixLength(a));
        lua_pop(s.L, 1);
    }

    CHECK(mooRun(s.L, "return type(vec3.new(1, 2, 3))") == "vec3");
    CHECK(mooRun(s.L, "return vec3.dot(vec3.new(1, 2, 3), vec3.new(1, 0, 0))") == "1");
    CHECK(mooRun(s.L, "return vec3.dot(vec3.create(1, 2, 3), vec3.new(0, 1, 0))") == "2");
    CHECK(mooRun(s.L, "return tostring(vec3.new(1,2,3) == vec3.create(1,2,3))") == "true");
}

// docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Saturation, stated precisely": + and - follow number's
// rule. Release saturates to the domain edge; a checked build raises "number overflow in '<op>'" at the operation.
TEST_CASE("Vec3SaturationEdges")
{
    TestState s;
    s.load("local function add(a,b) return a + b end local function sub(a,b) return a - b end "
           "moo_vec_add = add moo_vec_sub = sub");

    const fixed_t max = MOO_NUMBER_MAX;
    const fixed_t min = MOO_NUMBER_MIN;

    struct Edge
    {
        const char* name;
        const char* fn;
        fixed_t a[3];
        fixed_t b[3];
        fixed_t want[3];
    };
    const Edge edges[] = {
        {"+", "moo_vec_add", {max, min, 0}, {1, -1, 0}, {max, min, 0}},
        {"-", "moo_vec_sub", {min, max, 0}, {1, -1, 0}, {min, max, 0}},
        {"+", "moo_vec_add", {max - 1, min + 1, 0}, {1, -1, 0}, {max, min, 0}},
    };

    for (const Edge& e : edges)
    {
        CAPTURE(e.name);
        lua_getglobal(s.L, e.fn);
        moo_pushvec3(s.L, e.a[0], e.a[1], e.a[2]);
        moo_pushvec3(s.L, e.b[0], e.b[1], e.b[2]);
        int status = lua_pcall(s.L, 2, 1, 0);
#if MOO_OVERFLOW_ERRORS
        REQUIRE(status != 0);
        CHECK(std::string(lua_tostring(s.L, -1)).find(std::string("number overflow in '") + e.name + "'") != std::string::npos);
        lua_pop(s.L, 1);
#else
        REQUIRE(status == 0);
        fixed_t got[3];
        readVec(s.L, got);
        CHECK(got[0] == e.want[0]);
        CHECK(got[1] == e.want[1]);
        CHECK(got[2] == e.want[2]);
#endif
    }
}

// The expected-behaviour script of this card, in tests/conformance/moo/l3 (docs/layer3/README.md).
TEST_CASE("Vec3OpsConformance")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK(mooRunFile(L, "l3/moo-v2-l3b-vec3-ops.luau") == "OK");
    lua_close(L);
}

TEST_SUITE_END();
