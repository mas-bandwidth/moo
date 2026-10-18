// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The analyzer's builtin definitions drop os, vector and integer (docs/DESIGN.md "Analyzer");
// DONE-WHEN runs build/moo-analyze on vector.create and expects an unknown global. Moo.Test links
// the VM and the compiler, not the analyzer, so these cases pin the VM side of that seam: the
// globals the analyzer must not declare are absent from the VM in the same build.
#include "lua.h"
#include "lualib.h"

#include "doctest.h"
#include "MooHost.h"

TEST_SUITE_BEGIN("moo-l3-analyzer-builtins");

// DESIGN.md "Analyzer": the builtin definitions drop os and the integer library; DETERMINISM.md
// "Status" 9 says the os library is deleted. Neither name resolves in the VM.
TEST_CASE("OsAndIntegerAreNotGlobals")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "return rawget(_G, 'os') == nil") == "true");
    CHECK(mooRun(L, "return rawget(_G, 'integer') == nil") == "true");

    lua_close(L);
}

// DESIGN.md "Analyzer": both analyzer and VM omit the vector global after moo-l3-libs-out.
TEST_CASE("VectorIsNotAVmGlobal")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "return rawget(_G, 'vector') == nil") == "true");

    lua_close(L);
}

TEST_SUITE_END();
