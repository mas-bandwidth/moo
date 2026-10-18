// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// One test per file: tests/moo/*.test.cpp is built into Moo.Test by the CMake glob, so a layer 5 card
// adds its cases in its own file and never edits tests/Moo.test.cpp (docs/layer3/README.md).
//
// Layer 5 error messages (DETERMINISM.md 20): each case pins the error text of one kind the VM
// raises at the C API boundary. A run error is "error: <chunk>:<line>: <message>"; the chunk and
// line prefix is deterministic but layout-dependent, so each case asserts the message text, and the
// adversarial program tests/determinism/errors.luau pins the full error strings and tracebacks byte
// for byte in the determinism suite.
#include "lua.h"
#include "lualib.h"

#include "doctest.h"
#include "MooHost.h"

TEST_SUITE_BEGIN("moo-l5-errors-tracebacks");

static void checkError(lua_State* L, const char* src, const char* phrase)
{
    std::string result = mooRun(L, src);
    CHECK_MESSAGE(result.rfind("error: ", 0) == 0, src << " did not error, got: " << result);
    CHECK_MESSAGE(result.find(phrase) != std::string::npos, "expected \"" << phrase << "\" in: " << result);
}

// DETERMINISM.md 20: the named moo error for division by zero.
TEST_CASE("ErrorDivisionByZero")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    checkError(L, "return 1 / 0", "division by zero");

    lua_close(L);
}

// DETERMINISM.md 20: a zero step in a numeric for is a named error.
TEST_CASE("ErrorForStepZero")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    checkError(L, "for i = 1, 10, 0 do end", "'for' step is zero");

    lua_close(L);
}

// DETERMINISM.md 20: arithmetic on a nil operand.
TEST_CASE("ErrorArithmeticNil")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    checkError(L, "return 1 + nil", "attempt to perform arithmetic (add) on number and nil");

    lua_close(L);
}

// DETERMINISM.md 20: calling a nil value.
TEST_CASE("ErrorCallNil")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    checkError(L, "local nv; return nv()", "attempt to call a nil value");

    lua_close(L);
}

// DETERMINISM.md 20: indexing a nil value with a string key.
TEST_CASE("ErrorIndexNilString")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    checkError(L, "local t; return t.x", "attempt to index nil with 'x'");

    lua_close(L);
}

// DETERMINISM.md 20: a nil table key.
TEST_CASE("ErrorIndexNilKey")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    checkError(L, "local t = {}; t[nil] = 1", "table index is nil");

    lua_close(L);
}

// DETERMINISM.md 20: an error inside an __index metamethod propagates unchanged.
TEST_CASE("ErrorMetamethodPropagates")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local meta = setmetatable({}, {__index = function() error("meta boom") end})
        return meta.x
    )";
    checkError(L, src, "meta boom");

    lua_close(L);
}

// DETERMINISM.md 20: __len must return a number.
TEST_CASE("ErrorLenMustReturnNumber")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        return #setmetatable({}, {__len = function() return "x" end})
    )";
    checkError(L, src, "'__len' must return a number");

    lua_close(L);
}

// DETERMINISM.md 20: deep recursion ends in a fixed "stack overflow" error.
TEST_CASE("ErrorStackOverflow")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local function rec(n) return rec(n + 1) end
        return rec(0)
    )";
    checkError(L, src, "stack overflow");

    lua_close(L);
}

TEST_SUITE_END();
