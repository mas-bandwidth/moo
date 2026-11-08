// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 3c, representation (N1): the vec2, mat3, plane, aabb and fix30 objects, their tags, layout, value equality,
// table keys, tostring and GC (docs/DESIGN.md "Layer 3c as built"; the key rules are modelled in tla/ValueKeys.tla).

#include "lua.h"
#include "lualib.h"
#include "MooHost.h"
#include "doctest.h"
#include "ltm.h"
#include "lapi.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "lvm.h"
#include "moo_number.h"
#include "moo_vec2.h"
#include "moo_mat3.h"
#include "moo_plane.h"
#include "moo_aabb.h"
#include "moo_fix30.h"

#include <stdint.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("narrow");

namespace narrow_n1
{

namespace
{

const fixed_t kOne = MOO_NUMBER_ONE;

// the frozen key hashes of NarrowHashFrozen and the traversal orders of NarrowKeysOneSlotAndPinnedOrder
const uint64_t kNarrowHashPin = 8582529927171123617ull;
const uint64_t kNarrowOrderPin = 17296621104353019144ull;

// The five kinds, each built from a seed and a perturbation: `differ` changes the last raw component by one, so
// two values from the same seed are equal and a differing one is not.
enum Kind
{
    kVec2,
    kMat3,
    kPlane,
    kAabb,
    kFix30,
    kKinds
};

const int kTags[kKinds] = {LUA_TVEC2, LUA_TMAT3, LUA_TPLANE, LUA_TAABB, LUA_TFIX30};
const char* const kNames[kKinds] = {"vec2", "mat3", "plane", "aabb", "fix30"};
const size_t kSizes[kKinds] = {sizeof(MooVec2), sizeof(MooMat3), sizeof(MooPlane), sizeof(MooAABB), sizeof(MooFix30)};

fixed_t c(int seed, int k)
{
    return fixed_t(seed) * 3 + fixed_t(k) * 7 - 11;
}

void push(lua_State* L, Kind kind, int seed, int differ = 0)
{
    TValue* top = L->top;
    switch (kind)
    {
    case kVec2:
        setvec2value(L, top, moo_vec2_new(L, FIX_LITERAL(fixVec2){c(seed, 0), c(seed, 1) + differ}));
        break;
    case kMat3:
    {
        fixMatrix3 m = {{c(seed, 0), c(seed, 1), c(seed, 2)}, {c(seed, 3), c(seed, 4), c(seed, 5)}, {c(seed, 6), c(seed, 7), c(seed, 8) + differ}};
        setmat3value(L, top, moo_mat3_new(L, m));
        break;
    }
    case kPlane:
        setplanevalue(L, top, moo_plane_new(L, FIX_LITERAL(fixPlane){{c(seed, 0), c(seed, 1), c(seed, 2)}, c(seed, 3) + differ}));
        break;
    case kAabb:
        setaabbvalue(L, top, moo_aabb_new(L, FIX_LITERAL(fixAABB){{c(seed, 0), c(seed, 1), c(seed, 2)}, {c(seed, 3), c(seed, 4), c(seed, 5) + differ}}));
        break;
    case kFix30:
        setfix30value(L, top, moo_fix30_new(L, fixed30_t{int32_t(c(seed, 0) + differ)}));
        break;
    default:
        REQUIRE(false);
    }
    incr_top(L);
}

struct HeapCount
{
    int tag;
    size_t count;
};

bool countVisitor(void* context, lua_Page* page, GCObject* gco)
{
    (void)page;
    HeapCount* h = static_cast<HeapCount*>(context);
    if (gco->gch.tt == h->tag)
        h->count++;
    return false;
}

size_t countLive(lua_State* L, int tag)
{
    HeapCount h = {tag, 0};
    luaM_visitgco(L, &h, countVisitor);
    return h.count;
}

uint64_t fnv(const std::string& s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (char ch : s)
    {
        h ^= uint8_t(ch);
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string tostr(lua_State* L, int idx)
{
    size_t len = 0;
    const char* s = luaL_tolstring(L, idx, &len);
    std::string r(s, len);
    lua_pop(L, 1);
    return r;
}

int nextWrapper(lua_State* L)
{
    if (lua_next(L, 1))
        return 2;
    lua_pushnil(L);
    return 1;
}

struct State
{
    lua_State* L;

    State()
    {
        L = luaL_newstate();
        REQUIRE(L != nullptr);
        luaL_openlibs(L);
    }

    ~State()
    {
        lua_close(L);
    }
};

} // namespace

// The five tags follow transform in this order, before LUA_TDEADKEY, which still fits TKey::tt's 5 bits.
TEST_CASE("NarrowTagsFollowTransform")
{
    CHECK(int(LUA_TVEC2) == int(LUA_TTRANSFORM) + 1);
    CHECK(int(LUA_TMAT3) == int(LUA_TVEC2) + 1);
    CHECK(int(LUA_TPLANE) == int(LUA_TMAT3) + 1);
    CHECK(int(LUA_TAABB) == int(LUA_TPLANE) + 1);
    CHECK(int(LUA_TFIX30) == int(LUA_TAABB) + 1);
    CHECK(int(LUA_TDEADKEY) == int(LUA_TFIX30) + 1);
    CHECK(int(LUA_TDEADKEY) <= 31);
    for (int k = 0; k < kKinds; ++k)
        CHECK(strcmp(luaT_typenames[kTags[k]], kNames[k]) == 0);
}

// Each object is one allocation of its pinned size, with the next creation number; type() names it.
TEST_CASE("NarrowCreateAndLayout")
{
    CHECK(sizeof(MooVec2) == 32);
    CHECK(sizeof(MooMat3) == 88);
    CHECK(sizeof(MooPlane) == 48);
    CHECK(sizeof(MooAABB) == 64);
    CHECK(sizeof(MooFix30) == 24);

    State s;
    lua_State* L = s.L;
    for (int k = 0; k < kKinds; ++k)
    {
        luaC_fullgc(L);
        size_t bytes = L->global->totalbytes;
        uint64_t seq = L->global->gcseq;
        push(L, Kind(k), 1);
        CHECK(lua_type(L, -1) == kTags[k]);
        CHECK(gcvalue(L->top - 1)->gch.gcseq == seq);
        CHECK(moo_objectseq(L, -1) == seq);
        CHECK(L->global->totalbytes == bytes + kSizes[k]);
        CHECK(lua_topointer(L, -1) == nullptr);
        lua_setglobal(L, "x");
        CHECK(mooRun(L, "return type(x)") == kNames[k]);
    }
}

// Equality is by the raw components: two objects of equal bits are equal everywhere (==, rawequal, a table key),
// a one-bit difference in the last component is not, and no kind equals another kind or a number.
TEST_CASE("NarrowEqualByValue")
{
    State s;
    lua_State* L = s.L;
    for (int k = 0; k < kKinds; ++k)
    {
        INFO(kNames[k]);
        push(L, Kind(k), 5);
        push(L, Kind(k), 5);
        push(L, Kind(k), 5, 1);
        const TValue* a = L->top - 3;
        const TValue* b = L->top - 2;
        const TValue* d = L->top - 1;
        CHECK(gcvalue(a) != gcvalue(b));
        CHECK(luaO_rawequalObj(a, b) == 1);
        CHECK(luaO_rawequalObj(a, d) == 0);
        CHECK(luaV_equalval(L, a, b) == 1);
        CHECK(luaV_equalval(L, a, d) == 0);

        LuaNode node = {};
        setnilvalue(&node.val);
        setnodekey(L, &node, a);
        CHECK(luaO_rawequalKey(&node.key, b) == 1);
        CHECK(luaO_rawequalKey(&node.key, d) == 0);

        lua_setglobal(L, "d");
        lua_setglobal(L, "b");
        lua_setglobal(L, "a");
        CHECK(mooRun(L, "return tostring(a == b) .. tostring(a ~= b) .. tostring(a == d) .. tostring(rawequal(a, b)) .. tostring(rawequal(a, d)) .. tostring(a == 1)") ==
              "truefalsefalsetruefalsefalse");
    }

    // no cross-kind equality, even where the payload bits could coincide
    for (int k = 0; k < kKinds; ++k)
        for (int j = 0; j < kKinds; ++j)
        {
            push(L, Kind(k), 0);
            push(L, Kind(j), 0);
            CHECK(luaO_rawequalObj(L->top - 2, L->top - 1) == int(k == j));
            lua_pop(L, 2);
        }
}

// tostring prints the literal-like form with moo's exact formatter: no address and no creation number.
TEST_CASE("NarrowTostring")
{
    State s;
    lua_State* L = s.L;

    setvec2value(L, L->top, moo_vec2_new(L, FIX_LITERAL(fixVec2){kOne, -kOne / 2}));
    incr_top(L);
    CHECK(tostr(L, -1) == "vec2(1, -0.5)");

    fixMatrix3 m = {{kOne, 0, 0}, {0, 2 * kOne, 0}, {0, 0, -3 * kOne}};
    setmat3value(L, L->top, moo_mat3_new(L, m));
    incr_top(L);
    CHECK(tostr(L, -1) == "mat3(vec3(1, 0, 0), vec3(0, 2, 0), vec3(0, 0, -3))");

    setplanevalue(L, L->top, moo_plane_new(L, FIX_LITERAL(fixPlane){{0, kOne, 0}, kOne / 4}));
    incr_top(L);
    CHECK(tostr(L, -1) == "plane(vec3(0, 1, 0), 0.25)");

    setaabbvalue(L, L->top, moo_aabb_new(L, FIX_LITERAL(fixAABB){{-kOne, -kOne, -kOne}, {kOne, kOne, kOne}}));
    incr_top(L);
    CHECK(tostr(L, -1) == "aabb(vec3(-1, -1, -1), vec3(1, 1, 1))");

    setfix30value(L, L->top, moo_fix30_new(L, fixed30_t{1 << 30}));
    incr_top(L);
    CHECK(tostr(L, -1) == "fix30(1)");
    setfix30value(L, L->top, moo_fix30_new(L, fixed30_t{-(1 << 28)}));
    incr_top(L);
    CHECK(tostr(L, -1) == "fix30(-0.25)");
    setfix30value(L, L->top, moo_fix30_new(L, fixed30_t{1}));
    incr_top(L);
    CHECK(tostr(L, -1) == "fix30(0.000000000931322574615478515625)");
    setfix30value(L, L->top, moo_fix30_new(L, fixed30_t{INT32_MIN}));
    incr_top(L);
    CHECK(tostr(L, -1) == "fix30(-2)");
    setfix30value(L, L->top, moo_fix30_new(L, fixed30_t{INT32_MAX}));
    incr_top(L);
    CHECK(tostr(L, -1) == "fix30(1.999999999068677425384521484375)");
    CHECK(strlen("fix30(-1.999999999068677425384521484375)") <= MOO_FIX30_TOSTR_MAX);

    // the widest components fit each kind's bound
    const fixed_t w = -INT64_MAX;
    fixMatrix3 mw = {{w, w, w}, {w, w, w}, {w, w, w}};
    char buf[MOO_MAT3_TOSTR_MAX];
    MooMat3* om = moo_mat3_new(L, mw);
    CHECK(size_t(moo_mat3_tostring(buf, om) - buf) <= MOO_MAT3_TOSTR_MAX);
    char pbuf[MOO_PLANE_TOSTR_MAX];
    CHECK(size_t(moo_plane_tostring(pbuf, moo_plane_new(L, FIX_LITERAL(fixPlane){{w, w, w}, w})) - pbuf) <= MOO_PLANE_TOSTR_MAX);
    char abuf[MOO_AABB_TOSTR_MAX];
    CHECK(size_t(moo_aabb_tostring(abuf, moo_aabb_new(L, FIX_LITERAL(fixAABB){{w, w, w}, {w, w, w}})) - abuf) <= MOO_AABB_TOSTR_MAX);
    char vbuf[MOO_VEC2_TOSTR_MAX];
    CHECK(size_t(moo_vec2_tostring(vbuf, moo_vec2_new(L, FIX_LITERAL(fixVec2){w, w})) - vbuf) <= MOO_VEC2_TOSTR_MAX);
}

// The key hashes are frozen: they fix `next` order, so they are part of the determinism guarantee.
TEST_CASE("NarrowHashFrozen")
{
    State s;
    lua_State* L = s.L;
    std::string got;
    for (int k = 0; k < kKinds; ++k)
        for (int seed : {0, 1, 1000, -77})
        {
            push(L, Kind(k), seed);
            const TValue* v = L->top - 1;
            unsigned int h = 0;
            switch (k)
            {
            case kVec2:
                h = moo_vec2_hash(vec2value(v));
                break;
            case kMat3:
                h = moo_mat3_hash(mat3value(v));
                break;
            case kPlane:
                h = moo_plane_hash(planevalue(v));
                break;
            case kAabb:
                h = moo_aabb_hash(aabbvalue(v));
                break;
            default:
                h = moo_fix30_hash(fix30value(v));
                break;
            }
            got += std::to_string(h) + ",";
            lua_pop(L, 1);
        }
    INFO(got);
    CHECK(fnv(got) == kNarrowHashPin);
}

// Equal bits are one key; a table of each kind iterates in a pinned order after inserts, deletes and a rehash, and
// the split lookup path routes the same way.
TEST_CASE("NarrowKeysOneSlotAndPinnedOrder")
{
    std::string all;
    for (int k = 0; k < kKinds; ++k)
    {
        State s;
        lua_State* L = s.L;
        lua_newtable(L);
        LuaTable* t = hvalue(L->top - 1);

        push(L, Kind(k), 9);
        setnvalue(luaH_set(L, t, L->top - 1), kOne);
        lua_pop(L, 1);
        push(L, Kind(k), 9);
        const TValue* got = luaH_get(t, L->top - 1);
        REQUIRE(ttisnumber(got));
        CHECK(nvalue(got) == kOne);
        lua_pop(L, 1);
        push(L, Kind(k), 9, 1);
        CHECK(luaH_get(t, L->top - 1) == luaO_nilobject);
        lua_pop(L, 1);

        for (int i = 0; i < 2000; ++i)
        {
            push(L, Kind(k), i);
            setnvalue(luaH_set(L, t, L->top - 1), fixed_t(i));
            lua_pop(L, 1);
        }
        for (int i = 0; i < 2000; i += 7)
        {
            push(L, Kind(k), i);
            setnilvalue(luaH_set(L, t, L->top - 1));
            lua_pop(L, 1);
        }
        for (int i = 0; i < 1000; ++i)
        {
            push(L, Kind(k), 100000 + i);
            setnvalue(luaH_set(L, t, L->top - 1), fixed_t(i));
            lua_pop(L, 1);
        }
        luaC_fullgc(L);

        int count = 0;
        lua_pushnil(L);
        while (lua_next(L, 1))
        {
            CHECK(ttype(L->top - 2) == kTags[k]);
            all += std::to_string(nvalue(L->top - 1)) + ",";
            count++;
            lua_pop(L, 1);
        }
        all += "|";
        CHECK(count == 2000 - 286 + 1000);
    }
    INFO(fnv(all));
    CHECK(fnv(all) == kNarrowOrderPin);
}

// The GC treats each kind as a leaf: unreachable objects are freed, a churn runs without destructors and returns
// to the baseline, a weak table never clears one, and an emptied weak entry's key dies before its object is freed,
// after which next() accepts only the same object.
TEST_CASE("NarrowGcLeafWeakAndDeadKeys")
{
    for (int k = 0; k < kKinds; ++k)
    {
        INFO(kNames[k]);
        State s;
        lua_State* L = s.L;
        luaC_fullgc(L);
        size_t baseline = L->global->totalbytes;
        CHECK(countLive(L, kTags[k]) == 0);

        push(L, Kind(k), 1);
        push(L, Kind(k), 2);
        CHECK(countLive(L, kTags[k]) == 2);
        lua_pop(L, 2);
        luaC_fullgc(L);
        CHECK(countLive(L, kTags[k]) == 0);
        CHECK(L->global->totalbytes == baseline);

        for (int i = 0; i < 20000; ++i)
        {
            push(L, Kind(k), i);
            if (i % 64 == 63 && L->global->totalbytes >= L->global->GCthreshold)
                luaC_step(L, true);
            lua_pop(L, 1);
        }
        luaC_fullgc(L);
        CHECK(countLive(L, kTags[k]) == 0);
        CHECK(L->global->totalbytes == baseline);
        CHECK(L->global->dtorqueue == nullptr);

        for (const char* mode : {"k", "v", "kv"})
        {
            lua_newtable(L);
            lua_newtable(L);
            lua_pushstring(L, mode);
            lua_setfield(L, -2, "__mode");
            lua_setmetatable(L, -2);
            LuaTable* h = hvalue(L->top - 1);
            push(L, Kind(k), 3);
            TValue* slot = luaH_set(L, h, L->top - 1);
            lua_pop(L, 1);
            push(L, Kind(k), 4);
            setobj2t(L, slot, L->top - 1);
            lua_pop(L, 1);
            luaC_fullgc(L);
            CHECK(countLive(L, kTags[k]) == 2);
            push(L, Kind(k), 3);
            const TValue* got = luaH_get(h, L->top - 1);
            REQUIRE(ttype(got) == kTags[k]);
            lua_pop(L, 2);
            luaC_fullgc(L);
            CHECK(countLive(L, kTags[k]) == 0);
        }

        // dead key: index 1 the weak table, index 2 the key object, kept alive on the stack
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, "k");
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);
        LuaTable* t = hvalue(L->top - 1);
        push(L, Kind(k), 7);
        GCObject* key = gcvalue(L->top - 1);
        TValue* slot = luaH_set(L, t, L->top - 1);
        setnvalue(slot, kOne);
        setnilvalue(slot);
        luaC_fullgc(L);
        int dead = 0;
        for (int i = 0; i < sizenode(t); ++i)
            dead += ttype(gkey(gnode(t, i))) == LUA_TDEADKEY;
        CHECK(dead == 1);

        lua_pushcfunction(L, nextWrapper, "nextWrapper");
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        CHECK(lua_pcall(L, 2, 1, 0) == 0);
        CHECK(lua_isnil(L, -1));
        lua_pop(L, 1);

        push(L, Kind(k), 7);
        REQUIRE(gcvalue(L->top - 1) != key);
        lua_pushcfunction(L, nextWrapper, "nextWrapper");
        lua_pushvalue(L, 1);
        lua_pushvalue(L, -3);
        CHECK(lua_pcall(L, 2, 1, 0) != 0);
        CHECK(strstr(lua_tostring(L, -1), "invalid key to 'next'") != nullptr);
        lua_pop(L, 2);
        lua_pop(L, 2);
        luaC_fullgc(L);
        CHECK(L->global->totalbytes == baseline);
    }
}

} // namespace narrow_n1

TEST_SUITE_END();
