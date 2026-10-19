// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3 has no vector VM value; ordinary opcode families continue to operate on fixed numbers and reference values.
#include "lua.h"
#include "lualib.h"

#include "doctest.h"
#include "MooHost.h"

TEST_SUITE_BEGIN("moo-l3-vector-vm-out");

TEST_CASE("VectorRemovalLeavesScalarAndReferenceOpcodesWorking")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK(mooRunFile(L, "l3/moo-l3-vector-vm-out.luau") == "OK");

    lua_close(L);
}

TEST_SUITE_END();
