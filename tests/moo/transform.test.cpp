// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// transform: the object, value behaviour, GC, and the operations against fixed's C (docs/DESIGN.md
// "vec3, quat and transform").

#include "lua.h"
#include "lualib.h"
#include "moo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "moo_number.h"
#include "moo_quat.h"
#include "moo_transform.h"
#include "moo_vec3.h"
#include "Luau/Compiler.h"
#include "MooHost.h"
#include "doctest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("transform");

// moo: the transform object, operators and library of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer
// 3b)", "Operations"; docs/history/layer3b/moo-v2-l3b-transform.md). The object cases follow the vec3 and quat object
// cases (create, layout, equality, keys, tostring, GC churn). Each operation case compares the bits the VM produces
// with the fixed C function called directly here, on 4096 seeded inputs: transform * transform (fixMulTransforms),
// transform * vec3 and transform.point (fixTransformPoint), transform.inverse (fixInvertTransform),
// transform.direction (fixRotateVector), transform.invpoint (fixInvTransformPoint) and transform.invdirection
// (fixInvRotateVector). Reversed witnesses show that each comparison would see swapped operands, the inverse, or
// the position ignored or applied where it must not be.

namespace
{

// splitmix64 with a fixed seed: the same inputs on every platform, no clock and no rand().
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

    // anywhere in the domain [-INT64_MAX, INT64_MAX], or an edge (0, +-1 raw, +-1, the domain ends)
    fixed_t fullRange()
    {
        static const fixed_t edges[] = {0, 1, -1, MOO_NUMBER_ONE, -MOO_NUMBER_ONE, MOO_NUMBER_MAX, MOO_NUMBER_MIN};
        uint64_t r = next();
        if ((r & 7) == 0)
            return edges[(r >> 3) % (sizeof(edges) / sizeof(edges[0]))];
        int64_t v = int64_t(next() >> (1 + (r >> 3) % 63));
        return (r & 8) ? -v : v;
    }

    // a raw value in [-bound, bound]
    fixed_t within(fixed_t bound)
    {
        return fixed_t(next() % uint64_t(2 * bound + 1)) - bound;
    }

    fixVec3 vecWithin(fixed_t bound)
    {
        return FIX_LITERAL(fixVec3){within(bound), within(bound), within(bound)};
    }

    // a unit quaternion: four components in [-1, 1] through fixed's own fixNormalizeQuat
    fixQuat unitQuat()
    {
        fixQuat q = FIX_LITERAL(fixQuat){{within(MOO_NUMBER_ONE), within(MOO_NUMBER_ONE), within(MOO_NUMBER_ONE)},
            within(MOO_NUMBER_ONE)};
        return fixNormalizeQuat(q);
    }

    // a rigid transform inside fixed's defined range: a unit rotation and a position below 2^40 raw (2^24 units),
    // so no int64 sum inside fixed's functions overflows; every 64th is the identity rotation
    fixTransform rigid(int i)
    {
        fixTransform t;
        t.p = vecWithin(fixed_t(1) << 40);
        t.q = (i % 64 == 0) ? fixQuat_identity : unitQuat();
        return t;
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
        REQUIRE(luau_load(L, "=moo-transform", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }
};

void pushTransform(lua_State* L, fixTransform t)
{
    moo_pushtransform(L, t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s);
}

void pushVec3(lua_State* L, fixVec3 v)
{
    moo_pushvec3(L, v.x, v.y, v.z);
}

fixTransform popTransform(lua_State* L)
{
    lua_Number raw[7];
    REQUIRE(moo_totransform(L, -1, raw) == 1);
    lua_pop(L, 1);
    return FIX_LITERAL(fixTransform){{raw[0], raw[1], raw[2]}, {{raw[3], raw[4], raw[5]}, raw[6]}};
}

fixVec3 popVec3(lua_State* L)
{
    lua_Number raw[3];
    REQUIRE(moo_tovec3(L, -1, raw) == 1);
    lua_pop(L, 1);
    return FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
}

bool sameVec3(fixVec3 a, fixVec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool sameQuat(fixQuat a, fixQuat b)
{
    return sameVec3(a.v, b.v) && a.s == b.s;
}

bool sameTransform(fixTransform a, fixTransform b)
{
    return sameVec3(a.p, b.p) && sameQuat(a.q, b.q);
}

// calls the global function name with a transform and an optional second transform or vec3; returns 1 result
void call(lua_State* L, const char* name, fixTransform t)
{
    lua_getglobal(L, name);
    pushTransform(L, t);
    REQUIRE(lua_pcall(L, 1, 1, 0) == 0);
}

void call(lua_State* L, const char* name, fixTransform a, fixTransform b)
{
    lua_getglobal(L, name);
    pushTransform(L, a);
    pushTransform(L, b);
    REQUIRE(lua_pcall(L, 2, 1, 0) == 0);
}

void call(lua_State* L, const char* name, fixTransform t, fixVec3 v)
{
    lua_getglobal(L, name);
    pushTransform(L, t);
    pushVec3(L, v);
    REQUIRE(lua_pcall(L, 2, 1, 0) == 0);
}

struct TransformHeapCount
{
    size_t count = 0;
};

bool countTransformVisitor(void* context, lua_Page* page, GCObject* gco)
{
    (void)page;
    TransformHeapCount* c = static_cast<TransformHeapCount*>(context);
    if (gco->gch.tt == LUA_TTRANSFORM)
        c->count++;
    return false;
}

size_t countLiveTransform(lua_State* L)
{
    TransformHeapCount c;
    luaM_visitgco(L, &c, countTransformVisitor);
    return c.count;
}

const fixed_t ONE = MOO_NUMBER_ONE;

} // namespace

// Allocation through luaM_newgcofixed + luaC_init: tag LUA_TTRANSFORM (after quat), type() "transform", the payload
// is fixed's fixTransform, the creation number advances by 1 per transform, and totalbytes by sizeof(MooTransform)
// == 72 (16-byte header plus 56 bytes of payload).
TEST_CASE("TransformCreateAndLayoutInVm")
{
    CHECK(int(LUA_TTRANSFORM) == int(LUA_TQUAT) + 1);
    CHECK(sizeof(MooTransform) == 72);

    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    luaC_fullgc(L);
    size_t bytesBefore = L->global->totalbytes;
    uint64_t seqBefore = L->global->gcseq;

    fixTransform a = FIX_LITERAL(fixTransform){{1 * ONE, -2 * ONE, 3 * ONE}, {{4, 5, 6}, 7 * ONE}};
    MooTransform* t1 = moo_transform_new(L, a);
    REQUIRE(t1 != nullptr);
    CHECK(t1->tt == LUA_TTRANSFORM);
    CHECK(t1->memcat == L->activememcat);
    CHECK(sameTransform(moo_transform_payload(t1), a));
    CHECK(t1->gcseq == seqBefore);
    CHECK(L->global->totalbytes == bytesBefore + sizeof(MooTransform));

    settransformvalue(L, L->top, t1);
    incr_top(L);
    CHECK(ttistransform(L->top - 1));
    CHECK(transformvalue(L->top - 1) == t1);
    CHECK(gco2transform(gcvalue(L->top - 1)) == t1);
    CHECK(lua_type(L, -1) == LUA_TTRANSFORM);
    CHECK(strcmp(lua_typename(L, lua_type(L, -1)), "transform") == 0);
    CHECK(moo_objectseq(L, -1) == seqBefore);
    CHECK(lua_topointer(L, -1) == nullptr); // a value exposes no address

    MooTransform* t2 = moo_transform_new(L, fixTransform_identity);
    settransformvalue(L, L->top, t2);
    incr_top(L);
    CHECK(t2->gcseq == seqBefore + 1);
    CHECK(L->global->totalbytes == bytesBefore + 2 * sizeof(MooTransform));

    lua_pop(L, 2);
    lua_close(L);
}

// Equality, table keys and tostring by value: every one of the seven components is compared and hashed (a change in
// any one component is a different value and a different key), and the C API carries the raw components both ways.
TEST_CASE("TransformValueBehaviour")
{
    TestState s;
    CHECK(mooRun(s.L, "return tostring(transform.new())") == "transform(vec3(0, 0, 0), quat(0, 0, 0, 1))");
    CHECK(mooRun(s.L, "return tostring(transform.new(vec3.new(1, -2.5, 0.1), quat.new(0, 0, 0.6, 0.8)))") ==
          "transform(vec3(1, -2.5, 0.1), quat(0, 0, 0.6, 0.8))");
    CHECK(mooRun(s.L, "return tostring(transform.new(nil, quat.new(0, 0, 1, 0)))") == "transform(vec3(0, 0, 0), quat(0, 0, 1, 0))");
    CHECK(mooRun(s.L, "return type(transform.new()) .. ' ' .. typeof(transform.new())") == "transform transform");
    CHECK(mooRun(s.L, "local t = transform.new(vec3.new(1, 2, 3), quat.new(0, 0, 0.6, 0.8)) return tostring(t.p) .. ' ' .. tostring(t.q)") ==
          "vec3(1, 2, 3) quat(0, 0, 0.6, 0.8)");
    CHECK(mooRun(s.L, "return transform.new() == transform.new()") == "true");
    CHECK(mooRun(s.L, "return rawequal(transform.new(vec3.new(1, 2, 3)), transform.new(vec3.new(1, 2, 3)))") == "true");
    CHECK(mooRun(s.L, "local t = {} t[transform.new(vec3.new(1, 2, 3))] = 7 return t[transform.new(vec3.new(1, 2, 3))]") == "7");
    CHECK(mooRun(s.L, "return getmetatable(transform.new()) == nil") == "true");

    // the longest tostring fits MOO_TRANSFORM_TOSTR_MAX: seven components at the most negative fraction
    {
        lua_State* L = s.L;
        fixed_t m = MOO_NUMBER_MIN + 1; // a long fraction and a sign
        MooTransform* t = moo_transform_new(L, FIX_LITERAL(fixTransform){{m, m, m}, {{m, m, m}, m}});
        char buf[MOO_TRANSFORM_TOSTR_MAX];
        char* end = moo_transform_tostring(buf, t);
        CHECK(size_t(end - buf) <= size_t(MOO_TRANSFORM_TOSTR_MAX));
    }

    // each of the seven components separates two values and two keys (reversed witness: a compare or hash that
    // skipped any one component would see these pairs as equal)
    lua_newtable(s.L);
    LuaTable* h = hvalue(s.L->top - 1);
    for (int c = 0; c < 7; ++c)
    {
        fixed_t raw[7] = {1, 2, 3, 4, 5, 6, 7};
        fixed_t other[7] = {1, 2, 3, 4, 5, 6, 7};
        other[c] += 1000;
        moo_pushtransform(s.L, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6]);
        moo_pushtransform(s.L, other[0], other[1], other[2], other[3], other[4], other[5], other[6]);
        CAPTURE(c);
        CHECK(lua_rawequal(s.L, -1, -2) == 0);
        CHECK(lua_equal(s.L, -1, -2) == 0);
        CHECK(moo_transform_hash(transformvalue(s.L->top - 1)) != moo_transform_hash(transformvalue(s.L->top - 2)));
        TValue* slot = luaH_set(s.L, h, s.L->top - 1);
        setnvalue(slot, c + 1);
        lua_pop(s.L, 2);
    }
    for (int c = 0; c < 7; ++c)
    {
        fixed_t other[7] = {1, 2, 3, 4, 5, 6, 7};
        other[c] += 1000;
        moo_pushtransform(s.L, other[0], other[1], other[2], other[3], other[4], other[5], other[6]);
        lua_rawget(s.L, -2);
        CHECK(lua_tonumber(s.L, -1) == (c + 1));
        lua_pop(s.L, 1);
    }
    moo_pushtransform(s.L, 1, 2, 3, 4, 5, 6, 7);
    lua_rawget(s.L, -2);
    CHECK(lua_isnil(s.L, -1));
    lua_pop(s.L, 2);

    // the raw components cross the C API both ways, edges included; a vec3 is not a transform
    moo_pushtransform(s.L, MOO_NUMBER_MAX, MOO_NUMBER_MIN, 1, -1, 2, -2, 3);
    lua_Number raw[7] = {9, 9, 9, 9, 9, 9, 9};
    REQUIRE(moo_totransform(s.L, -1, raw) == 1);
    CHECK(raw[0] == MOO_NUMBER_MAX);
    CHECK(raw[1] == MOO_NUMBER_MIN);
    CHECK(raw[2] == 1);
    CHECK(raw[3] == -1);
    CHECK(raw[4] == 2);
    CHECK(raw[5] == -2);
    CHECK(raw[6] == 3);
    lua_pop(s.L, 1);
    moo_pushvec3(s.L, 1, 2, 3);
    for (lua_Number& r : raw)
        r = 9;
    CHECK(moo_totransform(s.L, -1, raw) == 0); // nothing written
    CHECK(raw[0] == 9);
    CHECK(raw[6] == 9);
    lua_pop(s.L, 1);

    // the GC debug walk knows the leaf
    CHECK(mooRun(s.L, "moo_keep = {transform.new(vec3.new(1, 2, 3))} return 0") == "0");
    luaC_fullgc(s.L);
    luaC_validate(s.L);
    CHECK(mooRun(s.L, "return tostring(moo_keep[1])") == "transform(vec3(1, 2, 3), quat(0, 0, 0, 1))");

    // one new object per operation, with the collector stopped: 72 bytes for a transform result, 40 for a vec3
    s.load("function moo_tf_mul(a, b) return a * b end function moo_tf_p(t) return t.p end function moo_tf_q(t) return t.q end");
    lua_gc(s.L, LUA_GCSTOP, 0);
    for (int kind = 0; kind < 4; ++kind)
    {
        lua_getglobal(s.L, kind < 2 ? "moo_tf_mul" : kind == 2 ? "moo_tf_p" : "moo_tf_q");
        pushTransform(s.L, fixTransform_identity);
        if (kind == 0)
            pushTransform(s.L, fixTransform_identity);
        else if (kind == 1)
            moo_pushvec3(s.L, 1, 2, 3);
        size_t before = lua_totalbytes(s.L, 0);
        REQUIRE(lua_pcall(s.L, kind < 2 ? 2 : 1, 1, 0) == 0);
        size_t want = kind == 0 ? sizeof(MooTransform) : kind == 3 ? sizeof(MooQuat) : sizeof(MooVec3);
        CAPTURE(kind);
        CHECK(lua_totalbytes(s.L, 0) - before == want);
        lua_pop(s.L, 1);
    }
    lua_gc(s.L, LUA_GCRESTART, 0);
}

// A transform is a leaf: collected when unreachable, totalbytes back to the baseline after 100000 transforms under
// incremental steps, no destructor queued, and a transform key or value in a weak table survives while referenced.
TEST_CASE("TransformGcLeafAndChurn100k")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    luaC_fullgc(L);
    CHECK(countLiveTransform(L) == 0);
    size_t baselineBytes = L->global->totalbytes;

    pushTransform(L, fixTransform_identity);
    pushTransform(L, FIX_LITERAL(fixTransform){{1, 2, 3}, {{0, 0, 0}, ONE}});
    CHECK(countLiveTransform(L) == 2);
    lua_pop(L, 1);
    luaC_fullgc(L);
    CHECK(countLiveTransform(L) == 1);
    lua_pop(L, 1);
    luaC_fullgc(L);
    CHECK(countLiveTransform(L) == 0);
    CHECK(L->global->totalbytes == baselineBytes);

    for (int i = 0; i < 100000; ++i)
    {
        MooTransform* t = moo_transform_new(L, FIX_LITERAL(fixTransform){{fixed_t(i), fixed_t(-i), 0}, {{0, 0, 0}, fixed_t(i + 1)}});
        settransformvalue(L, L->top, t);
        incr_top(L);
        // luaC_step requires the debt to be due (lgc.cpp asserts totalbytes >= GCthreshold in a checked build)
        if (i % 64 == 63 && L->global->totalbytes >= L->global->GCthreshold)
            luaC_step(L, true);
        CHECK(L->global->dtorqueue == nullptr);
        lua_pop(L, 1);
    }
    luaC_fullgc(L);
    CHECK(countLiveTransform(L) == 0);
    CHECK(L->global->totalbytes == baselineBytes);

    for (const char* mode : {"k", "v", "kv"})
    {
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, mode);
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);

        pushTransform(L, FIX_LITERAL(fixTransform){{100, 200, 300}, {{0, 0, 0}, ONE}});
        lua_pushboolean(L, 1);
        lua_settable(L, -3);
        lua_pushliteral(L, "v");
        pushTransform(L, FIX_LITERAL(fixTransform){{300, 200, 100}, {{0, 0, 0}, ONE}});
        lua_settable(L, -3);

        // a transform is a value, never cleared from a weak table (lgc.cpp isobjcleared, as for vec3 and quat)
        luaC_fullgc(L);
        CAPTURE(mode);
        CHECK(countLiveTransform(L) == 2);
        lua_pop(L, 1);
        luaC_fullgc(L);
        CHECK(countLiveTransform(L) == 0);
    }

    lua_close(L);
}

// transform * transform is fixMulTransforms on rigid transforms inside fixed's defined range. Reversed witness: the
// composition is not commutative on the corpus, so a VM that swapped the operands fails.
TEST_CASE("TransformMulMatchesFixed4096")
{
    TestState s;
    s.load("function moo_tf_mul(a, b) return a * b end");

    SplitMix rng{0x452821e638d01377ull};
    int notCommutative = 0;
    for (int i = 0; i < 4096; ++i)
    {
        fixTransform a = rng.rigid(i);
        fixTransform b = rng.rigid(i + 1);
        CAPTURE(i);
        call(s.L, "moo_tf_mul", a, b);
        fixTransform want = fixMulTransforms(a, b);
        CHECK(sameTransform(popTransform(s.L), want));
        if (!sameTransform(want, fixMulTransforms(b, a)))
            ++notCommutative;
    }
    CHECK(notCommutative > 4000);
}

// transform * vec3 and transform.point(t, v) are fixTransformPoint; transform.direction(t, v) is fixRotateVector
// (t.q, v). Reversed witnesses: point differs from direction (the position is applied) and from invpoint.
TEST_CASE("TransformPointAndDirectionMatchFixed4096")
{
    TestState s;
    s.load("function moo_tf_op(t, v) return t * v end "
           "function moo_tf_point(t, v) return transform.point(t, v) end "
           "function moo_tf_dir(t, v) return transform.direction(t, v) end");

    SplitMix rng{0xbe5466cf34e90c6cull};
    int positionApplied = 0;
    int inverseDiffers = 0;
    for (int i = 0; i < 4096; ++i)
    {
        fixTransform t = rng.rigid(i);
        fixVec3 v = rng.vecWithin(fixed_t(1) << 40);
        CAPTURE(i);
        fixVec3 want = fixTransformPoint(t, v);
        call(s.L, "moo_tf_op", t, v);
        CHECK(sameVec3(popVec3(s.L), want));
        call(s.L, "moo_tf_point", t, v);
        CHECK(sameVec3(popVec3(s.L), want));
        fixVec3 wantDir = fixRotateVector(t.q, v);
        call(s.L, "moo_tf_dir", t, v);
        CHECK(sameVec3(popVec3(s.L), wantDir));
        if (!sameVec3(want, wantDir))
            ++positionApplied;
        if (!sameVec3(want, fixInvTransformPoint(t, v)))
            ++inverseDiffers;
    }
    CHECK(positionApplied > 4000);
    CHECK(inverseDiffers > 4000);
}

// transform.invpoint is fixInvTransformPoint and transform.invdirection is fixInvRotateVector(t.q, v). Reversed
// witnesses: invdirection differs from direction (the conjugate is applied) and invpoint from invdirection.
TEST_CASE("TransformInversePointAndDirectionMatchFixed4096")
{
    TestState s;
    s.load("function moo_tf_ipoint(t, v) return transform.invpoint(t, v) end "
           "function moo_tf_idir(t, v) return transform.invdirection(t, v) end");

    SplitMix rng{0xc0ac29b7c97c50ddull};
    int conjugateApplied = 0;
    int positionApplied = 0;
    for (int i = 0; i < 4096; ++i)
    {
        fixTransform t = rng.rigid(i);
        fixVec3 v = rng.vecWithin(fixed_t(1) << 40);
        CAPTURE(i);
        fixVec3 want = fixInvTransformPoint(t, v);
        call(s.L, "moo_tf_ipoint", t, v);
        CHECK(sameVec3(popVec3(s.L), want));
        fixVec3 wantDir = fixInvRotateVector(t.q, v);
        call(s.L, "moo_tf_idir", t, v);
        CHECK(sameVec3(popVec3(s.L), wantDir));
        if (!sameVec3(wantDir, fixRotateVector(t.q, v)))
            ++conjugateApplied;
        if (!sameVec3(want, wantDir))
            ++positionApplied;
    }
    CHECK(conjugateApplied > 4000);
    CHECK(positionApplied > 4000);
}

// transform.inverse is fixInvertTransform on rigid transforms inside fixed's defined range (negation is exact, but
// fixInvRotateVector's sums are plain int64 outside it). Reversed witness: the inverse's position is the rotated negation, which differs
// from the plain negation on the corpus, so a VM that only negated would fail.
TEST_CASE("TransformInverseMatchesFixed4096")
{
    TestState s;
    s.load("function moo_tf_inv(t) return transform.inverse(t) end");

    SplitMix rng{0x3f84d5b5b5470917ull};
    int rotated = 0;
    for (int i = 0; i < 4096; ++i)
    {
        fixTransform t = rng.rigid(i);
        CAPTURE(i);
        call(s.L, "moo_tf_inv", t);
        fixTransform want = fixInvertTransform(t);
        CHECK(sameTransform(popTransform(s.L), want));
        if (!sameVec3(want.p, fixVecNeg(t.p)))
            ++rotated;
    }
    CHECK(rotated > 4000);
}

// Outside fixed's defined range the one position add of compose and point, and the subtract of invpoint, saturate
// as vec3 + and - do, where fixed's fixVecAdd/fixVecSub would overflow int64 (undefined in C). With the identity
// rotation the rotated part is exact, so the expected saturated value is known.
TEST_CASE("TransformPositionSaturates")
{
    TestState s;
    s.load("function moo_tf_mul(a, b) return a * b end "
           "function moo_tf_point(t, v) return transform.point(t, v) end "
           "function moo_tf_ipoint(t, v) return transform.invpoint(t, v) end");

    const fixed_t big = MOO_NUMBER_MAX - 5;
    fixTransform a = FIX_LITERAL(fixTransform){{big, -big, 1}, fixQuat_identity};
    fixTransform b = FIX_LITERAL(fixTransform){{big, -big, 2}, fixQuat_identity};

    call(s.L, "moo_tf_mul", a, b);
    fixTransform got = popTransform(s.L);
    CHECK(got.p.x == MOO_NUMBER_MAX);
    CHECK(got.p.y == MOO_NUMBER_MIN);
    CHECK(got.p.z == 3);
    CHECK(sameQuat(got.q, fixQuat_identity));

    call(s.L, "moo_tf_point", a, b.p);
    fixVec3 gp = popVec3(s.L);
    CHECK(gp.x == MOO_NUMBER_MAX);
    CHECK(gp.y == MOO_NUMBER_MIN);
    CHECK(gp.z == 3);

    call(s.L, "moo_tf_ipoint", a, FIX_LITERAL(fixVec3){-big, big, 1});
    fixVec3 gi = popVec3(s.L);
    CHECK(gi.x == MOO_NUMBER_MIN);
    CHECK(gi.y == MOO_NUMBER_MAX);
    CHECK(gi.z == 0);

    // one raw unit inside the range nothing saturates and the bits are fixed's own (a clamp one unit early fails)
    fixTransform c = FIX_LITERAL(fixTransform){{MOO_NUMBER_MAX - 4, MOO_NUMBER_MIN + 4, 0}, fixQuat_identity};
    fixVec3 w = FIX_LITERAL(fixVec3){3, -3, 0};
    call(s.L, "moo_tf_point", c, w);
    fixVec3 gc = popVec3(s.L);
    CHECK(gc.x == MOO_NUMBER_MAX - 1);
    CHECK(gc.y == MOO_NUMBER_MIN + 1);
    CHECK(sameVec3(gc, fixTransformPoint(c, w)));
}

// Every operation outside the table of docs/DESIGN.md "Operations" is refused with the ordinary error, by name.
TEST_CASE("TransformRefusals")
{
    TestState s;
    struct Refusal
    {
        const char* source;
        const char* error;
    };
    const Refusal refusals[] = {
        {"return t + t", "attempt to perform arithmetic (add) on transform"},
        {"return t - t", "attempt to perform arithmetic (sub) on transform"},
        {"return -t", "attempt to perform arithmetic (unm) on transform"},
        {"return t / t", "attempt to perform arithmetic (div) on transform"},
        {"return t // t", "attempt to perform arithmetic (idiv) on transform"},
        {"return t % t", "attempt to perform arithmetic (mod) on transform"},
        {"return t ^ 2", "attempt to perform arithmetic (pow) on transform and number"},
        {"return t * 2", "attempt to perform arithmetic (mul) on transform and number"},
        {"return 2 * t", "attempt to perform arithmetic (mul) on number and transform"},
        {"return v * t", "attempt to perform arithmetic (mul) on vec3 and transform"},
        {"return q * t", "attempt to perform arithmetic (mul) on quat and transform"},
        {"return t * q", "attempt to perform arithmetic (mul) on transform and quat"},
        {"return t < t", "attempt to compare transform < transform"},
        {"return #t", "attempt to get length of a transform value"},
        {"return t .. ''", "attempt to concatenate transform with string"},
        {"return t.x", "attempt to index transform with 'x'"},
        {"return t.P", "attempt to index transform with 'P'"},
        {"return t.position", "attempt to index transform with 'position'"},
        {"t.p = v", "attempt to index transform with 'p'"},
        {"return t()", "attempt to call a transform value"},
        {"return transform.new(1)", "invalid argument #1 to 'new' (vec3 expected, got number)"},
        {"return transform.new(v, v)", "invalid argument #2 to 'new' (quat expected, got vec3)"},
        {"return transform.inverse(v)", "invalid argument #1 to 'inverse' (transform expected, got vec3)"},
        {"return transform.point(t, q)", "invalid argument #2 to 'point' (vec3 expected, got quat)"},
        {"return transform.point(v, v)", "invalid argument #1 to 'point' (transform expected, got vec3)"},
        {"return transform.direction(t)", "missing argument #2 to 'direction' (vec3 expected)"},
        {"return transform.invpoint(t, 1)", "invalid argument #2 to 'invpoint' (vec3 expected, got number)"},
        {"return transform.invdirection(q, v)", "invalid argument #1 to 'invdirection' (transform expected, got quat)"},
    };
    for (const Refusal& r : refusals)
    {
        std::string source =
            std::string("local t, q, v = transform.new(vec3.new(1, 2, 3)), quat.new(0, 0, 0.6, 0.8), vec3.new(1, 2, 3) ") + r.source;
        std::string got = mooRun(s.L, source.c_str());
        CAPTURE(source);
        CAPTURE(got);
        CHECK(got.rfind("error: ", 0) == 0);
        CHECK(got.find(r.error) != std::string::npos);
    }
}

// The expected-behaviour script of this row, in tests/conformance/moo/spec.
TEST_CASE("TransformScript")
{
    TestState s;
    CHECK(mooRunFile(s.L, "spec/transform.luau") == "OK");
}

TEST_SUITE_END();
