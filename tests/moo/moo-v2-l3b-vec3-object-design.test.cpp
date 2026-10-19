// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec3 representation decision of layer 3b (docs/layer3b/moo-v2-l3b-vec3-object.md). Nothing creates a
// vec3 yet, so these cases pin only what the decision fixes statically: the object layout and the tag's slot.
#include "lua.h"
#include "lualib.h"

#include "moo_vec3.h"
#include "moo_format.h"

#include "doctest.h"

#include <stddef.h>
#include <stdint.h>

TEST_SUITE_BEGIN("moo-v2-l3b-vec3-object-design");

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

// Note, decision 1: LUA_TVEC3 goes right after LUA_TOBJECT (and after the LUA_VECTOR_DOUBLE == 1 arm moo does not
// build), before LUA_TDEADKEY, so in moo's build it is 14, today's LUA_T_COUNT. It is a TValue tag (below
// LUA_T_COUNT) and collectable (at or above LUA_TSTRING). This card does not add it (the note says why); the case
// pins the slot the note names, so an upstream merge that adds a type there makes the note be read again.
TEST_CASE("Vec3TagSlotIsAfterObject")
{
    CHECK(LUA_VECTOR_DOUBLE == 0);
    CHECK(int(LUA_TOBJECT) == 13);
    CHECK(int(LUA_TDEADKEY) == int(LUA_TOBJECT) + 1);
    CHECK(int(LUA_T_COUNT) == int(LUA_TDEADKEY));
    CHECK(int(LUA_TOBJECT) + 1 >= int(LUA_TSTRING));
    // a table key's tag lives in TKey::tt, 4 bits: with vec3 added, LUA_TDEADKEY becomes 15, the last value it holds
    CHECK(int(LUA_TDEADKEY) + 1 <= 15);
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

TEST_SUITE_END();
