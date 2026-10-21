// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3 removes the float vector from the C API (docs/DESIGN.md "vec3, quat and transform");
// luaL_openlibs and luaL_sandbox leave no vector-valued field in the VM's globals.
#include "lua.h"
#include "lualib.h"

#include "doctest.h"

TEST_SUITE_BEGIN("moo-l3-vector-api-out");

namespace
{

// Counts vector values in the table at index and, one level down, in its table fields.
// The depth limit keeps a self-reference such as _G._G from cycling.
int countVectorFields(lua_State* L, int index, int depth)
{
    int count = 0;

    lua_pushvalue(L, index);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        if (lua_type(L, -1) == LUA_TVECTOR)
            count++;
        else if (depth > 0 && lua_istable(L, -1))
            count += countVectorFields(L, -1, depth - 1);

        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    return count;
}

void checkNoVectorInGlobals(lua_State* L)
{
    CHECK_EQ(countVectorFields(L, LUA_GLOBALSINDEX, 1), 0);

    lua_getglobal(L, "vector");
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 1);
}

} // namespace

TEST_CASE("OpenlibsLeavesNoVectorInGlobals")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    luaL_openlibs(L);
    checkNoVectorInGlobals(L);

    lua_close(L);
}

TEST_CASE("SandboxLeavesNoVectorInGlobals")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    luaL_openlibs(L);
    luaL_sandbox(L);
    checkNoVectorInGlobals(L);

    lua_close(L);
}

TEST_SUITE_END();
