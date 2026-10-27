// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// quat: the object, value equality, table keys, GC, and the operations against fixed's C (docs/DESIGN.md
// "vec3, quat and transform").

#include "lua.h"
#include "lualib.h"
#include "lgc.h"
#include "lstate.h"
#include "moo_number.h"
#include "moo_quat.h"
#include "moo_vec3.h"
#include "Luau/Compiler.h"
#include "MooHost.h"
#include "doctest.h"
#include "moo.h"
#include "lapi.h"
#include "lmem.h"
#include "lobject.h"
#include "ltable.h"
#include "moo_format.h"
#include "moo_parse.h"

#include <stdint.h>
#include <string.h>
#include <string>
#include <stddef.h>

TEST_SUITE_BEGIN("quat");

// moo: the quat operators and library of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)",
// "Operations"; docs/history/layer3b/moo-v2-l3b-quat.md). Each case compares the bits the VM produces with the fixed C
// function called directly here, on 4096 seeded inputs: quat * quat (fixMulQuat), quat * vec3 (fixRotateVector),
// quat.conjugate (fixConjugate) and quat.nlerp (fixNLerp). Reversed witnesses show that each comparison would see
// a swapped operand, the inverse rotation or a reversed alpha.

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

    // a unit quaternion: four components in [-1, 1] through fixed's own fixNormalizeQuat
    fixQuat unitQuat()
    {
        fixQuat q = FIX_LITERAL(fixQuat){{within(MOO_NUMBER_ONE), within(MOO_NUMBER_ONE), within(MOO_NUMBER_ONE)},
            within(MOO_NUMBER_ONE)};
        return fixNormalizeQuat(q);
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
        REQUIRE(luau_load(L, "=moo-quat", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }
};

void pushQuat(lua_State* L, fixQuat q)
{
    moo_pushquat(L, q.v.x, q.v.y, q.v.z, q.s);
}

fixQuat popQuat(lua_State* L)
{
    lua_Number raw[4];
    REQUIRE(moo_toquat(L, -1, raw) == 1);
    lua_pop(L, 1);
    return FIX_LITERAL(fixQuat){{raw[0], raw[1], raw[2]}, raw[3]};
}

fixVec3 popVec3(lua_State* L)
{
    lua_Number raw[3];
    REQUIRE(moo_tovec3(L, -1, raw) == 1);
    lua_pop(L, 1);
    return FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
}

bool sameQuat(fixQuat a, fixQuat b)
{
    return a.v.x == b.v.x && a.v.y == b.v.y && a.v.z == b.v.z && a.s == b.s;
}

bool sameVec3(fixVec3 a, fixVec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

// quat * quat is fixMulQuat over the full domain: every component is one 128-bit reduction (defined for any
// operands) and saturates under FIX_SATURATE. Reversed witness: the product is not commutative on the corpus,
// so a VM that swapped the operands fails.
TEST_CASE("QuatMulMatchesFixedFullRange4096")
{
    TestState s;
    s.load("function moo_quat_mul(a, b) return a * b end");

    SplitMix rng{0x243f6a8885a308d3ull};
    int notCommutative = 0;
    for (int i = 0; i < 4096; ++i)
    {
        // half full-range, half unit quaternions
        fixQuat a, b;
        if (i & 1)
        {
            a = rng.unitQuat();
            b = rng.unitQuat();
        }
        else
        {
            a = FIX_LITERAL(fixQuat){{rng.fullRange(), rng.fullRange(), rng.fullRange()}, rng.fullRange()};
            b = FIX_LITERAL(fixQuat){{rng.fullRange(), rng.fullRange(), rng.fullRange()}, rng.fullRange()};
        }
        CAPTURE(i);
        lua_getglobal(s.L, "moo_quat_mul");
        pushQuat(s.L, a);
        pushQuat(s.L, b);
        REQUIRE(lua_pcall(s.L, 2, 1, 0) == 0);
        fixQuat got = popQuat(s.L);
        fixQuat want = fixMulQuat(a, b);
        CHECK(sameQuat(got, want));
        if (!sameQuat(want, fixMulQuat(b, a)))
            ++notCommutative;
    }
    CHECK(notCommutative > 1000);
}

// quat * vec3 is fixRotateVector for a unit quaternion and a vector whose components stay inside fixed's
// defined range (|v| < 2^24 units): fixRotateVector's fixCross and fixMulAdd are plain int64 arithmetic at this
// pin (DESIGN.md "Saturation, stated precisely"). Reversed witness: the corpus distinguishes the rotation from
// the inverse rotation (fixInvRotateVector), so a VM that applied the conjugate fails.
TEST_CASE("QuatRotateMatchesFixed4096")
{
    TestState s;
    s.load("function moo_quat_rot(q, v) return q * v end");

    SplitMix rng{0x13198a2e03707344ull};
    int inverseDiffers = 0;
    const fixed_t bound = fixed_t(1) << 40;
    for (int i = 0; i < 4096; ++i)
    {
        fixQuat q = (i % 64 == 0) ? fixQuat_identity : rng.unitQuat();
        fixVec3 v = FIX_LITERAL(fixVec3){rng.within(bound), rng.within(bound), rng.within(bound)};
        CAPTURE(i);
        lua_getglobal(s.L, "moo_quat_rot");
        pushQuat(s.L, q);
        moo_pushvec3(s.L, v.x, v.y, v.z);
        REQUIRE(lua_pcall(s.L, 2, 1, 0) == 0);
        fixVec3 got = popVec3(s.L);
        fixVec3 want = fixRotateVector(q, v);
        CHECK(sameVec3(got, want));
        if (sameQuat(q, fixQuat_identity))
            CHECK(sameVec3(got, v)); // the identity rotation is exact
        if (!sameVec3(want, fixInvRotateVector(q, v)))
            ++inverseDiffers;
    }
    CHECK(inverseDiffers > 3000);
}

// quat.conjugate is fixConjugate over the full domain (negation is exact on [-INT64_MAX, INT64_MAX]).
TEST_CASE("QuatConjugateMatchesFixedFullRange4096")
{
    TestState s;
    s.load("function moo_quat_conj(q) return quat.conjugate(q) end");

    SplitMix rng{0xa4093822299f31d0ull};
    for (int i = 0; i < 4096; ++i)
    {
        fixQuat q = FIX_LITERAL(fixQuat){{rng.fullRange(), rng.fullRange(), rng.fullRange()}, rng.fullRange()};
        CAPTURE(i);
        lua_getglobal(s.L, "moo_quat_conj");
        pushQuat(s.L, q);
        REQUIRE(lua_pcall(s.L, 1, 1, 0) == 0);
        CHECK(sameQuat(popQuat(s.L), fixConjugate(q)));
    }
}

// quat.nlerp is fixNLerp for unit quaternions and alpha in [0, 1], the edges 0, 1/2 and 1 included. Reversed
// witness: the corpus distinguishes alpha from 1 - alpha, so a VM that reversed the blend fails.
TEST_CASE("QuatNlerpMatchesFixed4096")
{
    TestState s;
    s.load("function moo_quat_nlerp(a, b, t) return quat.nlerp(a, b, t) end");

    SplitMix rng{0x082efa98ec4e6c89ull};
    int reversedDiffers = 0;
    for (int i = 0; i < 4096; ++i)
    {
        fixQuat a = rng.unitQuat();
        fixQuat b = rng.unitQuat();
        fixed_t alpha = i % 3 == 0 ? fixed_t(i % 9 == 0 ? 0 : i % 9 == 3 ? MOO_NUMBER_ONE / 2 : MOO_NUMBER_ONE)
                                   : rng.within(MOO_NUMBER_ONE / 2) + MOO_NUMBER_ONE / 2;
        CAPTURE(i);
        CAPTURE(alpha);
        lua_getglobal(s.L, "moo_quat_nlerp");
        pushQuat(s.L, a);
        pushQuat(s.L, b);
        lua_pushnumber(s.L, alpha);
        REQUIRE(lua_pcall(s.L, 3, 1, 0) == 0);
        fixQuat want = fixNLerp(a, b, alpha);
        CHECK(sameQuat(popQuat(s.L), want));
        if (!sameQuat(want, fixNLerp(a, b, MOO_NUMBER_ONE - alpha)))
            ++reversedDiffers;
    }
    CHECK(reversedDiffers > 2000);
}

// Components, tostring, constructor defaults, equality by value, table keys, the C API both ways, and one
// allocation per operation (48 bytes for a quat result, 40 for a vec3).
TEST_CASE("QuatValueBehaviour")
{
    TestState s;
    CHECK(mooRun(s.L, "return tostring(quat.new(0, 0, 0.6, 0.8))") == "quat(0, 0, 0.6, 0.8)");
    CHECK(mooRun(s.L, "return tostring(quat.new())") == "quat(0, 0, 0, 1)");
    CHECK(mooRun(s.L, "return tostring(quat.new(1, 2))") == "quat(1, 2, 0, 1)");
    CHECK(mooRun(s.L, "local q = quat.new(1, -2.5, 0.1, 3) return q.x .. ' ' .. q.y .. ' ' .. q.z .. ' ' .. q.w") ==
          "1 -2.5 0.1 3");
    CHECK(mooRun(s.L, "local q = quat.new(1, -2.5, 0.1, 3) return q.X .. ' ' .. q.W") == "1 3");
    CHECK(mooRun(s.L, "return type(quat.new()) .. ' ' .. typeof(quat.new())") == "quat quat");
    CHECK(mooRun(s.L, "return quat.new(1, 2, 3, 4) == quat.new(1, 2, 3, 4)") == "true");
    CHECK(mooRun(s.L, "return quat.new(1, 2, 3, 4) ~= quat.new(1, 2, 3, 5)") == "true");
    CHECK(mooRun(s.L, "return rawequal(quat.new(1, 2, 3, 4), quat.new(1, 2, 3, 4))") == "true");
    CHECK(mooRun(s.L, "local t = {} t[quat.new(0, 0, 0.6, 0.8)] = 7 return t[quat.new(0, 0, 0.6, 0.8)]") == "7");
    CHECK(mooRun(s.L, "return getmetatable(quat.new()) == nil") == "true");
    // a vec3 has no w
    CHECK(mooRun(s.L, "return vec3.new(1, 2, 3).w").find("attempt to index vec3 with 'w'") != std::string::npos);

    // the raw components cross the C API both ways, edges included
    moo_pushquat(s.L, MOO_NUMBER_MAX, MOO_NUMBER_MIN, 1, -1);
    lua_Number raw[4] = {7, 7, 7, 7};
    REQUIRE(moo_toquat(s.L, -1, raw) == 1);
    CHECK(raw[0] == MOO_NUMBER_MAX);
    CHECK(raw[1] == MOO_NUMBER_MIN);
    CHECK(raw[2] == 1);
    CHECK(raw[3] == -1);
    lua_pop(s.L, 1);
    moo_pushvec3(s.L, 1, 2, 3);
    raw[0] = raw[1] = raw[2] = raw[3] = 7;
    CHECK(moo_toquat(s.L, -1, raw) == 0); // not a quat: nothing written
    CHECK(raw[0] == 7);
    CHECK(raw[3] == 7);
    CHECK(lua_topointer(s.L, -1) == nullptr); // a value exposes no address
    lua_pop(s.L, 1);

    // the GC debug walk knows both leaves: with a live vec3 and quat, luaC_validate's walk of every object reached
    // LUAU_ASSERT("unexpected object type") in a checked build before lgcdebug.cpp had their cases
    CHECK(mooRun(s.L, "moo_keep = {vec3.new(1, 2, 3), quat.new(0, 0, 0.6, 0.8)} return 0") == "0");
    luaC_fullgc(s.L);
    luaC_validate(s.L);
    CHECK(mooRun(s.L, "return tostring(moo_keep[1]) .. ' ' .. tostring(moo_keep[2])") == "vec3(1, 2, 3) quat(0, 0, 0.6, 0.8)");

    // one new object per operation, with the collector stopped
    s.load("function moo_quat_mul(a, b) return a * b end");
    lua_gc(s.L, LUA_GCSTOP, 0);
    for (int kind = 0; kind < 2; ++kind)
    {
        lua_getglobal(s.L, "moo_quat_mul");
        moo_pushquat(s.L, 0, 0, 0, MOO_NUMBER_ONE);
        if (kind == 0)
            moo_pushquat(s.L, 0, 0, 0, MOO_NUMBER_ONE);
        else
            moo_pushvec3(s.L, 1, 2, 3);
        size_t before = lua_totalbytes(s.L, 0);
        REQUIRE(lua_pcall(s.L, 2, 1, 0) == 0);
        CHECK(lua_totalbytes(s.L, 0) - before == (kind == 0 ? sizeof(MooQuat) : sizeof(MooVec3)));
        lua_pop(s.L, 1);
    }
    lua_gc(s.L, LUA_GCRESTART, 0);
}

// Every operation outside the table of docs/DESIGN.md "Operations" is refused with the ordinary error, by name,
// and quat.slerp is refused until fixed has a slerp.
TEST_CASE("QuatRefusals")
{
    TestState s;
    struct Refusal
    {
        const char* source;
        const char* error;
    };
    const Refusal refusals[] = {
        {"return q + q", "attempt to perform arithmetic (add) on quat"},
        {"return q - q", "attempt to perform arithmetic (sub) on quat"},
        {"return -q", "attempt to perform arithmetic (unm) on quat"},
        {"return q / q", "attempt to perform arithmetic (div) on quat"},
        {"return q // q", "attempt to perform arithmetic (idiv) on quat"},
        {"return q % q", "attempt to perform arithmetic (mod) on quat"},
        {"return q ^ 2", "attempt to perform arithmetic (pow) on quat and number"},
        {"return q * 2", "attempt to perform arithmetic (mul) on quat and number"},
        {"return 2 * q", "attempt to perform arithmetic (mul) on number and quat"},
        {"return v * q", "attempt to perform arithmetic (mul) on vec3 and quat"},
        {"return q * '2'", "attempt to perform arithmetic (mul) on quat and string"},
        {"return q + v", "attempt to perform arithmetic (add) on quat and vec3"},
        {"return q < q", "attempt to compare quat < quat"},
        {"return #q", "attempt to get length of a quat value"},
        {"return q .. ''", "attempt to concatenate quat with string"},
        {"return q.v", "attempt to index quat with 'v'"},
        {"return q.s", "attempt to index quat with 's'"},
        {"q.x = 1", "attempt to index quat with 'x'"},
        {"return q()", "attempt to call a quat value"},
        {"return quat.conjugate(1)", "invalid argument #1 to 'conjugate' (quat expected, got number)"},
        {"return quat.conjugate(v)", "invalid argument #1 to 'conjugate' (quat expected, got vec3)"},
        {"return quat.nlerp(q, 1, 0.5)", "invalid argument #2 to 'nlerp' (quat expected, got number)"},
        {"return quat.nlerp(q, q)", "missing argument #3 to 'nlerp' (number expected)"},
        {"return quat.nlerp(q, q, 1 + moo_test.fromraw('1'))", "invalid argument #3 to 'nlerp' (alpha must be in [0, 1])"},
        {"return quat.nlerp(q, q, -moo_test.fromraw('1'))", "invalid argument #3 to 'nlerp' (alpha must be in [0, 1])"},
        {"return quat.slerp(q, q, 0.5)", "quat.slerp waits for fixed's slerp"},
        {"return quat.new('a')", "invalid argument #1 to 'new' (number expected, got string)"},
        {"return quat.new(1, 2, 3, {})", "invalid argument #4 to 'new' (number expected, got table)"},
    };
    for (const Refusal& r : refusals)
    {
        CAPTURE(r.source);
        std::string source = std::string("local q, v = quat.new(0, 0, 0.6, 0.8), vec3.new(1, 2, 3) ") + r.source;
        std::string got = mooRun(s.L, source.c_str());
        CHECK(got.rfind("error: ", 0) == 0);
        CHECK(got.find(r.error) != std::string::npos);
    }

    // the edges of alpha are accepted
    CHECK(mooRun(s.L, "local q = quat.new(0, 0, 0.6, 0.8) return type(quat.nlerp(quat.new(), q, 0))") == "quat");
    CHECK(mooRun(s.L, "local q = quat.new(0, 0, 0.6, 0.8) return type(quat.nlerp(quat.new(), q, 1))") == "quat");
}

// The expected-behaviour script of this row, in tests/conformance/moo/spec.
TEST_CASE("QuatScript")
{
    TestState s;
    CHECK(mooRunFile(s.L, "spec/quat.luau") == "OK");
}

// moo: layer 3b quat GC object kind, layout, value equality, table key, tostring and leaf GC
// (docs/DESIGN.md "vec3, quat and transform (layer 3b)"; docs/history/layer3b/moo-v2-l3b-quat-object.md).

namespace
{

void pushQuat(lua_State* L, fixed_t x, fixed_t y, fixed_t z, fixed_t s)
{
    MooQuat* q = moo_quat_new(L, x, y, z, s);
    setquatvalue(L, L->top, q);
    incr_top(L);
}

std::string formatQuat(const MooQuat* q)
{
    char buf[MOO_QUAT_TOSTR_MAX];
    char* end = moo_quat_tostring(buf, q);
    REQUIRE(end >= buf);
    REQUIRE(size_t(end - buf) <= MOO_QUAT_TOSTR_MAX);
    return std::string(buf, size_t(end - buf));
}

struct QuatHeapCount
{
    size_t count = 0;
};

bool countQuatVisitor(void* context, lua_Page* page, GCObject* gco)
{
    (void)page;
    QuatHeapCount* c = static_cast<QuatHeapCount*>(context);
    if (gco->gch.tt == LUA_TQUAT)
        c->count++;
    return false;
}

size_t countLiveQuat(lua_State* L)
{
    QuatHeapCount c;
    luaM_visitgco(L, &c, countQuatVisitor);
    return c.count;
}

} // namespace

// Allocation through luaM_newgcofixed + luaC_init: tag is LUA_TQUAT, type() is "quat", the payload is fixed's
// fixQuat, the creation number advances by 1 per quat, and totalbytes advances by sizeof(MooQuat) == 48.
TEST_CASE("QuatCreateAndLayoutInVm")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    luaC_fullgc(L);
    size_t bytesBefore = L->global->totalbytes;
    uint64_t seqBefore = L->global->gcseq;

    MooQuat* q1 = moo_quat_new(L, 1 * MOO_NUMBER_ONE, -2 * MOO_NUMBER_ONE, 3 * MOO_NUMBER_ONE, 4 * MOO_NUMBER_ONE);
    REQUIRE(q1 != nullptr);
    CHECK(q1->tt == LUA_TQUAT);
    CHECK(q1->memcat == L->activememcat);
    CHECK(q1->v.x == 1 * MOO_NUMBER_ONE);
    CHECK(q1->v.y == -2 * MOO_NUMBER_ONE);
    CHECK(q1->v.z == 3 * MOO_NUMBER_ONE);
    CHECK(q1->s == 4 * MOO_NUMBER_ONE);
    CHECK(q1->gcseq == seqBefore);
    CHECK(L->global->totalbytes == bytesBefore + sizeof(MooQuat));

    setquatvalue(L, L->top, q1);
    incr_top(L);
    CHECK(ttisquat(L->top - 1));
    CHECK(quatvalue(L->top - 1) == q1);
    CHECK(gco2quat(gcvalue(L->top - 1)) == q1);
    CHECK(lua_type(L, -1) == LUA_TQUAT);
    CHECK(strcmp(lua_typename(L, lua_type(L, -1)), "quat") == 0);
    CHECK(moo_objectseq(L, -1) == seqBefore);

    MooQuat* q2 = moo_quat_new(L, 5 * MOO_NUMBER_ONE, 6 * MOO_NUMBER_ONE, 7 * MOO_NUMBER_ONE, 8 * MOO_NUMBER_ONE);
    setquatvalue(L, L->top, q2);
    incr_top(L);
    CHECK(q2->gcseq == seqBefore + 1);
    CHECK(moo_objectseq(L, -1) == seqBefore + 1);
    CHECK(L->global->totalbytes == bytesBefore + 2 * sizeof(MooQuat));

    lua_setglobal(L, "q2");
    lua_setglobal(L, "q1");
    CHECK(mooRun(L, "return type(q1) .. \":\" .. type(q2)") == "quat:quat");

    lua_close(L);
}

// Equality is by the four raw Q48.16 components (moo_quat_equal), never by pointer or creation number.
// luaO_rawequalObj, luaO_rawequalKey, lua_rawequal and script rawequal all use moo_quat_equal.
TEST_CASE("QuatEqualByValue")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    pushQuat(L, 10, -20, 30, 40);
    pushQuat(L, 10, -20, 30, 40);
    pushQuat(L, 11, -20, 30, 40);
    pushQuat(L, 10, -19, 30, 40);
    pushQuat(L, 10, -20, 31, 40);
    pushQuat(L, 10, -20, 30, 41);

    const TValue* a = L->top - 6;
    const TValue* b = L->top - 5;
    const TValue* dx = L->top - 4;
    const TValue* dy = L->top - 3;
    const TValue* dz = L->top - 2;
    const TValue* ds = L->top - 1;

    CHECK(quatvalue(a) != quatvalue(b));
    CHECK(quatvalue(a)->gcseq != quatvalue(b)->gcseq);
    CHECK(moo_quat_equal(quatvalue(a), quatvalue(b)) == 1);
    CHECK(moo_quat_equal(quatvalue(a), quatvalue(dx)) == 0);
    CHECK(moo_quat_equal(quatvalue(a), quatvalue(dy)) == 0);
    CHECK(moo_quat_equal(quatvalue(a), quatvalue(dz)) == 0);
    CHECK(moo_quat_equal(quatvalue(a), quatvalue(ds)) == 0);

    CHECK(luaO_rawequalObj(a, b) == 1);
    CHECK(luaO_rawequalObj(a, dx) == 0);
    CHECK(luaO_rawequalObj(a, dy) == 0);
    CHECK(luaO_rawequalObj(a, dz) == 0);
    CHECK(luaO_rawequalObj(a, ds) == 0);

    LuaNode node = {};
    setnilvalue(&node.val);
    setnodekey(L, &node, a);
    CHECK(luaO_rawequalKey(&node.key, b) == 1);
    CHECK(luaO_rawequalKey(&node.key, dx) == 0);
    CHECK(luaO_rawequalKey(&node.key, dy) == 0);
    CHECK(luaO_rawequalKey(&node.key, dz) == 0);
    CHECK(luaO_rawequalKey(&node.key, ds) == 0);

    CHECK(lua_rawequal(L, -6, -5) == 1);
    CHECK(lua_rawequal(L, -6, -4) == 0);
    CHECK(lua_rawequal(L, -6, -3) == 0);
    CHECK(lua_rawequal(L, -6, -2) == 0);
    CHECK(lua_rawequal(L, -6, -1) == 0);

    lua_setglobal(L, "ds");
    lua_setglobal(L, "dz");
    lua_setglobal(L, "dy");
    lua_setglobal(L, "dx");
    lua_setglobal(L, "b");
    lua_setglobal(L, "a");
    CHECK(mooRun(L, "return tostring(rawequal(a, b)) .. \",\" .. tostring(rawequal(a, dx)) .. \",\" .. tostring(rawequal(a, dy)) .. \",\" .. tostring(rawequal(a, dz)) .. \",\" .. tostring(rawequal(a, ds)) .. \",\" .. tostring(rawequal(a, 1))") ==
          "true,false,false,false,false,false");

    lua_close(L);
}

// A quat is a value like a number, so two distinct equal quat are one table key (moo_quat_hash routes the tag
// through mainposition and luaO_rawequalKey through rawequalkeyvalue). The hash depends only on the raw
// components, so it is deterministic by construction.
TEST_CASE("QuatTableKeyByValue")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    pushQuat(L, 1, 2, 3, 4);
    pushQuat(L, 1, 2, 3, 4);
    pushQuat(L, 1, 2, 3, 5);

    const TValue* a = L->top - 3;
    const TValue* b = L->top - 2;
    const TValue* c = L->top - 1;

    CHECK(moo_quat_hash(quatvalue(a)) == moo_quat_hash(quatvalue(b)));
    CHECK(moo_quat_hash(quatvalue(a)) != moo_quat_hash(quatvalue(c)));

    lua_newtable(L);
    LuaTable* t = hvalue(L->top - 1);
    TValue* slot = luaH_set(L, t, a);
    setnvalue(slot, 1);
    const TValue* found = luaH_get(t, b);
    REQUIRE(found != luaO_nilobject);
    CHECK(nvalue(found) == 1);
    CHECK(luaH_get(t, c) == luaO_nilobject);

    int visited = 0;
    LuaNode* n = gnode(t, 0);
    for (int i = 0; i < sizenode(t); ++i)
        if (!ttisnil(gval(&n[i])))
            visited++;
    CHECK(visited == 1);

    lua_pop(L, 1);
    lua_setglobal(L, "c");
    lua_setglobal(L, "b");
    lua_setglobal(L, "a");
    CHECK(mooRun(L, "local t = {} t[a] = 7 return tostring(t[b]) .. \",\" .. tostring(t[c])") == "7,nil");

    lua_close(L);
}

// moo_quat_tostring formats "quat(x, y, z, s)" with each component through moo_numtostr, never printing an
// address or creation number, and fits within MOO_QUAT_TOSTR_MAX for the widest Q48.16 values.
TEST_CASE("QuatTostringFormat")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    fixed_t one = MOO_NUMBER_ONE;
    fixed_t minusTwoHalf = -2 * MOO_NUMBER_ONE - MOO_NUMBER_ONE / 2;
    fixed_t tenth = 0;
    REQUIRE(moo_numparse("0.1", &tenth) == MOO_PARSE_OK);

    MooQuat* q = moo_quat_new(L, one, minusTwoHalf, tenth, 0);
    setquatvalue(L, L->top, q);
    incr_top(L);
    CHECK(formatQuat(q) == "quat(1, -2.5, 0.1, 0)");

    MooQuat* zero = moo_quat_new(L, 0, 0, 0, one);
    setquatvalue(L, L->top, zero);
    incr_top(L);
    CHECK(formatQuat(zero) == "quat(0, 0, 0, 1)");

    MooQuat* unit = moo_quat_new(L, 1, -1, 65536, 65536);
    setquatvalue(L, L->top, unit);
    incr_top(L);
    CHECK(formatQuat(unit) == "quat(0.00002, -0.00002, 1, 1)");

    const fixed_t worst = -(int64_t(0x7fffffffffff) << 16) - 1;
    MooQuat* wide = moo_quat_new(L, worst, INT64_MAX, -INT64_MAX, worst);
    setquatvalue(L, L->top, wide);
    incr_top(L);
    std::string wideStr = formatQuat(wide);
    CHECK(wideStr.rfind("quat(", 0) == 0);
    CHECK(wideStr.back() == ')');
    CHECK(wideStr.find('#') == std::string::npos);
    CHECK(wideStr.size() <= MOO_QUAT_TOSTR_MAX);

    lua_close(L);
}

// GC treats MooQuat as a leaf: unreachable objects are swept and freed, a churn of 10^5 objects runs
// destructor-free and leaves zero live quat after a full collect, and weak tables never clear a quat key or
// value (isobjcleared marks LUA_TQUAT like a string).
TEST_CASE("QuatGcLeafAndChurn100k")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    luaC_fullgc(L);
    CHECK(countLiveQuat(L) == 0);
    size_t baselineBytes = L->global->totalbytes;

    pushQuat(L, 1, 2, 3, 4);
    pushQuat(L, 5, 6, 7, 8);
    CHECK(countLiveQuat(L) == 2);
    CHECK(L->global->totalbytes == baselineBytes + 2 * sizeof(MooQuat));
    lua_pop(L, 1);
    luaC_fullgc(L);
    CHECK(countLiveQuat(L) == 1);
    CHECK(quatvalue(L->top - 1)->v.x == 1);
    lua_pop(L, 1);
    luaC_fullgc(L);
    CHECK(countLiveQuat(L) == 0);
    CHECK(L->global->totalbytes == baselineBytes);

    for (int i = 0; i < 100000; ++i)
    {
        MooQuat* q = moo_quat_new(L, fixed_t(i), fixed_t(-i), fixed_t(i * 3), fixed_t(i + 1));
        setquatvalue(L, L->top, q);
        incr_top(L);
        // luaC_step requires the debt to be due (lgc.cpp asserts totalbytes >= GCthreshold in a checked build)
        if (i % 64 == 63 && L->global->totalbytes >= L->global->GCthreshold)
            luaC_step(L, true);
        CHECK(L->global->dtorqueue == nullptr);
        lua_pop(L, 1);
    }
    luaC_fullgc(L);
    CHECK(countLiveQuat(L) == 0);
    CHECK(L->global->totalbytes == baselineBytes);
    CHECK(L->global->dtorqueue == nullptr);

    for (const char* mode : {"k", "v", "kv"})
    {
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, mode);
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);

        LuaTable* h = hvalue(L->top - 1);
        pushQuat(L, 100, 200, 300, 400);
        MooQuat* qk = quatvalue(L->top - 1);
        TValue* slot = luaH_set(L, h, L->top - 1);
        setnvalue(slot, 1);
        lua_pop(L, 1);

        lua_pushliteral(L, "v");
        pushQuat(L, 400, 300, 200, 100);
        lua_settable(L, -3);

        luaC_fullgc(L);
        CHECK(countLiveQuat(L) >= 1);
        CHECK(qk->tt == LUA_TQUAT);
        lua_pop(L, 1);
    }

    lua_close(L);
}

TEST_SUITE_END();
