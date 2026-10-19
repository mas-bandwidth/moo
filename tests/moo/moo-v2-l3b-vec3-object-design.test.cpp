// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec3 representation decision of layer 3b (docs/layer3b/moo-v2-l3b-vec3-object.md). Nothing creates a
// vec3 yet, so these cases pin only what the decision fixes statically: the object layout, the tag's value and its name.
#include "lua.h"
#include "lualib.h"

#include "moo_vec3.h"
#include "ltm.h"
#include "moo_format.h"

#include "doctest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

// Note, decision 1: LUA_TVEC3 is right after LUA_TOBJECT (and after the LUA_VECTOR_DOUBLE == 1 arm moo does not
// build), before LUA_TDEADKEY, so in moo's build it is 14. It is a TValue tag (below LUA_T_COUNT, now 15) and
// collectable (at or above LUA_TSTRING), and its name is "vec3". An upstream merge that adds a type there makes this
// case fail, so the note is read again.
TEST_CASE("Vec3TagSlotIsAfterObject")
{
    CHECK(LUA_VECTOR_DOUBLE == 0);
    CHECK(int(LUA_TOBJECT) == 13);
    CHECK(int(LUA_TVEC3) == 14);
    CHECK(int(LUA_TVEC3) == int(LUA_TOBJECT) + 1);
    CHECK(int(LUA_TDEADKEY) == int(LUA_TVEC3) + 1);
    CHECK(int(LUA_T_COUNT) == 15);
    CHECK(int(LUA_TVEC3) < int(LUA_T_COUNT));
    CHECK(int(LUA_TVEC3) >= int(LUA_TSTRING));
    // a table key's tag lives in TKey::tt, 4 bits: LUA_TDEADKEY is now 15, the last value it holds (note, decision 1, item 4)
    CHECK(int(LUA_TDEADKEY) <= 15);
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

TEST_SUITE_END();
