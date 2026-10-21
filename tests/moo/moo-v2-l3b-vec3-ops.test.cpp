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
    }

    // every operation allocates exactly one new 40-byte object (docs/DESIGN.md "vec3, quat and transform (layer 3b)";
    // docs/layer3b/moo-v2-l3b-vec3-object.md, decision 2); with the collector stopped, totalbytes shows it
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
