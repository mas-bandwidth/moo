// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The compiler's constant folding of `^` with an integer exponent (docs/layer4/moo-l4-pow-integer-fold.md;
// docs/DESIGN.md "Layer 3 as built", "The compiler"; "math library", "pow and ^"): ConstantFolding.cpp calls
// moo_pow_int (Common/include/moo_pow.h), so a folded constant is bit-identical to the run time's result. A
// fractional exponent stays unfolded, and 0 ^ negative is never folded because the run time raises
// "division by zero". One test per file: tests/moo/*.test.cpp is built into Moo.Test by the CMake glob.
#include "moo_pow.h"

#include "lua.h"
#include "lualib.h"

#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"

#include "doctest.h"

#include <stdint.h>

#include <string>

TEST_SUITE_BEGIN("moo-l4-pow-integer-fold");

namespace
{

std::string compileDump(const char* source)
{
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code | Luau::BytecodeBuilder::Dump_Constants);
    Luau::compileOrThrow(bcb, source);
    return bcb.dumpFunction(0);
}

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

// Compiles and runs source; returns the result's raw Q48.16 value as a decimal string, or "error: ...".
std::string runRaw(const char* source)
{
    std::string bytecode = Luau::compile(source);

    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    if (luau_load(L, "=moo", bytecode.data(), bytecode.size(), 0) != 0)
    {
        lua_close(L);
        return "error: load";
    }

    std::string result;
    if (lua_pcall(L, 0, 1, 0) != 0)
        result = std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    else
        result = std::to_string(static_cast<long long>(lua_tonumber(L, -1))); // the raw Q48.16 value

    lua_close(L);
    return result;
}

} // namespace

// DESIGN.md "Layer 3 as built", "The compiler": `^` with an integer exponent folds through moo_pow_int, and
// the bytecode holds that raw constant (no POWK), equal to the run time's result.
TEST_CASE("MooFoldPowIntFoldsToPowInt")
{
    const struct
    {
        const char* source;
        fixed_t base;
        int64_t exponent;
    } cases[] = {
        {"return 3 ^ 4", 3 * MOO_NUMBER_ONE, 4},
        {"return 2 ^ -3", 2 * MOO_NUMBER_ONE, -3},
        {"return 1.5 ^ 7", 98304, 7},
        {"return 10 ^ 2", 10 * MOO_NUMBER_ONE, 2},
    };

    for (const auto& c : cases)
    {
        fixed_t expected = 0;
        REQUIRE_MESSAGE(moo_pow_int(c.base, c.exponent, &expected) == MOO_POW_OK, c.source);

        std::string dump = compileDump(c.source);
        CHECK_MESSAGE(!contains(dump, "POWK"), c.source << " was not folded:\n" << dump);
        CHECK_MESSAGE(runRaw(c.source) == std::to_string(static_cast<long long>(expected)),
            c.source << " is not moo_pow_int's raw result " << expected << ":\n" << dump);
    }
}

// DESIGN.md "Division by zero" and "Layer 3 as built", "The compiler": 0 ^ negative stays unfolded, and a
// fractional exponent stays unfolded, because neither is moo_pow_int's integer path.
TEST_CASE("MooFoldPowIntLeavesDivisionByZeroAndFractionalUnfolded")
{
    std::string zero = compileDump("return 0 ^ -1");
    CHECK_MESSAGE(contains(zero, "POWK"), "0 ^ -1 must not fold:\n" << zero);

    std::string fractional = compileDump("return 2 ^ 0.5");
    CHECK_MESSAGE(contains(fractional, "POWK"), "2 ^ 0.5 must not fold:\n" << fractional);
}

TEST_SUITE_END();
