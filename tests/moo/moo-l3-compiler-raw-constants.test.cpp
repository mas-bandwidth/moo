// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The compiler's number constants are the raw Q48.16 value (docs/layer3/moo-l3-compiler-raw-constants.md;
// docs/DESIGN.md "Layer 3 as built", "The compiler"): a folded constant is the exact raw value, with no double in
// the path and no 2^53 limit, so folding an expression and running it give the same bits at every magnitude.
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"

#include "doctest.h"
#include "MooHost.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

TEST_SUITE_BEGIN("moo-l3-compiler-raw-constants");

namespace
{

struct MooState
{
    lua_State* L;

    explicit MooState()
        : L(luaL_newstate())
    {
        luaL_openlibs(L);
    }

    ~MooState()
    {
        lua_close(L);
    }

    MooState(const MooState&) = delete;
    MooState& operator=(const MooState&) = delete;
};

// the exact decimal literal of a raw value: r / 2^16 has at most 16 fraction digits (10^16 / 2^16 = 5^16), and the
// literal parser reads it exactly (DESIGN.md "Literals"). Parenthesized, so a negative literal is one operand.
std::string rawToLiteral(int64_t r)
{
    uint64_t m = r < 0 ? uint64_t(0) - uint64_t(r) : uint64_t(r);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "(%s%llu.%016llu)", r < 0 ? "-" : "", (unsigned long long)(m >> 16),
        (unsigned long long)((m & 0xffff) * 152587890625ull));
    return buffer;
}

// compiles and runs `source`, returning its first result as the raw Q48.16 value
int64_t runRaw(lua_State* L, const char* source)
{
    size_t size = 0;
    char* bytecode = luau_compile(source, strlen(source), nullptr, &size);
    int status = luau_load(L, "=moo", bytecode, size, 0);
    free(bytecode);

    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);
    REQUIRE(status == 0);

    int64_t result = int64_t(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return result;
}

} // namespace

// The folded result at or above 2^53 raw is the exact raw value. 2^37 + 2^37 is 2^38, whose raw is 2^54.
TEST_CASE("MooFoldRawAtOrAboveTwoPowFiftyThree")
{
    MooState s;

    CHECK_EQ(runRaw(s.L, "return 137438953472 + 137438953472"), int64_t(1) << 54);

    // the bytecode holds the folded constant, not an ADD: the dump prints the raw 2^54 as its shortest round trip
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code | Luau::BytecodeBuilder::Dump_Constants);
    Luau::compileOrThrow(bcb, "return 137438953472 + 137438953472");
    std::string dump = bcb.dumpFunction(0);
    CHECK(dump.find("ADD") == std::string::npos);
    CHECK(dump.find("[274877906944]") != std::string::npos);
}

// A folded result equals the same expression computed at run time bit for bit over 256 seeded operand pairs: once
// compiled with constant operands (folded), once with the operands passed as function arguments (runtime add).
TEST_CASE("MooFoldEqualsRuntimeBitForBit")
{
    MooState s;

    std::mt19937_64 rng(0x4d4f4f524157ULL); // "MOORAW", deterministic
    for (int i = 0; i < 256; ++i)
    {
        int64_t a = int64_t(rng() % (uint64_t(1) << 37)) - (int64_t(1) << 36);
        int64_t b = int64_t(rng() % (uint64_t(1) << 37)) - (int64_t(1) << 36);

        std::string la = rawToLiteral(a);
        std::string lb = rawToLiteral(b);

        std::string folded = "return " + la + " + " + lb;
        std::string runtime = "local f = function(a, b) return a + b end return f(" + la + ", " + lb + ")";

        int64_t foldedRaw = runRaw(s.L, folded.c_str());
        int64_t runtimeRaw = runRaw(s.L, runtime.c_str());
        INFO("pair " << i << " a=" << a << " b=" << b);
        CHECK_EQ(foldedRaw, runtimeRaw);
    }
}

TEST_SUITE_END();
