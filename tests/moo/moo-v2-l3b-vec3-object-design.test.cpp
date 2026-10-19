// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#include "lua.h"
#include "lualib.h"

#include "../VM/src/moo_vec3.h"

#include "doctest.h"

TEST_SUITE_BEGIN("moo-l3b-vec3-object-design");

TEST_CASE("Vec3StructLayoutAndCollectableTag")
{
    CHECK_EQ(sizeof(GCheader), 16u);
    CHECK_EQ(sizeof(moo_Vec3), 40u);
    CHECK_EQ(alignof(moo_Vec3), 8u);
    CHECK_EQ(offsetof(moo_Vec3, x), 16u);
    CHECK_EQ(offsetof(moo_Vec3, y), 24u);
    CHECK_EQ(offsetof(moo_Vec3, z), 32u);
    CHECK_GE(int(LUA_TVEC3), int(LUA_TSTRING));
}

TEST_CASE("Vec3TValueMacrosAndValueEquality")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    moo_Vec3 a{};
    luaC_init(L, (&a), LUA_TVEC3);
    a.x = 1LL << 16;
    a.y = -(2LL << 16);
    a.z = 3LL << 16;

    moo_Vec3 b{};
    luaC_init(L, (&b), LUA_TVEC3);
    b.x = 1LL << 16;
    b.y = -(2LL << 16);
    b.z = 3LL << 16;

    moo_Vec3 c{};
    luaC_init(L, (&c), LUA_TVEC3);
    c.x = 1LL << 16;
    c.y = -(2LL << 16);
    c.z = 4LL << 16;

    // Distinct objects receive distinct monotonic creation numbers, yet compare equal by value.
    CHECK_NE(&a, &b);
    CHECK_EQ(a.gcseq + 1, b.gcseq);
    CHECK_EQ(b.gcseq + 1, c.gcseq);
    CHECK(moo_vec3equal(&a, &b));
    CHECK(!moo_vec3equal(&a, &c));

    TValue tv{};
    setvec3value(L, &tv, &a);
    CHECK(iscollectable(&tv));
    CHECK(ttisvec3(&tv));
    CHECK_EQ(vec3value(&tv), &a);
    CHECK_EQ(vec3value(&tv)->x, 1LL << 16);
    CHECK_EQ(vec3value(&tv)->y, -(2LL << 16));
    CHECK_EQ(vec3value(&tv)->z, 3LL << 16);

    lua_close(L);
}

TEST_SUITE_END();
