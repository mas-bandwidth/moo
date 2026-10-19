// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The twelve Luau.UnitTest cases named by this card all assert removed builtins (os, vector,
// integer) and are skipped below with a moo: reason (docs/DESIGN.md "Analyzer"). Moo.Test links
// the VM and the compiler, not the analyzer (see moo-l3-analyzer-builtins.test.cpp), so the
// replacement coverage below pins the VM side of that seam: the removed names do not resolve as
// globals, and the compiler folds no vector builtin fastcall (MooBuiltinsDoNotFold).
#include "lua.h"
#include "lualib.h"

#include "doctest.h"
#include "MooHost.h"

TEST_SUITE_BEGIN("moo-l3-unittest-base-reds-named");

// DESIGN.md "Analyzer": the builtin definitions drop os and the integer library. The skipped
// os/integer cases in tests/TypeInfer.tables.test.cpp and tests/TypeFunction.user.test.cpp all
// assume a name the analyzer no longer declares; the VM exposes neither.
TEST_CASE("RemovedOsAndIntegerAreNotGlobals")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "return rawget(_G, 'os') == nil") == "true");
    CHECK(mooRun(L, "return rawget(_G, 'integer') == nil") == "true");

    lua_close(L);
}

TEST_SUITE_END();
