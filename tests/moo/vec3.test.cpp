// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// vec3: the object, its layout and tag, value equality, table keys, GC, and the operators and library
// against fixed's C (docs/DESIGN.md "vec3, quat and transform").

#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo_number.h"
#include "moo_vec3.h"
#include "Luau/Compiler.h"
#include "MooHost.h"
#include "doctest.h"
#include "ltm.h"
#include "moo_format.h"
#include "moo.h"
#include "lapi.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "moo_parse.h"
#include "ScopedFlags.h"

#include <stdint.h>
#include <string.h>
#include <string>
#include <stddef.h>

TEST_SUITE_BEGIN("vec3");

// moo: the vec3 operators and library of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)",
// "Operations"). Each case compares the bits the VM produces with the fixed C function called directly here on
// 1024 seeded inputs; the saturation case pins the edge of + and -; the library case also runs the expected-
// behaviour script through mooRunFile.
namespace vec3_ops
{

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
    }

    // every operation allocates exactly one new 40-byte object (docs/DESIGN.md "vec3, quat and transform (layer 3b)";
    // docs/history/layer3b/moo-v2-l3b-vec3-object.md, decision 2); with the collector stopped, totalbytes shows it
    lua_gc(s.L, LUA_GCSTOP, 0);
    lua_getglobal(s.L, "moo_vec_op");
    lua_pushinteger(s.L, 1);
    moo_pushvec3(s.L, 1, 2, 3);
    moo_pushvec3(s.L, 0, 0, 0);
    size_t before = lua_totalbytes(s.L, 0);
    REQUIRE(lua_pcall(s.L, 3, 1, 0) == 0);
    CHECK(lua_totalbytes(s.L, 0) - before == sizeof(MooVec3));
    lua_pop(s.L, 1);
    lua_gc(s.L, LUA_GCRESTART, 0);
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
// rule. Release saturates to the domain edge; a checked build raises "number overflow in '<op>'" at the operation,
// and only where release saturates: an edge that lands exactly on the domain end is exact in both builds.
TEST_CASE("Vec3SaturationEdges")
{
    TestState s;
    s.load("local function add(a,b) return a + b end local function sub(a,b) return a - b end "
           "local function mul(a,b) return a * b end moo_vec_add = add moo_vec_sub = sub moo_vec_mul = mul");

    const fixed_t max = MOO_NUMBER_MAX;
    const fixed_t min = MOO_NUMBER_MIN;
    const fixed_t one = MOO_NUMBER_ONE;

    struct Edge
    {
        const char* name;
        const char* fn;
        fixed_t a[3];
        fixed_t b[3];
        fixed_t want[3];
        bool saturates;
    };
    const Edge edges[] = {
        {"+", "moo_vec_add", {max, min, 0}, {1, -1, 0}, {max, min, 0}, true},
        {"+", "moo_vec_add", {0, 0, max}, {0, 0, 1}, {0, 0, max}, true}, // one component alone saturates
        {"-", "moo_vec_sub", {min, max, 0}, {1, -1, 0}, {min, max, 0}, true},
        {"-", "moo_vec_sub", {0, min, 0}, {0, 1, 0}, {0, min, 0}, true},
        {"*", "moo_vec_mul", {max, 0, 0}, {2 * one, 0, 0}, {max, 0, 0}, true},
        {"*", "moo_vec_mul", {0, min, 0}, {0, 2 * one, 0}, {0, min, 0}, true},
        // exactly on the edge: exact, no error in either build
        {"+", "moo_vec_add", {max - 1, min + 1, 0}, {1, -1, 0}, {max, min, 0}, false},
        {"-", "moo_vec_sub", {min + 1, max - 1, 0}, {1, -1, 0}, {min, max, 0}, false},
        {"*", "moo_vec_mul", {max, min, 0}, {one, one, one}, {max, min, 0}, false},
    };

    for (const Edge& e : edges)
    {
        CAPTURE(e.name);
        CAPTURE(e.a[0]);
        lua_getglobal(s.L, e.fn);
        moo_pushvec3(s.L, e.a[0], e.a[1], e.a[2]);
        moo_pushvec3(s.L, e.b[0], e.b[1], e.b[2]);
        int status = lua_pcall(s.L, 2, 1, 0);
        if (MOO_OVERFLOW_ERRORS && e.saturates)
        {
            REQUIRE(status != 0);
            CHECK(std::string(lua_tostring(s.L, -1)).find(std::string("number overflow in '") + e.name + "'") != std::string::npos);
            lua_pop(s.L, 1);
            continue;
        }
        REQUIRE(status == 0);
        fixed_t got[3];
        readVec(s.L, got);
        CHECK(got[0] == e.want[0]);
        CHECK(got[1] == e.want[1]);
        CHECK(got[2] == e.want[2]);
    }

    // vec3 * number and number * vec3 follow the same rule
    for (int order = 0; order < 2; ++order)
    {
        lua_getglobal(s.L, "moo_vec_mul");
        if (order == 0)
        {
            moo_pushvec3(s.L, 0, max, 0);
            lua_pushnumber(s.L, -2 * one);
        }
        else
        {
            lua_pushnumber(s.L, -2 * one);
            moo_pushvec3(s.L, 0, max, 0);
        }
        int status = lua_pcall(s.L, 2, 1, 0);
        CAPTURE(order);
        if (MOO_OVERFLOW_ERRORS)
        {
            REQUIRE(status != 0);
            CHECK(std::string(lua_tostring(s.L, -1)).find("number overflow in '*'") != std::string::npos);
            lua_pop(s.L, 1);
        }
        else
        {
            REQUIRE(status == 0);
            fixed_t got[3];
            readVec(s.L, got);
            CHECK(got[0] == 0);
            CHECK(got[1] == min);
            CHECK(got[2] == 0);
        }
    }
}

namespace
{

// A component anywhere in the domain [-INT64_MAX, INT64_MAX], or one of the edges: zero, one raw unit, one, 2^47
// raw, and the two domain ends with their neighbours.
fixed_t fullRangeComponent(SplitMix& rng)
{
    static const fixed_t edges[] = {0, 1, -1, MOO_NUMBER_ONE, -MOO_NUMBER_ONE, fixed_t(1) << 47, -(fixed_t(1) << 47),
        MOO_NUMBER_MAX, MOO_NUMBER_MIN, MOO_NUMBER_MAX - 1, MOO_NUMBER_MIN + 1};
    uint64_t r = rng.next();
    if ((r & 7) == 0)
        return edges[(r >> 3) % (sizeof(edges) / sizeof(edges[0]))];
    // a random magnitude: shift a random value right by 1..63 so small and large components both occur
    int64_t v = int64_t(rng.next() >> (1 + (r >> 3) % 63));
    return (r & 8) ? -v : v; // v <= INT64_MAX, so -v is in the domain
}

// fixed's fixVecAdd at this pin is a plain int64 add; the same add done without undefined behaviour, so the
// reversed witness below can show that the corpus reaches inputs where it and moo's saturating add differ.
fixed_t wrappingAdd(fixed_t a, fixed_t b)
{
    return fixed_t(uint64_t(a) + uint64_t(b));
}

} // namespace

// docs/DESIGN.md "Saturation, stated precisely": over the full domain, + and - are moo_numadd/moo_numsub per
// component, * is fixVecMul/fixMulSV (saturating fixMul) and unary - is fixVecNeg; a checked build raises the
// named error exactly when a component saturates. 4096 seeded inputs.
TEST_CASE("Vec3OperatorsFullRange4096")
{
    TestState s;
    s.load("local function f(k, a, b) "
           "if k == 1 then return a + b elseif k == 2 then return a - b elseif k == 3 then return a * b "
           "elseif k == 4 then return a * b elseif k == 5 then return b * a elseif k == 6 then return -a end "
           "end moo_vec_op = f");

    SplitMix rng{0x510e527fade682d1ull};
    int saturated[7] = {};
    int wrapDiffers = 0;
    for (int i = 0; i < 4096; ++i)
    {
        fixed_t ax = fullRangeComponent(rng), ay = fullRangeComponent(rng), az = fullRangeComponent(rng);
        fixed_t bx = fullRangeComponent(rng), by = fullRangeComponent(rng), bz = fullRangeComponent(rng);
        fixed_t scalar = fullRangeComponent(rng);
        fixVec3 a = FIX_LITERAL(fixVec3){ax, ay, az};
        fixVec3 b = FIX_LITERAL(fixVec3){bx, by, bz};

        struct Op
        {
            int kind;
            fixVec3 want;
            bool saturates;
            bool secondIsNumber;
        };
        const Op ops[] = {
            {1, FIX_LITERAL(fixVec3){moo_numadd(ax, bx), moo_numadd(ay, by), moo_numadd(az, bz)},
                moo_numaddsaturates(ax, bx) || moo_numaddsaturates(ay, by) || moo_numaddsaturates(az, bz), false},
            {2, FIX_LITERAL(fixVec3){moo_numsub(ax, bx), moo_numsub(ay, by), moo_numsub(az, bz)},
                moo_numsubsaturates(ax, bx) || moo_numsubsaturates(ay, by) || moo_numsubsaturates(az, bz), false},
            {3, fixVecMul(a, b), moo_nummulsaturates(ax, bx) || moo_nummulsaturates(ay, by) || moo_nummulsaturates(az, bz),
                false},
            {4, fixMulSV(scalar, a),
                moo_nummulsaturates(scalar, ax) || moo_nummulsaturates(scalar, ay) || moo_nummulsaturates(scalar, az), true},
            {5, fixMulSV(scalar, a),
                moo_nummulsaturates(scalar, ax) || moo_nummulsaturates(scalar, ay) || moo_nummulsaturates(scalar, az), true},
            {6, fixVecNeg(a), false, false},
        };

        if (wrappingAdd(ax, bx) != moo_numadd(ax, bx))
            ++wrapDiffers;

        for (const Op& op : ops)
        {
            CAPTURE(i);
            CAPTURE(op.kind);
            bool ok = s.callVec(op.kind, ax, ay, az, bx, by, bz, op.secondIsNumber, scalar);
            if (op.saturates)
                ++saturated[op.kind];
            if (MOO_OVERFLOW_ERRORS && op.saturates)
            {
                CHECK(!ok);
                continue;
            }
            REQUIRE(ok);
            fixed_t got[3];
            readVec(s.L, got);
            CHECK(got[0] == op.want.x);
            CHECK(got[1] == op.want.y);
            CHECK(got[2] == op.want.z);
        }
    }

    // Reversed witnesses: the corpus does reach saturation for every saturating operator, and reaches inputs where
    // fixed's plain int64 add would wrap and so differ from moo's saturating add. A corpus of small components
    // would pass the comparison above while proving nothing about the edges.
    CHECK(saturated[1] > 100);
    CHECK(saturated[2] > 100);
    CHECK(saturated[3] > 100);
    CHECK(saturated[4] > 100);
    CHECK(wrapDiffers > 100);
}

// docs/DESIGN.md "Operations": dot, length and normalize are fixed's functions over the full domain (their sums
// are exact at 128 bits, so no input is out of their range); dot and length saturate as fixed's do under
// FIX_SATURATE. cross is compared where fixed's two products and their difference stay inside the domain: outside
// it fixCross's plain int64 subtraction is the open gap of "Saturation, stated precisely".
TEST_CASE("Vec3LibraryFullRange4096")
{
    TestState s;
    s.load("local function f(k, a, b) "
           "if k == 1 then return vec3.dot(a, b) elseif k == 2 then return vec3.cross(a, b) "
           "elseif k == 3 then return vec3.length(a) elseif k == 4 then return vec3.normalize(a) end "
           "end moo_vec_lib = f");

    SplitMix rng{0x9b05688c2b3e6c1full};
    int crossCompared = 0;
    int dotSaturated = 0;
    for (int i = 0; i < 4096; ++i)
    {
        fixed_t ax = fullRangeComponent(rng), ay = fullRangeComponent(rng), az = fullRangeComponent(rng);
        fixed_t bx = fullRangeComponent(rng), by = fullRangeComponent(rng), bz = fullRangeComponent(rng);
        fixVec3 a = FIX_LITERAL(fixVec3){ax, ay, az};
        fixVec3 b = FIX_LITERAL(fixVec3){bx, by, bz};

        auto call = [&](int kind) {
            lua_getglobal(s.L, "moo_vec_lib");
            lua_pushinteger(s.L, kind);
            moo_pushvec3(s.L, ax, ay, az);
            moo_pushvec3(s.L, bx, by, bz);
            REQUIRE(lua_pcall(s.L, 3, 1, 0) == 0);
        };
        CAPTURE(i);

        call(1);
        CHECK(lua_tonumber(s.L, -1) == fixDot(a, b));
        if (fixDot(a, b) == MOO_NUMBER_MAX || fixDot(a, b) == MOO_NUMBER_MIN)
            ++dotSaturated;
        lua_pop(s.L, 1);

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

        // cross only where fixCross is defined: each difference of two fixMul results in the domain
        fixed_t p[6] = {fixMul(ay, bz), fixMul(az, by), fixMul(az, bx), fixMul(ax, bz), fixMul(ax, by), fixMul(ay, bx)};
        bool inRange = true;
        for (int k = 0; k < 6; k += 2)
        {
            if (moo_numsubsaturates(p[k], p[k + 1]))
                inRange = false;
        }
        if (inRange)
        {
            ++crossCompared;
            call(2);
            fixed_t got[3];
            readVec(s.L, got);
            fixVec3 want = fixCross(a, b);
            CHECK(got[0] == want.x);
            CHECK(got[1] == want.y);
            CHECK(got[2] == want.z);
        }
    }
    CHECK(crossCompared > 1000);
    CHECK(dotSaturated > 100);

    // the zero vector: length 0, and normalize returns zero, as fixNormalize documents
    CHECK(mooRun(s.L, "return vec3.length(vec3.new())") == "0");
    CHECK(mooRun(s.L, "return vec3.normalize(vec3.new()) == vec3.new(0, 0, 0)") == "true");
}

// A vec3 reads its components as v.x, v.y, v.z (and X, Y, Z, as Luau's vector) and prints vec3(x, y, z) with the
// exact formatter; a vec3 is immutable, so assigning a component is refused.
TEST_CASE("Vec3ComponentsAndTostring")
{
    TestState s;
    CHECK(mooRun(s.L, "return tostring(vec3.new(1, -2.5, 0.1))") == "vec3(1, -2.5, 0.1)");
    CHECK(mooRun(s.L, "return tostring(vec3.new())") == "vec3(0, 0, 0)");
    CHECK(mooRun(s.L, "local v = vec3.new(1, -2.5, 0.1) return v.x .. ' ' .. v.y .. ' ' .. v.z") == "1 -2.5 0.1");
    CHECK(mooRun(s.L, "local v = vec3.new(1, -2.5, 0.1) return v.X .. ' ' .. v.Y .. ' ' .. v.Z") == "1 -2.5 0.1");
    CHECK(mooRun(s.L, "local v = vec3.new(1, 2, 3) local k = 'y' return v[k]") == "2");

    // the exact raw components cross the C API both ways
    moo_pushvec3(s.L, MOO_NUMBER_MAX, MOO_NUMBER_MIN, 1);
    lua_setglobal(s.L, "edge");
    CHECK(mooRun(s.L, "return edge.x == moo_test.fromraw('9223372036854775807') and edge.y == -edge.x "
                      "and edge.z == moo_test.fromraw('1')") == "true");

    // moo_tovec3 refuses anything that is not a vec3 and writes nothing
    lua_pushnumber(s.L, MOO_NUMBER_ONE);
    fixed_t out[3] = {7, 7, 7};
    CHECK(moo_tovec3(s.L, -1, out) == 0);
    CHECK(out[0] == 7);
    CHECK(out[1] == 7);
    CHECK(out[2] == 7);
    lua_pop(s.L, 1);
}

// Every operation outside the table of docs/DESIGN.md "Operations" is refused with the ordinary error, by name.
TEST_CASE("Vec3Refusals")
{
    TestState s;
    struct Refusal
    {
        const char* source;
        const char* error;
    };
    const Refusal refusals[] = {
        {"return v + 1", "attempt to perform arithmetic (add) on vec3 and number"},
        {"return 1 + v", "attempt to perform arithmetic (add) on number and vec3"},
        {"return v - 1", "attempt to perform arithmetic (sub) on vec3 and number"},
        {"return 1 - v", "attempt to perform arithmetic (sub) on number and vec3"},
        {"return v / v", "attempt to perform arithmetic (div) on vec3"},
        {"return v / 2", "attempt to perform arithmetic (div) on vec3 and number"},
        {"return v // v", "attempt to perform arithmetic (idiv) on vec3"},
        {"return v % v", "attempt to perform arithmetic (mod) on vec3"},
        {"return v ^ 2", "attempt to perform arithmetic (pow) on vec3 and number"},
        {"return v * '2'", "attempt to perform arithmetic (mul) on vec3 and string"},
        {"return v * true", "attempt to perform arithmetic (mul) on vec3 and boolean"},
        {"return v < v", "attempt to compare vec3 < vec3"},
        {"return v <= v", "attempt to compare vec3 <= vec3"},
        {"return #v", "attempt to get length of a vec3 value"},
        {"return v .. ''", "attempt to concatenate vec3 with string"},
        {"return v.w", "attempt to index vec3 with 'w'"},
        {"return v.xy", "attempt to index vec3 with 'xy'"},
        {"return v[1]", "attempt to index vec3 with number"},
        {"v.x = 1", "attempt to index vec3 with 'x'"},
        {"return v()", "attempt to call a vec3 value"},
        {"for _ in v do end", "attempt to iterate over a vec3 value"},
        {"return vec3.dot(v, 1)", "invalid argument #2 to 'dot' (vec3 expected, got number)"},
        {"return vec3.dot(1, v)", "invalid argument #1 to 'dot' (vec3 expected, got number)"},
        {"return vec3.cross(v)", "missing argument #2 to 'cross' (vec3 expected)"},
        {"return vec3.length()", "missing argument #1 to 'length' (vec3 expected)"},
        {"return vec3.magnitude({})", "invalid argument #1 to 'magnitude' (vec3 expected, got table)"},
        {"return vec3.normalize('a')", "invalid argument #1 to 'normalize' (vec3 expected, got string)"},
        {"return vec3.new('a')", "invalid argument #1 to 'new' (number expected, got string)"},
        {"return vec3.new(1, 2, {})", "invalid argument #3 to 'new' (number expected, got table)"},
    };
    for (const Refusal& r : refusals)
    {
        CAPTURE(r.source);
        std::string source = std::string("local v = vec3.new(1, 2, 3) ") + r.source;
        std::string got = mooRun(s.L, source.c_str());
        CHECK(got.rfind("error: ", 0) == 0);
        CHECK(got.find(r.error) != std::string::npos);
    }

    // a vec3 has no metatable, so == never calls __eq and no script can give it methods
    CHECK(mooRun(s.L, "return getmetatable(vec3.new()) == nil") == "true");
}

// The vec3 expected-behaviour script, tests/conformance/moo/spec/vec3.luau.
TEST_CASE("Vec3Script")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK(mooRunFile(L, "spec/vec3.luau") == "OK");
    lua_close(L);
}

} // namespace vec3_ops

// moo: the vec3 representation decision of layer 3b (docs/history/layer3b/moo-v2-l3b-vec3-object.md). Nothing creates a
// vec3 yet, so these cases pin only what the decision fixes statically: the object layout, the tag's value and its name.
namespace vec3_object
{

// Note, decision 2: the GC header as every object declares it, then fixed_t x, y, z; 40 bytes, 24 of payload.
TEST_CASE("Vec3ObjectLayout")
{
    CHECK(sizeof(GCheader) == 16);
    CHECK(offsetof(MooVec3, gcseq) == offsetof(GCheader, gcseq));
    CHECK(offsetof(MooVec3, tt) == offsetof(GCheader, tt));
    CHECK(offsetof(MooVec3, marked) == offsetof(GCheader, marked));
    CHECK(offsetof(MooVec3, memcat) == offsetof(GCheader, memcat));

    CHECK(offsetof(MooVec3, x) == 16);
    CHECK(offsetof(MooVec3, y) == 24);
    CHECK(offsetof(MooVec3, z) == 32);
    CHECK(sizeof(MooVec3) == 40);
    CHECK(alignof(MooVec3) == 8);
    CHECK(sizeof(MooVec3) - offsetof(MooVec3, x) == sizeof(fixVec3));
    CHECK(sizeof(fixed_t) == 8);
}

// Note, decision 1: LUA_TVEC3 is right after LUA_TOBJECT (and after the LUA_VECTOR_DOUBLE == 1 arm moo does not
// build), so in moo's build it is 14. It is a TValue tag and collectable (at or above LUA_TSTRING), and its name is
// "vec3". moo: quat follows it at 15 and transform at 16, so LUA_TDEADKEY is 17 and LUA_T_COUNT 17
// (layer 3b, docs/history/layer3b/moo-v2-l3b-quat-object.md, docs/history/layer3b/moo-v2-l3b-transform.md). An upstream merge that adds a type there makes this case fail, so the
// note is read again.
TEST_CASE("Vec3TagSlotIsAfterObject")
{
    CHECK(LUA_VECTOR_DOUBLE == 0);
    CHECK(int(LUA_TOBJECT) == 13);
    CHECK(int(LUA_TVEC3) == 14);
    CHECK(int(LUA_TVEC3) == int(LUA_TOBJECT) + 1);
    CHECK(int(LUA_TQUAT) == int(LUA_TVEC3) + 1);
    CHECK(int(LUA_TTRANSFORM) == int(LUA_TQUAT) + 1);
    CHECK(int(LUA_TDEADKEY) == int(LUA_TTRANSFORM) + 1);
    CHECK(int(LUA_T_COUNT) == 17);
    CHECK(int(LUA_TVEC3) < int(LUA_T_COUNT));
    CHECK(int(LUA_TVEC3) >= int(LUA_TSTRING));
    // a table key's tag lives in TKey::tt, now 5 bits: LUA_TDEADKEY is 17 (note, decision 1, item 4; quat widens it)
    CHECK(int(LUA_TDEADKEY) <= 31);
    CHECK(strcmp(luaT_typenames[LUA_TVEC3], "vec3") == 0);
}

// Note, decision 5: tostring writes three moo_numtostr results into MOO_VEC3_TOSTR_MAX bytes; the longest
// numbers are the ends of the range with every fraction digit.
TEST_CASE("Vec3TostringBound")
{
    const fixed_t widest[] = {INT64_MAX, -INT64_MAX, -(int64_t(0x7fffffffffff) << 16) - 1, -1, 1};
    size_t longest = 0;
    for (fixed_t n : widest)
    {
        char buf[64];
        size_t len = size_t(moo_numtostr(buf, n) - buf);
        longest = len > longest ? len : longest;
    }
    CHECK(longest <= 34);
    CHECK(MOO_VEC3_TOSTR_MAX == 5 + 3 * 34 + 2 * 2 + 1);
}

} // namespace vec3_object

// moo: layer 3b vec3 GC object kind, layout, value equality, tostring and leaf GC
// (docs/DESIGN.md "vec3, quat and transform (layer 3b)"; docs/history/layer3b/moo-v2-l3b-vec3-object.md,
// "Card moo-v2-l3b-vec3-object-gc").
namespace vec3_gc
{

namespace
{

void pushVec3(lua_State* L, fixed_t x, fixed_t y, fixed_t z)
{
    MooVec3* v = moo_vec3_new(L, x, y, z);
    setvec3value(L, L->top, v);
    incr_top(L);
}

std::string formatVec3(const MooVec3* v)
{
    char buf[MOO_VEC3_TOSTR_MAX];
    char* end = moo_vec3_tostring(buf, v);
    REQUIRE(end >= buf);
    REQUIRE(size_t(end - buf) <= MOO_VEC3_TOSTR_MAX);
    return std::string(buf, size_t(end - buf));
}

struct Vec3HeapCount
{
    size_t count = 0;
};

bool countVec3Visitor(void* context, lua_Page* page, GCObject* gco)
{
    (void)page;
    Vec3HeapCount* c = static_cast<Vec3HeapCount*>(context);
    if (gco->gch.tt == LUA_TVEC3)
        c->count++;
    return false;
}

size_t countLiveVec3(lua_State* L)
{
    Vec3HeapCount c;
    luaM_visitgco(L, &c, countVec3Visitor);
    return c.count;
}

} // namespace

// Allocation through luaM_newgcofixed + luaC_init: tag is LUA_TVEC3, type() is "vec3", creation number advances
// by 1 per vec3, and totalbytes advances by sizeof(MooVec3) == 40 per vec3.
TEST_CASE("Vec3CreateAndLayoutInVm")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    luaC_fullgc(L);
    size_t bytesBefore = L->global->totalbytes;
    uint64_t seqBefore = L->global->gcseq;

    MooVec3* v1 = moo_vec3_new(L, 1 * MOO_NUMBER_ONE, -2 * MOO_NUMBER_ONE, 3 * MOO_NUMBER_ONE);
    REQUIRE(v1 != nullptr);
    CHECK(v1->tt == LUA_TVEC3);
    CHECK(v1->memcat == L->activememcat);
    CHECK(v1->x == 1 * MOO_NUMBER_ONE);
    CHECK(v1->y == -2 * MOO_NUMBER_ONE);
    CHECK(v1->z == 3 * MOO_NUMBER_ONE);
    CHECK(v1->gcseq == seqBefore);
    CHECK(L->global->totalbytes == bytesBefore + sizeof(MooVec3));

    setvec3value(L, L->top, v1);
    incr_top(L);
    CHECK(ttisvec3(L->top - 1));
    CHECK(vec3value(L->top - 1) == v1);
    CHECK(gco2vec3(gcvalue(L->top - 1)) == v1);
    CHECK(lua_type(L, -1) == LUA_TVEC3);
    CHECK(strcmp(lua_typename(L, lua_type(L, -1)), "vec3") == 0);
    CHECK(moo_objectseq(L, -1) == seqBefore);

    MooVec3* v2 = moo_vec3_new(L, 4 * MOO_NUMBER_ONE, 5 * MOO_NUMBER_ONE, 6 * MOO_NUMBER_ONE);
    setvec3value(L, L->top, v2);
    incr_top(L);
    CHECK(v2->gcseq == seqBefore + 1);
    CHECK(moo_objectseq(L, -1) == seqBefore + 1);
    CHECK(L->global->totalbytes == bytesBefore + 2 * sizeof(MooVec3));

    lua_setglobal(L, "v2");
    lua_setglobal(L, "v1");
    CHECK(mooRun(L, "return type(v1) .. \":\" .. type(v2)") == "vec3:vec3");

    lua_close(L);
}

// Equality is by the three raw Q48.16 components (moo_vec3_equal), never by pointer or creation number.
// luaO_rawequalObj, luaO_rawequalKey, lua_rawequal and script rawequal all use moo_vec3_equal.
TEST_CASE("Vec3EqualByValue")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    pushVec3(L, 10, -20, 30);
    pushVec3(L, 10, -20, 30);
    pushVec3(L, 11, -20, 30);
    pushVec3(L, 10, -19, 30);
    pushVec3(L, 10, -20, 31);

    const TValue* a = L->top - 5;
    const TValue* b = L->top - 4;
    const TValue* dx = L->top - 3;
    const TValue* dy = L->top - 2;
    const TValue* dz = L->top - 1;

    CHECK(vec3value(a) != vec3value(b));
    CHECK(vec3value(a)->gcseq != vec3value(b)->gcseq);
    CHECK(moo_vec3_equal(vec3value(a), vec3value(b)) == 1);
    CHECK(moo_vec3_equal(vec3value(a), vec3value(dx)) == 0);
    CHECK(moo_vec3_equal(vec3value(a), vec3value(dy)) == 0);
    CHECK(moo_vec3_equal(vec3value(a), vec3value(dz)) == 0);

    CHECK(luaO_rawequalObj(a, b) == 1);
    CHECK(luaO_rawequalObj(a, dx) == 0);
    CHECK(luaO_rawequalObj(a, dy) == 0);
    CHECK(luaO_rawequalObj(a, dz) == 0);

    LuaNode node = {};
    setnilvalue(&node.val);
    setnodekey(L, &node, a);
    CHECK(luaO_rawequalKey(&node.key, b) == 1);
    CHECK(luaO_rawequalKey(&node.key, dx) == 0);
    CHECK(luaO_rawequalKey(&node.key, dy) == 0);
    CHECK(luaO_rawequalKey(&node.key, dz) == 0);

    CHECK(lua_rawequal(L, -5, -4) == 1);
    CHECK(lua_rawequal(L, -5, -3) == 0);
    CHECK(lua_rawequal(L, -5, -2) == 0);
    CHECK(lua_rawequal(L, -5, -1) == 0);

    lua_setglobal(L, "dz");
    lua_setglobal(L, "dy");
    lua_setglobal(L, "dx");
    lua_setglobal(L, "b");
    lua_setglobal(L, "a");
    CHECK(mooRun(L, "return tostring(rawequal(a, b)) .. \",\" .. tostring(rawequal(a, dx)) .. \",\" .. tostring(rawequal(a, dy)) .. \",\" .. tostring(rawequal(a, dz)) .. \",\" .. tostring(rawequal(a, 1))") ==
          "true,false,false,false,false");

    lua_close(L);
}

// moo_vec3_tostring formats "vec3(x, y, z)" with each component through moo_numtostr, never printing an
// address or creation number, and fits within MOO_VEC3_TOSTR_MAX for the widest Q48.16 values.
TEST_CASE("Vec3TostringFormat")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    fixed_t one = MOO_NUMBER_ONE;
    fixed_t minusTwoHalf = -2 * MOO_NUMBER_ONE - MOO_NUMBER_ONE / 2;
    fixed_t tenth = 0;
    REQUIRE(moo_numparse("0.1", &tenth) == MOO_PARSE_OK);

    MooVec3* v = moo_vec3_new(L, one, minusTwoHalf, tenth);
    setvec3value(L, L->top, v);
    incr_top(L);
    CHECK(formatVec3(v) == "vec3(1, -2.5, 0.1)");

    MooVec3* zero = moo_vec3_new(L, 0, 0, 0);
    setvec3value(L, L->top, zero);
    incr_top(L);
    CHECK(formatVec3(zero) == "vec3(0, 0, 0)");

    MooVec3* unit = moo_vec3_new(L, 1, -1, 65536);
    setvec3value(L, L->top, unit);
    incr_top(L);
    CHECK(formatVec3(unit) == "vec3(0.00002, -0.00002, 1)");

    const fixed_t worst = -(int64_t(0x7fffffffffff) << 16) - 1;
    MooVec3* wide = moo_vec3_new(L, worst, INT64_MAX, -INT64_MAX);
    setvec3value(L, L->top, wide);
    incr_top(L);
    std::string wideStr = formatVec3(wide);
    CHECK(wideStr.rfind("vec3(", 0) == 0);
    CHECK(wideStr.back() == ')');
    CHECK(wideStr.find('#') == std::string::npos);
    CHECK(wideStr.size() <= MOO_VEC3_TOSTR_MAX);

    lua_close(L);
}

// GC treats MooVec3 as a leaf: unreachable objects are swept and freed, a churn of 10^5 objects runs
// destructor-free and leaves zero live vec3s after a full collect, weak tables never clear a vec3 key or
// value, and cleartable turns an emptied weak-table entry's collectable key into LUA_TDEADKEY before sweep.
TEST_CASE("Vec3GcLeafAndChurn100k")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    luaC_fullgc(L);
    CHECK(countLiveVec3(L) == 0);
    size_t baselineBytes = L->global->totalbytes;

    // 1. Unreachable vec3 objects are swept and freed on full GC.
    pushVec3(L, 1, 2, 3);
    pushVec3(L, 4, 5, 6);
    CHECK(countLiveVec3(L) == 2);
    CHECK(L->global->totalbytes == baselineBytes + 2 * sizeof(MooVec3));
    lua_pop(L, 1);
    luaC_fullgc(L);
    CHECK(countLiveVec3(L) == 1);
    CHECK(vec3value(L->top - 1)->x == 1);
    lua_pop(L, 1);
    luaC_fullgc(L);
    CHECK(countLiveVec3(L) == 0);
    CHECK(L->global->totalbytes == baselineBytes);

    // 2. Destructor-free churn of 10^5 vec3 objects under incremental GC.
    for (int i = 0; i < 100000; ++i)
    {
        MooVec3* v = moo_vec3_new(L, fixed_t(i), fixed_t(-i), fixed_t(i * 3));
        setvec3value(L, L->top, v);
        incr_top(L);
        // luaC_step requires the debt to be due (lgc.cpp asserts totalbytes >= GCthreshold in a checked build)
        if (i % 64 == 63 && L->global->totalbytes >= L->global->GCthreshold)
            luaC_step(L, true);
        CHECK(L->global->dtorqueue == nullptr);
        lua_pop(L, 1);
    }
    luaC_fullgc(L);
    CHECK(countLiveVec3(L) == 0);
    CHECK(L->global->totalbytes == baselineBytes);
    CHECK(L->global->dtorqueue == nullptr);

    // 3. Weak tables ("k", "v", "kv") never clear a vec3 key or value (isobjcleared marks LUA_TVEC3 like a string).
    for (const char* mode : {"k", "v", "kv"})
    {
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, mode);
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);

        LuaTable* h = hvalue(L->top - 1);
        pushVec3(L, 100, 200, 300);
        MooVec3* vk = vec3value(L->top - 1);
        TValue* slot = luaH_set(L, h, L->top - 1);
        lua_pop(L, 1);

        pushVec3(L, 400, 500, 600);
        MooVec3* vv = vec3value(L->top - 1);
        setobj2t(L, slot, L->top - 1);
        lua_pop(L, 1);

        luaC_fullgc(L);
        CHECK(countLiveVec3(L) == 2);

        TValue keyVal;
        setvec3value(L, &keyVal, vk);
        const TValue* got = luaH_get(h, &keyVal);
        REQUIRE(ttisvec3(got));
        CHECK(vec3value(got) == vv);

        lua_pop(L, 1);
        luaC_fullgc(L);
        CHECK(countLiveVec3(L) == 0);
    }

    // 4. An emptied entry in a "k" or "kv" weak table turns its collectable key into LUA_TDEADKEY during cleartable.
    for (const char* mode : {"k", "kv"})
    {
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, mode);
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);

        LuaTable* h = hvalue(L->top - 1);
        pushVec3(L, 7, 8, 9);
        TValue* slot = luaH_set(L, h, L->top - 1);
        setnvalue(slot, 1 * MOO_NUMBER_ONE);
        // Empty the entry before dropping the key reference on the stack.
        setnilvalue(slot);
        lua_pop(L, 1);

        luaC_fullgc(L);
        CHECK(countLiveVec3(L) == 0);
        int deadKeys = 0;
        for (int i = 0; i < sizenode(h); ++i)
        {
            LuaNode* n = gnode(h, i);
            CHECK(int(ttype(gkey(n))) != LUA_TVEC3);
            if (ttype(gkey(n)) == LUA_TDEADKEY)
                deadKeys++;
        }
        CHECK(deadKeys == 1);

        lua_pop(L, 1);
        luaC_fullgc(L);
    }

    lua_close(L);
}

} // namespace vec3_gc

// moo: layer 3b vec3 as a table key: component hashing, main-position and key-equality routing, and the
// deterministic iteration order of a vec3-keyed table
// (docs/DESIGN.md "vec3, quat and transform (layer 3b)"; docs/history/layer3b/moo-v2-l3b-vec3-object.md,
// "Card moo-v2-l3b-vec3-object-key").
LUAU_DYNAMIC_FASTFLAG(LuauSplitTableLookups)

namespace vec3_key
{



namespace
{

void pushVec3(lua_State* L, fixed_t x, fixed_t y, fixed_t z)
{
    MooVec3* v = moo_vec3_new(L, x, y, z);
    setvec3value(L, L->top, v);
    incr_top(L);
}

// FNV-1a 64-bit over the byte stream
uint64_t fnv(const std::string& s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (char c : s)
    {
        h ^= uint8_t(c);
        h *= 0x100000001b3ull;
    }
    return h;
}

void appendI64(std::string& s, int64_t v)
{
    for (int k = 0; k < 8; ++k)
        s.push_back(static_cast<char>(v >> (8 * k)));
}

// Walks the table with lua_next (the table is at stack index 1), appending each vec3 key's three raw
// components to out. Returns the number of non-nil entries visited.
int collectOrder(lua_State* L, std::string& out)
{
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, 1))
    {
        const TValue* k = L->top - 2;
        REQUIRE(ttisvec3(k));
        appendI64(out, vec3value(k)->x);
        appendI64(out, vec3value(k)->y);
        appendI64(out, vec3value(k)->z);
        count++;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return count;
}

// The pinned traversal order and entry count of the table Vec3KeyTableOrderPinned builds. The split lookup path
// (DFFlag::LuauSplitTableLookups) must produce the same order as the default path.
const uint64_t kVec3KeyOrderHash = 465732153082891246ull;
const int kVec3KeyOrderCount = 13571;

struct Vec3KeyOrderResult
{
    uint64_t hash;
    int count;
};

Vec3KeyOrderResult runVec3KeyOrder()
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_newtable(L);
    LuaTable* t = hvalue(L->top - 1);

    for (int i = 0; i < 10000; ++i)
    {
        pushVec3(L, fixed_t(i), fixed_t(i * 2), fixed_t(i * 3));
        TValue* slot = luaH_set(L, t, L->top - 1);
        setnvalue(slot, fixed_t(i));
        lua_pop(L, 1);
    }

    for (int i = 0; i < 10000; i += 7)
    {
        pushVec3(L, fixed_t(i), fixed_t(i * 2), fixed_t(i * 3));
        TValue* slot = luaH_set(L, t, L->top - 1);
        setnilvalue(slot);
        lua_pop(L, 1);
    }

    for (int i = 0; i < 5000; ++i)
    {
        pushVec3(L, fixed_t(100000 + i), fixed_t(i), fixed_t(-i));
        TValue* slot = luaH_set(L, t, L->top - 1);
        setnvalue(slot, fixed_t(i));
        lua_pop(L, 1);
    }

    std::string order;
    int count = collectOrder(L, order);
    uint64_t hash = fnv(order);

    lua_close(L);
    return Vec3KeyOrderResult{hash, count};
}

} // namespace

// Two distinct equal vec3 are one key: t[a]=1 makes t[b]==1, and next visits one entry. A component that
// differs makes a separate key.
TEST_CASE("Vec3KeyEqualsAreOneKey")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_newtable(L);
    LuaTable* t = hvalue(L->top - 1);

    pushVec3(L, 5 * MOO_NUMBER_ONE, -6 * MOO_NUMBER_ONE, 7 * MOO_NUMBER_ONE);
    TValue* slot = luaH_set(L, t, L->top - 1);
    REQUIRE(slot != luaO_nilobject);
    setnvalue(slot, 1 * MOO_NUMBER_ONE);
    const MooVec3* first = vec3value(L->top - 1); // stays alive as the key
    lua_pop(L, 1);

    pushVec3(L, 5 * MOO_NUMBER_ONE, -6 * MOO_NUMBER_ONE, 7 * MOO_NUMBER_ONE);
    REQUIRE(vec3value(L->top - 1) != first); // a distinct object with equal components
    const TValue* got = luaH_get(t, L->top - 1);
    REQUIRE(ttisnumber(got));
    CHECK(nvalue(got) == 1 * MOO_NUMBER_ONE);
    lua_pop(L, 1);

    pushVec3(L, 5 * MOO_NUMBER_ONE, -6 * MOO_NUMBER_ONE, 8 * MOO_NUMBER_ONE);
    const TValue* got2 = luaH_get(t, L->top - 1);
    CHECK(got2 == luaO_nilobject);
    lua_pop(L, 1);

    std::string order;
    CHECK(collectOrder(L, order) == 1);

    lua_close(L);
}

// moo_vec3_hash is frozen and deterministic: it depends only on the three raw components, so equal vec3 hash
// equal, and the values below are pinned by hash (docs/history/layer3b/moo-v2-l3b-vec3-object.md, decision 4).
TEST_CASE("Vec3HashFrozen")
{
    MooVec3 v = {};
    v.x = 0; v.y = 0; v.z = 0;
    CHECK(moo_vec3_hash(&v) == 0u);
    v.x = 1; v.y = 1; v.z = 1;
    CHECK(moo_vec3_hash(&v) == 1431246824u);
    v.x = 1 * MOO_NUMBER_ONE; v.y = -1 * MOO_NUMBER_ONE; v.z = 2 * MOO_NUMBER_ONE;
    CHECK(moo_vec3_hash(&v) == 3949486901u);
    v.x = INT64_MAX; v.y = INT64_MAX; v.z = INT64_MAX;
    CHECK(moo_vec3_hash(&v) == 2201428105u);
    v.x = -INT64_MAX; v.y = -INT64_MAX; v.z = -INT64_MAX;
    CHECK(moo_vec3_hash(&v) == 1639996852u);
}

// A vec3-keyed table iterates in the same order on every run: insert 10^4 vec3 keys, delete every 7th, insert a
// further batch to force a rehash, and pin the traversal order by a frozen hash. The same sequence run twice in
// one process yields the same hash, and the absolute value is frozen.
TEST_CASE("Vec3KeyTableOrderPinned")
{
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        Vec3KeyOrderResult r = runVec3KeyOrder();
        INFO("repeat=" << repeat << " orderHash=" << r.hash << " count=" << r.count);
        CHECK(r.count == kVec3KeyOrderCount);
        CHECK(r.hash == kVec3KeyOrderHash);
    }
}

// The split lookup path (DFFlag::LuauSplitTableLookups) must route a vec3 through its component hash and value
// equality, not through the deprecated path: with two distinct equal vec3 a and b, luaH_get(t, b) finds t[a] and
// luaH_set(t, b) returns the same slot. The pinned order above must also hold with the flag on.
TEST_CASE("Vec3KeySplitLookupRouting")
{
    ScopedFastFlag splitTableLookups{DFFlag::LuauSplitTableLookups, true};

    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_newtable(L);
    LuaTable* t = hvalue(L->top - 1);

    // a at stack index 2
    pushVec3(L, 5 * MOO_NUMBER_ONE, -6 * MOO_NUMBER_ONE, 7 * MOO_NUMBER_ONE);
    TValue* aSlot = luaH_set(L, t, L->top - 1);
    REQUIRE(aSlot != luaO_nilobject);
    setnvalue(aSlot, 1 * MOO_NUMBER_ONE);

    // b, a distinct equal vec3, at stack index 3
    pushVec3(L, 5 * MOO_NUMBER_ONE, -6 * MOO_NUMBER_ONE, 7 * MOO_NUMBER_ONE);
    REQUIRE(vec3value(L->top - 1) != vec3value(L->top - 2));

    const TValue* got = luaH_get(t, L->top - 1);
    REQUIRE(ttisnumber(got));
    CHECK(nvalue(got) == 1 * MOO_NUMBER_ONE);
    CHECK(got == aSlot);

    TValue* bSlot = luaH_set(L, t, L->top - 1);
    CHECK(bSlot == aSlot);

    lua_pop(L, 1);
    lua_pop(L, 1);

    Vec3KeyOrderResult r = runVec3KeyOrder();
    INFO("split orderHash=" << r.hash << " count=" << r.count);
    CHECK(r.count == kVec3KeyOrderCount);
    CHECK(r.hash == kVec3KeyOrderHash);

    lua_close(L);
}

// A vec3 key survives a GC cycle while its value is referenced: the table's hash node keeps the key object alive,
// so a full collect does not free it, and lookup by an equal vec3 still finds the value.
TEST_CASE("Vec3KeySurvivesGc")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_newtable(L);
    LuaTable* t = hvalue(L->top - 1);

    pushVec3(L, 1 * MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE, 3 * MOO_NUMBER_ONE);
    TValue* slot = luaH_set(L, t, L->top - 1);
    setnvalue(slot, 42 * MOO_NUMBER_ONE);
    lua_pop(L, 1);

    luaC_fullgc(L);

    pushVec3(L, 1 * MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE, 3 * MOO_NUMBER_ONE);
    const TValue* got = luaH_get(t, L->top - 1);
    REQUIRE(ttisnumber(got));
    CHECK(nvalue(got) == 42 * MOO_NUMBER_ONE);
    lua_pop(L, 1);

    lua_close(L);
}

// The wrapper next(t, k) used to catch the "invalid key to 'next'" error under pcall. The table is at stack
// index 1 and the current key at index 2. On success it returns the next key and value, or nil when the
// traversal ends; it runs the VM's lua_next, which raises for an invalid key.
int nextWrapper(lua_State* L)
{
    int more = lua_next(L, 1);
    if (more)
        return 2;
    lua_pushnil(L);
    return 1;
}

// In a weak "k" table, an emptied entry's key dies before its object is freed (cleartable turns it into
// LUA_TDEADKEY). After t[k] = nil and a full collect, next(t, k) works only with the same object; an equal
// different object raises "invalid key to 'next'".
TEST_CASE("Vec3KeyNextSameObjectOnly")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    // weak "k" table at stack index 1
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "k");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    LuaTable* t = hvalue(L->top - 1);

    // k at stack index 2, kept alive on the stack for the whole test
    pushVec3(L, 7 * MOO_NUMBER_ONE, 8 * MOO_NUMBER_ONE, 9 * MOO_NUMBER_ONE);
    MooVec3* k = vec3value(L->top - 1);
    TValue* slot = luaH_set(L, t, L->top - 1);
    setnvalue(slot, 5 * MOO_NUMBER_ONE);
    setnilvalue(slot);
    // k stays on the stack at index 2; the table's emptied entry becomes a dead key in cleartable

    luaC_fullgc(L);

    // the same object k works as the current key; the traversal simply ends (no non-nil entries remain)
    lua_pushcfunction(L, nextWrapper, "nextWrapper");
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    CHECK(lua_pcall(L, 2, 1, 0) == 0);
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 1);

    // an equal, different object raises "invalid key to 'next'"
    pushVec3(L, 7 * MOO_NUMBER_ONE, 8 * MOO_NUMBER_ONE, 9 * MOO_NUMBER_ONE);
    REQUIRE(vec3value(L->top - 1) != k);
    lua_pushcfunction(L, nextWrapper, "nextWrapper");
    lua_pushvalue(L, 1);
    lua_pushvalue(L, -3);
    CHECK(lua_pcall(L, 2, 1, 0) != 0);
    const char* message = lua_tostring(L, -1);
    CHECK(message != nullptr);
    CHECK(strstr(message, "invalid key to 'next'") != nullptr);
    lua_pop(L, 1);

    lua_close(L);
}

} // namespace vec3_key

TEST_SUITE_END();
