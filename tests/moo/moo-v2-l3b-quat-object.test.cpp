// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3b quat GC object kind, layout, value equality, table key, tostring and leaf GC
// (docs/DESIGN.md "vec3, quat and transform (layer 3b)"; docs/layer3b/moo-v2-l3b-quat-object.md).
#include "lua.h"
#include "lualib.h"
#include "moo.h"

#include "lapi.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "moo_format.h"
#include "moo_parse.h"
#include "moo_quat.h"

#include "MooHost.h"

#include "doctest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("moo-v2-l3b-quat-object");

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
        if (i % 64 == 63)
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
