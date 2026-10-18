// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the test harnesses are hosts, and a host's floats cross into the VM only through the boundary
// (moo.h). mooHostNumber rounds a host double by the boundary rule; out of range saturates and NaN gives 0,
// which is what a test that pushes such a value can expect of moo.
#pragma once

#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"

#include "Luau/Compiler.h"

#include "doctest.h"

#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>

inline lua_Number mooHostNumber(double d)
{
    int64_t n = 0;
    int status = moo_fromdouble(d, &n);
    if (status == MOO_NUMBER_OK)
        return n;
    if (status == MOO_NUMBER_NAN)
        return 0;
    return d > 0 ? INT64_MAX : -INT64_MAX;
}

// Compiles and runs source as a chunk; returns its first result as a string, or "error: " and the message.
// DESIGN.md "Layer 3 as built": number is Q48.16 and every operator goes through moo_number.h, so the result
// the host reads at the boundary is the raw value.
inline std::string mooRun(lua_State* L, const char* source)
{
    size_t size = 0;
    char* bytecode = luau_compile(source, strlen(source), nullptr, &size);
    int status = luau_load(L, "=moo", bytecode, size, 0);
    free(bytecode);

    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);

    std::string result;
    if (status != 0)
        result = std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    else
        result = luaL_tolstring(L, -1, nullptr), lua_pop(L, 1);

    lua_pop(L, 1);
    return result;
}

// Runs a moo-specific expected-behaviour file from tests/conformance/moo; it returns "OK".
// DESIGN.md "Layer 3 as built" and DETERMINISM.md "Status": moo's expected-behaviour scripts live under
// tests/conformance/moo, one file per behaviour.
inline std::string mooRunFile(lua_State* L, const char* name)
{
#ifdef LUAU_CONFORMANCE_SOURCE_DIR
    std::string path = std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/moo/" + name;
    std::ifstream file(path, std::ios::binary);
    std::stringstream source;
    source << file.rdbuf();
    REQUIRE_MESSAGE(file.good(), "cannot read " << path);
    return mooRun(L, source.str().c_str());
#else
    return "OK";
#endif
}

namespace moo_test_detail
{

// moo_test.raw(x): the raw Q48.16 value as a decimal string (DESIGN.md "Layer 3 as built", "The number is fixed").
inline int raw(lua_State* L)
{
    lua_Number n = luaL_checknumber(L, 1);
    std::string text = std::to_string(static_cast<long long>(n));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// moo_test.fromraw(s): the number with that raw Q48.16 value (DESIGN.md "Layer 3 as built", "The C API boundary").
inline int fromraw(lua_State* L)
{
    size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    std::string copy(text, length);
    int64_t value = static_cast<int64_t>(strtoll(copy.c_str(), nullptr, 10));
    lua_pushnumber(L, value);
    return 1;
}

// moo_test.compile(src, optlevel): the bytecode string Luau::compile produces at level 0, 1 or 2
// (DESIGN.md "Layer 3 as built", "The compiler": it folds with the same moo_number.h operations).
inline int compile(lua_State* L)
{
    size_t length = 0;
    const char* source = luaL_checklstring(L, 1, &length);
    int level = luaL_optinteger(L, 2, 1);
    if (level < 0 || level > 2)
        luaL_error(L, "optimization level must be 0, 1 or 2");

    Luau::CompileOptions options;
    options.optimizationLevel = level;
    std::string bytecode = Luau::compile(std::string(source, length), options);
    lua_pushlstring(L, bytecode.data(), bytecode.size());
    return 1;
}

// moo_test.load(bytes, name): luau_load's result as a function, or nil and the error string
// (DESIGN.md "Layer 3 as built", "TEMPORARY bridges": bytecode number constants are rounded at load).
inline int load(lua_State* L)
{
    size_t length = 0;
    const char* bytes = luaL_checklstring(L, 1, &length);
    const char* name = luaL_optstring(L, 2, "=moo_test");
    if (luau_load(L, name, bytes, length, 0) == 0)
        return 1;

    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

} // namespace moo_test_detail

// Opens the moo_test helpers in a fresh VM, before a tests/conformance/moo/l3 script runs
// (DESIGN.md "Layer 3 as built": the helpers report the raw value across the C API boundary).
inline void mooOpenTestHelpers(lua_State* L)
{
    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, moo_test_detail::raw, "raw");
    lua_setfield(L, -2, "raw");
    lua_pushcfunction(L, moo_test_detail::fromraw, "fromraw");
    lua_setfield(L, -2, "fromraw");
    lua_pushcfunction(L, moo_test_detail::compile, "compile");
    lua_setfield(L, -2, "compile");
    lua_pushcfunction(L, moo_test_detail::load, "load");
    lua_setfield(L, -2, "load");
    lua_setglobal(L, "moo_test");
}
