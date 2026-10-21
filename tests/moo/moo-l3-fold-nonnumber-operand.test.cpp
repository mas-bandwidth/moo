// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The constant folder leaves an arithmetic or ordering operator alone when an operand is a constant that is not a
// number ("a" + 1, true < 2), and the VM raises the ordinary error at run time. Layer 3 removed upstream's vector
// cases for these operators from foldBinary (Compiler/src/ConstantFolding.cpp), so a checked build asserted
// "Unexpected binary operation" while compiling such an expression (tests/determinism/errors.luau has one).
#include "lua.h"
#include "lualib.h"

#include "MooHost.h"

#include "doctest.h"

#include <string>

TEST_SUITE_BEGIN("moo-l3-fold-nonnumber-operand");

TEST_CASE("FoldLeavesNonNumberOperandsToTheVM")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    struct Case
    {
        const char* source;
        const char* error;
    };
    const Case cases[] = {
        {"return 'a' + 1", "attempt to perform arithmetic (add) on string and number"},
        {"return 1 - true", "attempt to perform arithmetic (sub) on number and boolean"},
        {"return nil * 2", "attempt to perform arithmetic (mul) on nil and number"},
        {"return 'x' / 'y'", "attempt to perform arithmetic (div) on string"},
        {"return true // 1", "attempt to perform arithmetic (idiv) on boolean and number"},
        {"return 1 % false", "attempt to perform arithmetic (mod) on number and boolean"},
        {"return 'b' ^ 2", "attempt to perform arithmetic (pow) on string and number"},
        {"return true < 2", "attempt to compare boolean < number"},
        {"return 1 <= nil", "attempt to compare number <= nil"},
        {"return 'a' > 1", "attempt to compare number < string"},
        {"return false >= 0", "attempt to compare number <= boolean"},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.source);
        std::string got = mooRun(L, c.source);
        CHECK(got.rfind("error: ", 0) == 0);
        CHECK(got.find(c.error) != std::string::npos);
    }

    // a string that reads as a number still converts at run time, as in Luau
    CHECK(mooRun(L, "return '2' + 1") == "3");
    CHECK(mooRun(L, "return '2' < '10'") == "false");

    lua_close(L);
}

TEST_SUITE_END();
