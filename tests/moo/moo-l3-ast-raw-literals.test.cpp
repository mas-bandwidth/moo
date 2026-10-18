// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Number literals reach the compiler as the exact raw Q48.16 value (docs/layer3/moo-l3-ast-raw-literals.md;
// docs/DESIGN.md "Layer 3 as built", "Literals"): the parser stores the raw value from moo_numparse in the AST, so a
// literal that needs more than 53 significant bits compiles to exactly that raw constant. A Luau integer literal is
// refused by name (docs/DESIGN.md "integer").
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include "Luau/Ast.h"
#include "Luau/Common.h"
#include "Luau/Parser.h"

#include "doctest.h"
#include "MooHost.h"
#include "ScopedFlags.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

LUAU_FASTFLAG(LuauIntegerType2)

TEST_SUITE_BEGIN("moo-l3-ast-raw-literals");

using namespace Luau;

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

struct ConstantIntegerCounter : AstVisitor
{
    int count = 0;

    bool visit(AstExprConstantInteger*) override
    {
        ++count;
        return false;
    }
};

} // namespace

// The compiler's number constant is the raw value the literal denotes, at every magnitude. The largest value
// (INT64_MAX raw, 2^47 - 2^-16) and 2^46 - 1 + one unit need more than 53 significant bits, so a double cannot hold
// them; below the fix the largest literal compiled to 0 because the AST's double rounded to 2^47.
TEST_CASE("MooLiteralRawAboveFiftyThreeBits")
{
    MooState s;

    CHECK_EQ(runRaw(s.L, "return 140737488355327.99998474121094"), int64_t(9223372036854775807LL));
    CHECK_EQ(runRaw(s.L, "return -140737488355327.99998474121094"), int64_t(-9223372036854775807LL));
    CHECK_EQ(runRaw(s.L, "return 70368744177663.00001525878906"), int64_t(4611686018427322369LL));
}

// The parser stores the exact raw value from moo_numparse in the AST (docs/DESIGN.md "Literals"), and the double is
// still the nearest double to it for the analyzer and tools (docs/DESIGN.md "Analyzer").
TEST_CASE("MooAstConstantNumberCarriesRawValue")
{
    const char* source = "return 140737488355327.99998474121094, 70368744177663.00001525878906";

    Allocator allocator;
    AstNameTable names(allocator);
    ParseResult result = Parser::parse(source, strlen(source), names, allocator);

    REQUIRE(result.errors.empty());
    AstStatBlock* block = result.root;
    REQUIRE(block != nullptr);
    REQUIRE_EQ(block->body.size, 1);
    AstStatReturn* ret = block->body.data[0]->as<AstStatReturn>();
    REQUIRE(ret != nullptr);
    REQUIRE_EQ(ret->list.size, 2);

    AstExprConstantNumber* largest = ret->list.data[0]->as<AstExprConstantNumber>();
    AstExprConstantNumber* above = ret->list.data[1]->as<AstExprConstantNumber>();
    REQUIRE(largest != nullptr);
    REQUIRE(above != nullptr);

    CHECK_EQ(largest->rawValue, int64_t(9223372036854775807LL));
    CHECK_EQ(largest->value, double(9223372036854775807LL) * (1.0 / 65536.0));

    CHECK_EQ(above->rawValue, int64_t(4611686018427322369LL));
    CHECK_EQ(above->value, double(4611686018427322369LL) * (1.0 / 65536.0));
}

// With LuauIntegerType2 on, moo has no integer type (docs/DESIGN.md "integer"): `1i` and `0x10i` are the parse error
// "moo has no integer type", and no AstExprConstantInteger is built.
TEST_CASE("MooIntegerLiteralRefused")
{
    ScopedFastFlag sff{FFlag::LuauIntegerType2, true};

    for (const char* source : {"return 1i", "return 0x10i"})
    {
        Allocator allocator;
        AstNameTable names(allocator);
        ParseResult result = Parser::parse(source, strlen(source), names, allocator);

        INFO(source);
        REQUIRE(!result.errors.empty());
        CHECK_EQ(result.errors.front().getMessage(), "moo has no integer type");

        ConstantIntegerCounter counter;
        result.root->visit(&counter);
        CHECK_EQ(counter.count, 0);
    }
}

TEST_SUITE_END();
