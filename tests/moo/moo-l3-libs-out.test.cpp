// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo-l3-libs-out: luaL_openlibs opens no vector and no integer library, whatever the integer flag says
// (docs/DESIGN.md, "Layer 3 as built").
#include "lua.h"
#include "lualib.h"
#include "moo.h"

#include "doctest.h"
#include "ScopedFlags.h"

LUAU_FASTFLAG(LuauIntegerLibrary)
LUAU_FASTFLAG(LuauIntegerType2)

TEST_CASE("OpenlibsOpensNoVectorOrInteger")
{
    // Even with the integer flag and type on, the integer library stays out; vector is always out.
    ScopedFastFlag integerLibrary{FFlag::LuauIntegerLibrary, true};
    ScopedFastFlag integerType{FFlag::LuauIntegerType2, true};

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    lua_getglobal(L, "vector");
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 1);

    lua_getglobal(L, "integer");
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 1);

    lua_close(L);
}
