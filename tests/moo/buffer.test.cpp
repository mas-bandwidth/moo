// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Float bytes: buffer.readf32/readf64/writef32/writef64 and string.pack/unpack f, d and n convert
// between IEEE bits and Q48.16 by the boundary rule (docs/DESIGN.md "Boundary"; DETERMINISM.md 18).

#include "lua.h"
#include "lualib.h"
#include "doctest.h"
#include "MooHost.h"
#include "moo_number.h"
#include "moo.h"

#include <string.h>
#include <string>
#include <vector>
#include <stdint.h>

TEST_SUITE_BEGIN("buffer");

// DETERMINISM.md 18: string.pack/unpack f, d and n convert bytes and Q48.16 in integer arithmetic, by
// the one boundary rule (docs/DESIGN.md "Boundary"). f and d pack the nearest float/double bit pattern,
// ties to even; n is the raw Q48.16 int64, an exact round trip.
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

// The little-endian bytes of moo_numtodoublebits(raw), the reference for this check (buffer.writef64's
// bytes are checked against the same function in the buffer cases below).
std::string doubleBitsLE(int64_t raw)
{
    uint64_t bits = moo_numtodoublebits(raw);
    std::string out;
    for (int k = 0; k < 8; ++k)
        out.push_back(static_cast<char>(bits >> (8 * k)));
    return out;
}

} // namespace


// DETERMINISM.md 18: string.pack('<d', x) byte-equals the double the boundary rule produces.
// The reference is moo_numtodoublebits, the function buffer.writef64 also uses.
TEST_CASE("PackFloatsDoubleBytes")
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
TEST_CASE("PackFloatsFloatTie")
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
TEST_CASE("PackFloatsNativeRoundTrip")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "local s = string.pack('<n', 123456789); return string.unpack('<n', s)") == "123456789");
    CHECK(mooRun(L, "local s = string.pack('>n', -987654321); return string.unpack('>n', s)") == "-987654321");

    lua_close(L);
}

// buffer.readf32 / readf64 / writef32 / writef64 by the exact boundary rule (docs/DESIGN.md, "Layer 3 as
// built": the buffer float TEMPORARY bridge is gone; docs/DETERMINISM.md, 18). Reads decode the stored IEEE
// bits to Q48.16 with moo_numfromdouble (the one boundary rule, ties to even) and raise a named error on NaN,
// infinity or out-of-range; writes store moo_numtofloatbits / moo_numtodoublebits.
//

namespace
{

uint64_t rngState = 0x123456789abcdef0ull;
uint64_t rng()
{
    rngState = rngState * 6364136223846793005ull + 1442695040888963407ull;
    return rngState;
}

// Runs source with the given compiler optimization level and returns the first result as a raw decimal string,
// or "error: <message>" if it fails. Level 0 disables the fastcalls, so level 0 is the library path and
// level 2 is the fastcall path.
std::string runRaw(lua_State* L, const std::string& source, int level)
{
    Luau::CompileOptions options;
    options.optimizationLevel = level;
    std::string bytecode = Luau::compile(source, options);
    if (luau_load(L, "=cmp", bytecode.data(), bytecode.size(), 0) != 0)
        return "error: load";

    if (lua_pcall(L, 0, 1, 0) != 0)
    {
        std::string result = "error: ";
        result += lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_pop(L, 1);
        return result;
    }

    int64_t n = lua_tointeger(L, -1);
    std::string result = std::to_string(static_cast<long long>(n));
    lua_pop(L, 1);
    lua_pop(L, 1);
    return result;
}

} // namespace

// DESIGN.md "Layer 3 as built": the fastcall (level 2) and library (level 0) buffer float paths must agree
// bit for bit on seeded raw values, because they share one integer-arithmetic boundary rule.
TEST_CASE("BufferFloatFastcallAgreesWithLibrary")
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    for (uint64_t i = 0; i < 4096; ++i)
    {
        int64_t raw = int64_t(rng() % uint64_t(INT64_MAX)) * (rng() & 1 ? 1 : -1);
        std::string rawText = std::to_string(static_cast<long long>(raw));

        for (const char* fmt : {"f32", "f64"})
        {
            std::string src = "local b = buffer.create(8) buffer.write" + std::string(fmt) +
                              "(b, 0, " + rawText + ") return buffer.read" + std::string(fmt) + "(b, 0)";
            std::string lib = runRaw(L, src, 0);
            std::string fast = runRaw(L, src, 2);
            if (lib != fast)
            {
                FAIL("fmt=" << fmt << " raw=" << rawText << " lib=" << lib << " fast=" << fast);
            }
        }
    }

    lua_close(L);
}

TEST_SUITE_END();
