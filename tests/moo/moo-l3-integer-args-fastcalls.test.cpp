// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: DESIGN.md "Exact integers": a fastcall in VM/src/lbuiltins.cpp that converts a number argument to an
// integer checks luai_numisint first and returns -1 on a fraction so the library path raises "number has no
// integer representation", matching -O0 (no fastcall) and -O1/-O2 (fastcall).
#include "lua.h"
#include "lualib.h"
#include "moo.h"

#include "Luau/Compiler.h"

#include "doctest.h"
#include "MooHost.h"

#include <string>

TEST_SUITE_BEGIN("moo-l3-integer-args-fastcalls");

namespace
{

std::string runAtOpt(const std::string& source, int optLevel)
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    luaL_sandbox(L);
    luaL_sandboxthread(L);

    Luau::CompileOptions options;
    options.optimizationLevel = optLevel;
    std::string bytecode = Luau::compile(source, options);

    int status = luau_load(L, "=moo", bytecode.data(), bytecode.size(), 0);
    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);

    std::string result;
    if (status != 0)
        result = std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    else
        result = luaL_tolstring(L, -1, nullptr), lua_pop(L, 1);

    lua_pop(L, 1);
    lua_close(L);
    return result;
}

struct FastcallCase
{
    const char* name;
    const char* badCall;
    const char* badMessage;
    const char* okCall;
    const char* okExpected;
};

} // namespace

// DESIGN.md "Exact integers": every fastcall-able builtin that takes an integer refuses 1.5 with the same
// error text and accepts 2 with the same result at -O0, -O1 and -O2.
TEST_CASE("FastcallIntegerArgsMatchAcrossOptimizationLevels")
{
    const FastcallCase cases[] = {
        // bit32.*
        {"bit32.arshift arg 1", "return bit32.arshift(1.5, 2)", "invalid argument #1 to 'arshift' (number has no integer representation)", "return bit32.arshift(8, 2)", "2"},
        {"bit32.arshift arg 2", "return bit32.arshift(8, 1.5)", "invalid argument #2 to 'arshift' (number has no integer representation)", "return bit32.arshift(2, 1)", "1"},
        {"bit32.band arg 1", "return bit32.band(1.5, 2)", "invalid argument #1 to 'band' (number has no integer representation)", "return bit32.band(3, 2)", "2"},
        {"bit32.band arg 2", "return bit32.band(3, 1.5)", "invalid argument #2 to 'band' (number has no integer representation)", "return bit32.band(2, 3)", "2"},
        {"bit32.band arg 3", "return bit32.band(7, 3, 1.5)", "invalid argument #3 to 'band' (number has no integer representation)", "return bit32.band(7, 3, 2)", "2"},
        {"bit32.bnot", "return bit32.bnot(1.5)", "invalid argument #1 to 'bnot' (number has no integer representation)", "return bit32.bnot(2)", "4294967293"},
        {"bit32.bor arg 1", "return bit32.bor(1.5, 2)", "invalid argument #1 to 'bor' (number has no integer representation)", "return bit32.bor(1, 2)", "3"},
        {"bit32.bor arg 2", "return bit32.bor(1, 1.5)", "invalid argument #2 to 'bor' (number has no integer representation)", "return bit32.bor(2, 4)", "6"},
        {"bit32.bor arg 3", "return bit32.bor(1, 2, 1.5)", "invalid argument #3 to 'bor' (number has no integer representation)", "return bit32.bor(1, 4, 2)", "7"},
        {"bit32.bxor arg 1", "return bit32.bxor(1.5, 2)", "invalid argument #1 to 'bxor' (number has no integer representation)", "return bit32.bxor(3, 2)", "1"},
        {"bit32.bxor arg 2", "return bit32.bxor(3, 1.5)", "invalid argument #2 to 'bxor' (number has no integer representation)", "return bit32.bxor(2, 1)", "3"},
        {"bit32.bxor arg 3", "return bit32.bxor(7, 1, 1.5)", "invalid argument #3 to 'bxor' (number has no integer representation)", "return bit32.bxor(7, 1, 2)", "4"},
        {"bit32.btest arg 1", "return bit32.btest(1.5, 2)", "invalid argument #1 to 'btest' (number has no integer representation)", "return bit32.btest(3, 2)", "true"},
        {"bit32.btest arg 2", "return bit32.btest(3, 1.5)", "invalid argument #2 to 'btest' (number has no integer representation)", "return bit32.btest(2, 1)", "false"},
        {"bit32.btest arg 3", "return bit32.btest(7, 3, 1.5)", "invalid argument #3 to 'btest' (number has no integer representation)", "return bit32.btest(7, 3, 2)", "true"},
        {"bit32.extract 2-arg arg 1", "local f = 1 return bit32.extract(1.5, f)", "invalid argument #1 to 'extract' (number has no integer representation)", "local f = 1 return bit32.extract(2, f)", "1"},
        {"bit32.extract 2-arg arg 2", "return bit32.extract(6, 1.5)", "invalid argument #2 to 'extract' (number has no integer representation)", "return bit32.extract(6, 2)", "1"},
        {"bit32.extract 3-arg nonconst arg 3", "local f, w = 1, 1.5 f, w = f, w return bit32.extract(6, f, w)", "invalid argument #3 to 'extract' (number has no integer representation)", "local f, w = 1, 2 f, w = f, w return bit32.extract(6, f, w)", "3"},
        {"bit32.extract FASTCALL2K arg 1", "return bit32.extract(1.5, 1, 2)", "invalid argument #1 to 'extract' (number has no integer representation)", "return bit32.extract(6, 1, 2)", "3"},
        {"bit32.extract 3-arg nonconst arg 2", "local f, w = 1.5, 2 f, w = f, w return bit32.extract(6, f, w)", "invalid argument #2 to 'extract' (number has no integer representation)", "local f, w = 2, 2 f, w = f, w return bit32.extract(12, f, w)", "3"},
        {"bit32.lrotate arg 1", "return bit32.lrotate(1.5, 2)", "invalid argument #1 to 'lrotate' (number has no integer representation)", "return bit32.lrotate(1, 2)", "4"},
        {"bit32.lrotate arg 2", "return bit32.lrotate(1, 1.5)", "invalid argument #2 to 'lrotate' (number has no integer representation)", "return bit32.lrotate(2, 1)", "4"},
        {"bit32.lshift arg 1", "return bit32.lshift(1.5, 2)", "invalid argument #1 to 'lshift' (number has no integer representation)", "return bit32.lshift(1, 2)", "4"},
        {"bit32.lshift arg 2", "return bit32.lshift(1, 1.5)", "invalid argument #2 to 'lshift' (number has no integer representation)", "return bit32.lshift(2, 1)", "4"},
        {"bit32.replace 3-arg arg 1", "local v = 1 return bit32.replace(1.5, v, 2)", "invalid argument #1 to 'replace' (number has no integer representation)", "local v = 1 return bit32.replace(0, v, 2)", "4"},
        {"bit32.replace 3-arg arg 2", "local n = 0 return bit32.replace(n, 1.5, 2)", "invalid argument #2 to 'replace' (number has no integer representation)", "local n = 0 return bit32.replace(n, 2, 1)", "0"},
        {"bit32.replace 3-arg arg 3", "local n = 0 return bit32.replace(n, 1, 1.5)", "invalid argument #3 to 'replace' (number has no integer representation)", "local n = 0 return bit32.replace(n, 1, 2)", "4"},
        {"bit32.replace 4-arg arg 4", "return bit32.replace(0, 3, 1, 1.5)", "invalid argument #4 to 'replace' (number has no integer representation)", "return bit32.replace(0, 3, 1, 2)", "6"},
        {"bit32.rrotate arg 1", "return bit32.rrotate(1.5, 2)", "invalid argument #1 to 'rrotate' (number has no integer representation)", "return bit32.rrotate(8, 2)", "2"},
        {"bit32.rrotate arg 2", "return bit32.rrotate(8, 1.5)", "invalid argument #2 to 'rrotate' (number has no integer representation)", "return bit32.rrotate(4, 2)", "1"},
        {"bit32.rshift arg 1", "return bit32.rshift(1.5, 2)", "invalid argument #1 to 'rshift' (number has no integer representation)", "return bit32.rshift(8, 2)", "2"},
        {"bit32.rshift arg 2", "return bit32.rshift(8, 1.5)", "invalid argument #2 to 'rshift' (number has no integer representation)", "return bit32.rshift(2, 1)", "1"},
        {"bit32.countlz", "return bit32.countlz(1.5)", "invalid argument #1 to 'countlz' (number has no integer representation)", "return bit32.countlz(2)", "30"},
        {"bit32.countrz", "return bit32.countrz(1.5)", "invalid argument #1 to 'countrz' (number has no integer representation)", "return bit32.countrz(2)", "1"},
        {"bit32.byteswap", "return bit32.byteswap(1.5)", "invalid argument #1 to 'byteswap' (number has no integer representation)", "return bit32.byteswap(2)", "33554432"},

        // string.byte, string.char, string.sub
        {"string.byte 2-arg", "local s = 'abc' return string.byte(s, 1.5)", "invalid argument #2 to 'byte' (number has no integer representation)", "local s = 'abc' return string.byte(s, 2)", "98"},
        {"string.byte 3-arg", "local s = 'abc' return string.byte(s, 1, 1.5)", "invalid argument #3 to 'byte' (number has no integer representation)", "local s = 'abc' return string.byte(s, 2, 2)", "98"},
        {"string.char arg 1", "return string.char(1.5)", "invalid argument #1 to 'char' (number has no integer representation)", "return string.char(2)", "\x02"},
        {"string.char arg 2", "return string.char(65, 1.5)", "invalid argument #2 to 'char' (number has no integer representation)", "return string.char(65, 2)", "A\x02"},
        {"string.sub arg 2", "local s = 'abcd' return string.sub(s, 1.5, 3)", "invalid argument #2 to 'sub' (number has no integer representation)", "local s = 'abcd' return string.sub(s, 2, 3)", "bc"},
        {"string.sub arg 3", "local s = 'abcd' return string.sub(s, 1, 1.5)", "invalid argument #3 to 'sub' (number has no integer representation)", "local s = 'abcd' return string.sub(s, 1, 2)", "ab"},

        // buffer reads and writes
        {"buffer.readi8", "local b = buffer.create(16) return buffer.readi8(b, 1.5)", "invalid argument #2 to 'readi8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 2, -5) return buffer.readi8(b, 2)", "-5"},
        {"buffer.readu8", "local b = buffer.create(16) return buffer.readu8(b, 1.5)", "invalid argument #2 to 'readu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 2, 7) return buffer.readu8(b, 2)", "7"},
        {"buffer.writei8 offset", "local b = buffer.create(16) buffer.writei8(b, 1.5, 2) return 0", "invalid argument #2 to 'writei8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 2, 2) return buffer.readi8(b, 2)", "2"},
        {"buffer.writei8 value", "local b = buffer.create(16) buffer.writei8(b, 2, 1.5) return 0", "invalid argument #3 to 'writei8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 1, 2) return buffer.readi8(b, 1)", "2"},
        {"buffer.writeu8 offset", "local b = buffer.create(16) buffer.writeu8(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 2, 2) return buffer.readu8(b, 2)", "2"},
        {"buffer.writeu8 value", "local b = buffer.create(16) buffer.writeu8(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 1, 2) return buffer.readu8(b, 1)", "2"},
        {"buffer.readi16", "local b = buffer.create(16) return buffer.readi16(b, 1.5)", "invalid argument #2 to 'readi16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 2, -300) return buffer.readi16(b, 2)", "-300"},
        {"buffer.readu16", "local b = buffer.create(16) return buffer.readu16(b, 1.5)", "invalid argument #2 to 'readu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 2, 300) return buffer.readu16(b, 2)", "300"},
        {"buffer.writei16 offset", "local b = buffer.create(16) buffer.writei16(b, 1.5, 2) return 0", "invalid argument #2 to 'writei16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 2, 2) return buffer.readi16(b, 2)", "2"},
        {"buffer.writei16 value", "local b = buffer.create(16) buffer.writei16(b, 2, 1.5) return 0", "invalid argument #3 to 'writei16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 0, 2) return buffer.readi16(b, 0)", "2"},
        {"buffer.writeu16 offset", "local b = buffer.create(16) buffer.writeu16(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 2, 2) return buffer.readu16(b, 2)", "2"},
        {"buffer.writeu16 value", "local b = buffer.create(16) buffer.writeu16(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 0, 2) return buffer.readu16(b, 0)", "2"},
        {"buffer.readi32", "local b = buffer.create(16) return buffer.readi32(b, 1.5)", "invalid argument #2 to 'readi32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 2, -70000) return buffer.readi32(b, 2)", "-70000"},
        {"buffer.readu32", "local b = buffer.create(16) return buffer.readu32(b, 1.5)", "invalid argument #2 to 'readu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 2, 70000) return buffer.readu32(b, 2)", "70000"},
        {"buffer.writei32 offset", "local b = buffer.create(16) buffer.writei32(b, 1.5, 2) return 0", "invalid argument #2 to 'writei32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 2, 2) return buffer.readi32(b, 2)", "2"},
        {"buffer.writei32 value", "local b = buffer.create(16) buffer.writei32(b, 2, 1.5) return 0", "invalid argument #3 to 'writei32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 0, 2) return buffer.readi32(b, 0)", "2"},
        {"buffer.writeu32 offset", "local b = buffer.create(16) buffer.writeu32(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 2, 2) return buffer.readu32(b, 2)", "2"},
        {"buffer.writeu32 value", "local b = buffer.create(16) buffer.writeu32(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 0, 2) return buffer.readu32(b, 0)", "2"},
        {"buffer.readf32", "local b = buffer.create(16) return buffer.readf32(b, 1.5)", "invalid argument #2 to 'readf32' (number has no integer representation)", "local b = buffer.create(16) buffer.writef32(b, 2, 2) return buffer.readf32(b, 2)", "2"},
        {"buffer.writef32 offset", "local b = buffer.create(16) buffer.writef32(b, 1.5, 2) return 0", "invalid argument #2 to 'writef32' (number has no integer representation)", "local b = buffer.create(16) buffer.writef32(b, 2, 3.5) return buffer.readf32(b, 2)", "3.5"},
        {"buffer.readf64", "local b = buffer.create(16) return buffer.readf64(b, 1.5)", "invalid argument #2 to 'readf64' (number has no integer representation)", "local b = buffer.create(16) buffer.writef64(b, 2, 2) return buffer.readf64(b, 2)", "2"},
        {"buffer.writef64 offset", "local b = buffer.create(16) buffer.writef64(b, 1.5, 2) return 0", "invalid argument #2 to 'writef64' (number has no integer representation)", "local b = buffer.create(16) buffer.writef64(b, 2, 3.5) return buffer.readf64(b, 2)", "3.5"},

        // math.ldexp, table.unpack / unpack, select
        {"math.ldexp", "return math.ldexp(3, 1.5)", "invalid argument #2 to 'ldexp' (number has no integer representation)", "return math.ldexp(3, 2)", "12"},
        {"table.unpack", "local t = {10, 20, 30} return select('#', table.unpack(t, 1, 1.5))", "invalid argument #3 to 'unpack' (number has no integer representation)", "local t = {10, 20, 30} return select('#', table.unpack(t, 1, 2))", "2"},
        {"unpack", "local t = {10, 20, 30} return select('#', unpack(t, 1, 1.5))", "invalid argument #3 to 'unpack' (number has no integer representation)", "local t = {10, 20, 30} return select('#', unpack(t, 1, 2))", "2"},
        {"select vararg", "local function f(...) local x = select(1.5, ...) return x end return f(10, 20, 30)", "invalid argument #1 to 'select' (number has no integer representation)", "local function f(...) local x = select(2, ...) return x end return f(10, 20, 30)", "20"},
    };

    for (const FastcallCase& tc : cases)
    {
        std::string err0 = runAtOpt(tc.badCall, 0);
        std::string err1 = runAtOpt(tc.badCall, 1);
        std::string err2 = runAtOpt(tc.badCall, 2);

        CHECK_MESSAGE(err0.find(tc.badMessage) != std::string::npos, std::string(tc.name) << " (-O0): " << err0);
        CHECK_MESSAGE(err1 == err0, std::string(tc.name) << " (-O1 vs -O0): " << err1 << " != " << err0);
        CHECK_MESSAGE(err2 == err0, std::string(tc.name) << " (-O2 vs -O0): " << err2 << " != " << err0);

        std::string ok0 = runAtOpt(tc.okCall, 0);
        std::string ok1 = runAtOpt(tc.okCall, 1);
        std::string ok2 = runAtOpt(tc.okCall, 2);

        CHECK_MESSAGE(ok0 == tc.okExpected, std::string(tc.name) << " ok (-O0): " << ok0);
        CHECK_MESSAGE(ok1 == ok0, std::string(tc.name) << " ok (-O1 vs -O0): " << ok1 << " != " << ok0);
        CHECK_MESSAGE(ok2 == ok0, std::string(tc.name) << " ok (-O2 vs -O0): " << ok2 << " != " << ok0);
    }
}

TEST_CASE("FastcallIntegerArgsScriptInSandbox")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    luaL_sandbox(L);
    luaL_sandboxthread(L);

    mooRunFile(L, "l3/moo-l3-integer-args-fastcalls.luau");

    lua_close(L);
}

TEST_SUITE_END();
