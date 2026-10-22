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
// build), so in moo's build it is 14. It is a TValue tag and collectable (at or above LUA_TSTRING), and its name is
// "vec3". moo: the quat card follows it at 15 and transform at 16, so LUA_TDEADKEY is 17 and LUA_T_COUNT 17
// (layer 3b, docs/layer3b/moo-v2-l3b-quat-object.md, docs/layer3b/moo-v2-l3b-transform.md). An upstream merge that adds a type there makes this case fail, so the
// note is read again.
TEST_CASE("Vec3TagSlotIsAfterObject")
{
    CHECK(LUA_VECTOR_DOUBLE == 0);
    CHECK(int(LUA_TOBJECT) == 13);
    CHECK(int(LUA_TVEC3) == 14);
    CHECK(int(LUA_TVEC3) == int(LUA_TOBJECT) + 1);
    CHECK(int(LUA_TQUAT) == int(LUA_TVEC3) + 1);
    CHECK(int(LUA_TTRANSFORM) == int(LUA_TQUAT) + 1);
    CHECK(int(LUA_TDEADKEY) == int(LUA_TTRANSFORM) + 1);
    CHECK(int(LUA_T_COUNT) == 17);
    CHECK(int(LUA_TVEC3) < int(LUA_T_COUNT));
    CHECK(int(LUA_TVEC3) >= int(LUA_TSTRING));
    // a table key's tag lives in TKey::tt, now 5 bits: LUA_TDEADKEY is 17 (note, decision 1, item 4; quat widens it)
    CHECK(int(LUA_TDEADKEY) <= 31);
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
