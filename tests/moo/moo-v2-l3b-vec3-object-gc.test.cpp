// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3b vec3 GC object kind, layout, value equality, tostring and leaf GC
// (docs/DESIGN.md "vec3, quat and transform (layer 3b)"; docs/layer3b/moo-v2-l3b-vec3-object.md,
// "Card moo-v2-l3b-vec3-object-gc").
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
#include "moo_vec3.h"

#include "MooHost.h"

#include "doctest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("moo-v2-l3b-vec3-object-gc");

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
        if (i % 64 == 63)
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

TEST_SUITE_END();
