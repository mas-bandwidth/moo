// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3b vec3 as a table key: component hashing, main-position and key-equality routing, and the
// deterministic iteration order of a vec3-keyed table
// (docs/DESIGN.md "vec3, quat and transform (layer 3b)"; docs/layer3b/moo-v2-l3b-vec3-object.md,
// "Card moo-v2-l3b-vec3-object-key").
#include "lua.h"
#include "lualib.h"
#include "moo.h"

#include "lapi.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "moo_vec3.h"

#include "MooHost.h"
#include "ScopedFlags.h"

#include "doctest.h"

#include <stdint.h>
#include <string.h>
#include <string>

LUAU_DYNAMIC_FASTFLAG(LuauSplitTableLookups)

TEST_SUITE_BEGIN("moo-v2-l3b-vec3-object-key");

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
    lua_pop(L, 1);

    pushVec3(L, 5 * MOO_NUMBER_ONE, -6 * MOO_NUMBER_ONE, 7 * MOO_NUMBER_ONE);
    REQUIRE(vec3value(L->top - 1) != vec3value(L->top - 2));
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
// equal, and the values below are pinned by hash (docs/layer3b/moo-v2-l3b-vec3-object.md, decision 4).
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

TEST_SUITE_END();
