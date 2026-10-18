// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// DESIGN.md "Exact integers": a function that needs an integer accepts only an integer-valued number
// (the 16 fraction bits are zero) and raises the named error "number has no integer representation",
// as Lua 5.3 does, instead of Luau's silent truncation. Each call below goes through a local alias so
// the bytecode cannot take a fastcall; the fastcall path is its own card (moo-l3-integer-args-fastcalls).
#include "lua.h"
#include "lualib.h"
#include "moo.h"

#include "doctest.h"
#include "MooHost.h"

#include <string>

TEST_SUITE_BEGIN("moo-l3-integer-args");

namespace
{

std::string run(const std::string& source)
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    std::string result = mooRun(L, source.c_str());
    lua_close(L);
    return result;
}

struct Refusal
{
    const char* what;
    const char* call;
    const char* message;
};

} // namespace

// One refusal per function DESIGN.md "Exact integers" names; each message is the whole error a script sees.
TEST_CASE("IntegerArgsRefuseFraction")
{
    const Refusal refusals[] = {
        {"string.byte", "byte('abc', 1.5)", "invalid argument #2 to 'byte' (number has no integer representation)"},
        {"string.char", "char(1.5)", "invalid argument #1 to 'char' (number has no integer representation)"},
        {"string.sub", "sub('abc', 1.5)", "invalid argument #2 to 'sub' (number has no integer representation)"},
        {"string.rep", "rep('a', 1.5)", "invalid argument #2 to 'rep' (number has no integer representation)"},
        {"table.insert", "insert(t, 1.5, 'x')", "invalid argument #2 to 'insert' (number has no integer representation)"},
        {"table.remove", "remove(t, 1.5)", "invalid argument #2 to 'remove' (number has no integer representation)"},
        {"table.move", "move(t, 1.5, 2, 1, t)", "invalid argument #2 to 'move' (number has no integer representation)"},
        {"table.create", "create(1.5)", "invalid argument #1 to 'create' (number has no integer representation)"},
        {"bit32.band", "band(1.5)", "invalid argument #1 to 'band' (number has no integer representation)"},
        {"bit32.lshift", "lshift(1, 1.5)", "invalid argument #2 to 'lshift' (number has no integer representation)"},
        {"buffer.readu8", "readu8(b, 1.5)", "invalid argument #2 to 'readu8' (number has no integer representation)"},
        {"buffer.readbits", "readbits(b, 1.5, 1)", "invalid argument #2 to 'readbits' (number has no integer representation)"},
        {"string.format %d", "format('%d', 1.5)", "invalid argument #2 to 'format' (number has no integer representation)"},
        {"string.format %i", "format('%i', 1.5)", "invalid argument #2 to 'format' (number has no integer representation)"},
        {"string.format %x", "format('%x', 1.5)", "invalid argument #2 to 'format' (number has no integer representation)"},
        {"string.format %X", "format('%X', 1.5)", "invalid argument #2 to 'format' (number has no integer representation)"},
        {"string.format %c", "format('%c', 1.5)", "invalid argument #2 to 'format' (number has no integer representation)"},
        {"string.pack signed", "pack('i4', 1.5)", "invalid argument #2 to 'pack' (number has no integer representation)"},
        {"string.pack unsigned", "pack('I4', 1.5)", "invalid argument #2 to 'pack' (number has no integer representation)"},
    };

    for (const Refusal& refusal : refusals)
    {
        // The local aliases keep the call from being inlined as a fastcall.
        std::string source = "local byte, char, sub, rep = string.byte, string.char, string.sub, string.rep "
                             "local insert, remove, move, create = table.insert, table.remove, table.move, table.create "
                             "local band, lshift = bit32.band, bit32.lshift "
                             "local readu8, readbits = buffer.readu8, buffer.readbits "
                             "local format, pack = string.format, string.pack "
                             "local t = {10, 20, 30} local b = buffer.create(8) "
                             "local ok, err = pcall(function() return " +
                             std::string(refusal.call) + " end) return tostring(ok) .. '|' .. tostring(err)";

        std::string result = run(source);
        CHECK_MESSAGE(result.rfind("false|", 0) == 0, refusal.what << ": " << result);
        std::string message = result.substr(result.find('|') + 1);
        size_t at = message.find("invalid argument");
        if (at != std::string::npos)
            message = message.substr(at);
        CHECK_MESSAGE(message == refusal.message, refusal.what << ": " << message);
    }
}

// The same functions still accept an integer-valued number, and a number equal to an integer (2.0).
TEST_CASE("IntegerArgsAcceptInteger")
{
    const char* source = "local byte, char, sub, rep = string.byte, string.char, string.sub, string.rep "
                         "local insert, remove, move, create = table.insert, table.remove, table.move, table.create "
                         "local band, lshift = bit32.band, bit32.lshift "
                         "local readu8, writeu8, readbits, writebits = buffer.readu8, buffer.writeu8, buffer.readbits, buffer.writebits "
                         "local format, pack = string.format, string.pack "
                         "local b = buffer.create(8) writeu8(b, 2, 7) "
                         "assert(byte('abc', 2.0) == 98) "
                         "assert(char(2.0) == '\\2') "
                         "assert(sub('abc', 2.0) == 'bc') "
                         "assert(rep('a', 2.0) == 'aa') "
                         "local t = {10, 20, 30} insert(t, 2.0, 15) assert(t[2] == 15 and #t == 4) "
                         "assert(remove(t, 2.0) == 15 and #t == 3) "
                         "assert(move(t, 2.0, 3, 1, t) == t) "
                         "local c = create(2.0, 5) assert(#c == 2 and c[1] == 5) "
                         "assert(band(2.0) == 2) "
                         "assert(lshift(1, 2.0) == 4) "
                         "assert(readu8(b, 2.0) == 7) "
                         "writebits(b, 16.0, 3, 5) assert(readbits(b, 16.0, 3) == 5) "
                         "assert(format('%d', 2.0) == '2') "
                         "assert(format('%i', 2.0) == '2') "
                         "assert(format('%x', 2.0) == '2') "
                         "assert(format('%X', 2.0) == '2') "
                         "assert(format('%c', 2.0) == '\\2') "
                         "assert(pack('i4', 2.0) == pack('i4', 2)) "
                         "assert(pack('I4', 2.0) == pack('I4', 2)) "
                         "return 'OK'";

    CHECK(run(source) == "OK");
}

// C API: lua_tointegerx / lua_tounsignedx report a fraction as "not a number", as Lua 5.3 does.
TEST_CASE("IntegerConversionReportsFraction")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    int isnum = -1;
    lua_pushnumber(L, mooHostNumber(1.5));
    lua_tointegerx(L, -1, &isnum);
    CHECK(isnum == 0);
    isnum = -1;
    lua_tounsignedx(L, -1, &isnum);
    CHECK(isnum == 0);

    lua_pushnumber(L, mooHostNumber(2.0));
    CHECK(lua_tointegerx(L, -1, &isnum) == 2);
    CHECK(isnum == 1);
    CHECK(lua_tounsignedx(L, -1, &isnum) == 2);
    CHECK(isnum == 1);

    lua_close(L);
}

TEST_SUITE_END();
