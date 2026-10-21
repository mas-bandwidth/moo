// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// One test per file: tests/moo/*.test.cpp is built into Moo.Test by the CMake glob, so a layer 3 card
// adds its cases in its own file and never edits tests/Moo.test.cpp (docs/layer3/README.md).
// DETERMINISM.md 18: string.pack/unpack f, d and n convert bytes and Q48.16 in integer arithmetic, by
// the one boundary rule (docs/DESIGN.md "Boundary"). f and d pack the nearest float/double bit pattern,
// ties to even; n is the raw Q48.16 int64, an exact round trip.
#include "lua.h"
#include "lualib.h"

#include "doctest.h"
#include "MooHost.h"
#include "moo_number.h"

#include <string.h>
#include <string>
#include <vector>

namespace
{

// string.pack('<d', raw) as the 8 returned bytes.
std::string packDouble(lua_State* L, int64_t raw)
{
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "pack");
    lua_pushstring(L, "<d");
    lua_pushnumber(L, raw);
    lua_call(L, 2, 1);
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    std::string out(s, len);
    lua_pop(L, 2);
    return out;
}

// The little-endian bytes of moo_numtodoublebits(raw), the reference the card names for this check
// until moo-l3-buffer-floats lands (then buffer.writef64's bytes take its place).
std::string doubleBitsLE(int64_t raw)
{
    uint64_t bits = moo_numtodoublebits(raw);
    std::string out;
    for (int k = 0; k < 8; ++k)
        out.push_back(static_cast<char>(bits >> (8 * k)));
    return out;
}

} // namespace

TEST_SUITE_BEGIN("moo-l3-pack-floats");

// DETERMINISM.md 18: string.pack('<d', x) byte-equals the double the boundary rule produces. The card
// says compare to buffer.writef64's bytes once moo-l3-buffer-floats has landed; it has not, so this
// runs the fallback and compares against moo_numtodoublebits directly.
TEST_CASE("MooL3PackFloatsDoubleBytes")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    uint64_t seed = 12345;
    for (int i = 0; i < 1024; ++i)
    {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        int64_t raw = static_cast<int64_t>(seed >> 16);
        if (raw < 0)
            raw = -raw;
        REQUIRE(packDouble(L, raw) == doubleBitsLE(raw));
    }

    lua_close(L);
}

// f packs the nearest float bit pattern, ties to even: raw 16777217 is halfway between float 256.0
// and its successor, so it rounds to 256.0 (even mantissa), little-endian 00 00 80 43.
TEST_CASE("MooL3PackFloatsFloatTie")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    lua_getglobal(L, "string");
    lua_getfield(L, -1, "pack");
    lua_pushstring(L, "<f");
    lua_pushnumber(L, static_cast<int64_t>(16777217));
    lua_call(L, 2, 1);
    size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    REQUIRE(len == 4);
    CHECK((unsigned char)s[0] == 0x00);
    CHECK((unsigned char)s[1] == 0x00);
    CHECK((unsigned char)s[2] == 0x80);
    CHECK((unsigned char)s[3] == 0x43);
    lua_close(L);
}

// n is the raw Q48.16 int64: pack/unpack is an exact round trip of the raw value.
TEST_CASE("MooL3PackFloatsNativeRoundTrip")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "local s = string.pack('<n', 123456789); return string.unpack('<n', s)") == "123456789");
    CHECK(mooRun(L, "local s = string.pack('>n', -987654321); return string.unpack('>n', s)") == "-987654321");

    lua_close(L);
}

TEST_SUITE_END();
