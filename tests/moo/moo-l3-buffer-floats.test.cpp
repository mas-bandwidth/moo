// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// buffer.readf32 / readf64 / writef32 / writef64 by the exact boundary rule (docs/DESIGN.md, "Layer 3 as
// built": the buffer float TEMPORARY bridge is gone; docs/DETERMINISM.md, 18). Reads decode the stored IEEE
// bits to Q48.16 with moo_numfromdouble (the one boundary rule, ties to even) and raise a named error on NaN,
// infinity or out-of-range; writes store moo_numtofloatbits / moo_numtodoublebits.
//
// One test per file: tests/moo/*.test.cpp is built into Moo.Test by the CMake glob.
#include "MooHost.h"
#include "moo.h"

#include "doctest.h"

#include <stdint.h>
#include <string>

TEST_SUITE_BEGIN("moo-buffer-floats");

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
