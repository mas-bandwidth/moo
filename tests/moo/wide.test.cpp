// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 3d, representation (W1): the wide, poswide, worldtransformwide, aabbwide and time objects, their tags, layout,
// value equality, table keys, tostring and GC (docs/DESIGN.md "Layer 3d as built"; the key rules are modelled in
// tla/ValueKeys.tla). Built as tests/moo/narrow.test.cpp is, kind for kind.

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
#include "moo_wide.h"

#include <stdint.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("wide");

namespace wide_w1
{

namespace
{

const fixed_t kOne = MOO_NUMBER_ONE;

// the frozen key hashes of WideHashFrozen and the traversal orders of WideKeysOneSlotAndPinnedOrder
const uint64_t kWideHashPin = 3675161459263674704ull;
const uint64_t kWideOrderPin = 16249484993737455052ull;

// The five kinds, each built from a seed and a perturbation: `differ` changes the last raw component by one, so
// two values from the same seed are equal and a differing one is not.
enum Kind
{
    kWide,
    kPosWide,
    kWorldTransformWide,
    kAabbWide,
    kTime,
    kKinds
};

const int kTags[kKinds] = {LUA_TWIDE, LUA_TPOSWIDE, LUA_TWORLDTRANSFORMWIDE, LUA_TAABBWIDE, LUA_TTIME};
const char* const kNames[kKinds] = {"wide", "poswide", "worldtransformwide", "aabbwide", "time"};
const size_t kSizes[kKinds] = {sizeof(MooFixedWide), sizeof(MooPosWide), sizeof(MooWorldTransformWide), sizeof(MooAABBWide), sizeof(MooTime)};

fixed_t c(int seed, int k)
{
    return fixed_t(seed) * 3 + fixed_t(k) * 7 - 11;
}

// a wide coordinate from a seed: both limbs carry it, so the high limb matters to equality and the hash
fixedWide_t w(int seed, int k)
{
    int64_t v = c(seed, k);
    return fixInt128Make(uint64_t(v * 5 + 1), uint64_t(v));
}

fixPosWide pw(int seed, int k, int differ)
{
    fixPosWide p = {w(seed, k), w(seed, k + 1), fixWideOffset(w(seed, k + 2), differ)};
    return p;
}

void push(lua_State* L, Kind kind, int seed, int differ = 0)
{
    TValue* top = L->top;
    switch (kind)
    {
    case kWide:
        setwidevalue(L, top, moo_wide_new(L, fixWideOffset(w(seed, 0), differ)));
        break;
    case kPosWide:
        setposwidevalue(L, top, moo_poswide_new(L, pw(seed, 0, differ)));
        break;
    case kWorldTransformWide:
    {
        fixWorldTransformWide t = {pw(seed, 0, 0), {{c(seed, 3), c(seed, 4), c(seed, 5)}, c(seed, 6) + differ}};
        setworldtransformwidevalue(L, top, moo_worldtransformwide_new(L, t));
        break;
    }
    case kAabbWide:
    {
        fixAABBWide b = {pw(seed, 0, 0), pw(seed, 3, differ)};
        setaabbwidevalue(L, top, moo_aabbwide_new(L, b));
        break;
    }
    case kTime:
        settimevalue(L, top, moo_time_new(L, fixTime(c(seed, 0)) * 65536 + differ));
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

// The five tags follow layer 3c's fix30 in this order, before LUA_TDEADKEY, which still fits TKey::tt's 5 bits.
TEST_CASE("WideTagsFollowFix30")
{
    CHECK(int(LUA_TWIDE) == int(LUA_TFIX30) + 1);
    CHECK(int(LUA_TPOSWIDE) == int(LUA_TWIDE) + 1);
    CHECK(int(LUA_TWORLDTRANSFORMWIDE) == int(LUA_TPOSWIDE) + 1);
    CHECK(int(LUA_TAABBWIDE) == int(LUA_TWORLDTRANSFORMWIDE) + 1);
    CHECK(int(LUA_TTIME) == int(LUA_TAABBWIDE) + 1);
    CHECK(int(LUA_TDEADKEY) == int(LUA_TTIME) + 1);
    CHECK(int(LUA_TDEADKEY) <= 31);
    for (int k = 0; k < kKinds; ++k)
        CHECK(strcmp(luaT_typenames[kTags[k]], kNames[k]) == 0);
}

// Each object is one allocation of its pinned size, with the next creation number; type() names it.
TEST_CASE("WideCreateAndLayout")
{
    CHECK(sizeof(MooFixedWide) == 32);
    CHECK(sizeof(MooPosWide) == 64);
    CHECK(sizeof(MooWorldTransformWide) == 96);
    CHECK(sizeof(MooAABBWide) == 112);
    CHECK(sizeof(MooTime) == 24);

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
TEST_CASE("WideEqualByValue")
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

// tostring prints each value as its literal (1.5w, 0.25s) or literal-like form with the exact formatter: no address
// and no creation number.
TEST_CASE("WideTostring")
{
    State s;
    lua_State* L = s.L;

    setwidevalue(L, L->top, moo_wide_new(L, fixWideFromFixed(kOne + kOne / 2)));
    incr_top(L);
    CHECK(tostr(L, -1) == "1.5w");
    setwidevalue(L, L->top, moo_wide_new(L, fixInt128Make(1, 0)));
    incr_top(L);
    CHECK(tostr(L, -1) == "281474976710656w");
    setwidevalue(L, L->top, moo_wide_new(L, FIX_WIDE_MIN));
    incr_top(L);
    CHECK(tostr(L, -1) == "-2596148429267413814265248164610047.99998w");

    settimevalue(L, L->top, moo_time_new(L, FIX_TIME_ONE / 4));
    incr_top(L);
    CHECK(tostr(L, -1) == "0.25s");
    settimevalue(L, L->top, moo_time_new(L, -FIX_TIME_ONE * 3));
    incr_top(L);
    CHECK(tostr(L, -1) == "-3s");
    settimevalue(L, L->top, moo_time_new(L, 1));
    incr_top(L);
    CHECK(tostr(L, -1) == "0.0000000002s");

    fixPosWide p = {fixWideFromFixed(kOne), fixWideFromFixed(-2 * kOne), fixInt128Make(1, 0)};
    setposwidevalue(L, L->top, moo_poswide_new(L, p));
    incr_top(L);
    CHECK(tostr(L, -1) == "poswide(1w, -2w, 281474976710656w)");

    fixWorldTransformWide t = {p, {{0, 0, 0}, kOne}};
    setworldtransformwidevalue(L, L->top, moo_worldtransformwide_new(L, t));
    incr_top(L);
    CHECK(tostr(L, -1) == "worldtransformwide(poswide(1w, -2w, 281474976710656w), quat(0, 0, 0, 1))");

    fixAABBWide b = {p, p};
    setaabbwidevalue(L, L->top, moo_aabbwide_new(L, b));
    incr_top(L);
    CHECK(tostr(L, -1) == "aabbwide(poswide(1w, -2w, 281474976710656w), poswide(1w, -2w, 281474976710656w))");

    // the widest components fit each kind's bound
    fixedWide_t m = FIX_WIDE_MIN;
    fixPosWide pm = {m, m, m};
    char wbuf[MOO_WIDE_TOSTR_MAX];
    CHECK(size_t(moo_wide_tostring(wbuf, moo_wide_new(L, m)) - wbuf) <= MOO_WIDE_TOSTR_MAX);
    char pbuf[MOO_POSWIDE_TOSTR_MAX];
    CHECK(size_t(moo_poswide_tostring(pbuf, moo_poswide_new(L, pm)) - pbuf) <= MOO_POSWIDE_TOSTR_MAX);
    const fixed_t n = -INT64_MAX;
    fixWorldTransformWide tm = {pm, {{n, n, n}, n}};
    char tbuf[MOO_WORLDTRANSFORMWIDE_TOSTR_MAX];
    CHECK(size_t(moo_worldtransformwide_tostring(tbuf, moo_worldtransformwide_new(L, tm)) - tbuf) <= MOO_WORLDTRANSFORMWIDE_TOSTR_MAX);
    fixAABBWide bm = {pm, pm};
    char bbuf[MOO_AABBWIDE_TOSTR_MAX];
    CHECK(size_t(moo_aabbwide_tostring(bbuf, moo_aabbwide_new(L, bm)) - bbuf) <= MOO_AABBWIDE_TOSTR_MAX);
    char ibuf[MOO_TIME_TOSTR_MAX];
    CHECK(size_t(moo_time_tostring(ibuf, moo_time_new(L, FIX_TIME_MIN)) - ibuf) <= MOO_TIME_TOSTR_MAX);
}

// The key hashes are frozen: they fix `next` order, so they are part of the determinism guarantee.
TEST_CASE("WideHashFrozen")
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
            case kWide:
                h = moo_wide_hash(widevalue(v));
                break;
            case kPosWide:
                h = moo_poswide_hash(poswidevalue(v));
                break;
            case kWorldTransformWide:
                h = moo_worldtransformwide_hash(worldtransformwidevalue(v));
                break;
            case kAabbWide:
                h = moo_aabbwide_hash(aabbwidevalue(v));
                break;
            default:
                h = moo_time_hash(timevalue(v));
                break;
            }
            got += std::to_string(h) + ",";
            lua_pop(L, 1);
        }
    INFO(got);
    CHECK(fnv(got) == kWideHashPin);
}

// Equal bits are one key; a table of each kind iterates in a pinned order after inserts, deletes and a rehash, and
// the split lookup path routes the same way.
TEST_CASE("WideKeysOneSlotAndPinnedOrder")
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
    CHECK(fnv(all) == kWideOrderPin);
}

// The GC treats each kind as a leaf: unreachable objects are freed, a churn runs without destructors and returns
// to the baseline, a weak table never clears one, and an emptied weak entry's key dies before its object is freed,
// after which next() accepts only the same object.
TEST_CASE("WideGcLeafWeakAndDeadKeys")
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

} // namespace wide_w1

TEST_SUITE_END();
