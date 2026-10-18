// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// One test per file: tests/moo/*.test.cpp is built into Moo.Test by the CMake glob, so a layer 3 card
// adds its cases in its own file and never edits tests/Moo.test.cpp (docs/layer3/README.md).
#include "lua.h"
#include "lualib.h"

#include "doctest.h"
#include "MooHost.h"

TEST_SUITE_BEGIN("moo-layout");

// DESIGN.md "Layer 3 as built": number is Q48.16, so 1 + 1 is the raw value for 2.
TEST_CASE("MooLayoutSample")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "return 1 + 1") == "2");

    lua_close(L);
}

TEST_SUITE_END();
