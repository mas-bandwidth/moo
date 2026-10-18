// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's constant folding (Compiler/src/ConstantFolding.cpp, BuiltinFolding.cpp). It replaces the upstream
// compiler cases that asserted folding moo changed on purpose; each is skipped in tests/Compiler.test.cpp with a
// pointer here:
//
// - number arithmetic folds with the VM's own fixed-point operations, so 2/3 folds to fixDiv's truncation and a
//   folded constant is the run time's bits; division by zero, `^` with a fractional exponent and 0 ^ negative
//   are not folded, and `^` with an integer exponent folds through moo_pow_int;
// - builtin calls (math.*, string.*, bit32.*, vector.*) and math.pi / math.huge do not fold, because the build
//   machine's libm and the run time's must not disagree (until the math library is rebuilt on fixed).
#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"

#include "doctest.h"

#include <string>

static std::string mooCompile0(const char* source)
{
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code | Luau::BytecodeBuilder::Dump_Constants);
    Luau::compileOrThrow(bcb, source);
    return bcb.dumpFunction(0);
}

static bool has(const std::string& dump, const char* text)
{
    return dump.find(text) != std::string::npos;
}

TEST_SUITE_BEGIN("moo");

TEST_CASE("MooFoldArithIsFixedPoint")
{
    // exact results fold as upstream does
    CHECK(has(mooCompile0("return 10 + 2"), "LOADN R0 12"));
    CHECK(has(mooCompile0("return 10 * 2"), "LOADN R0 20"));
    CHECK(has(mooCompile0("return -(2 - 5)"), "LOADN R0 3"));
    CHECK(has(mooCompile0("return 7 // 2"), "LOADN R0 3"));
    CHECK(has(mooCompile0("return -7 % 3"), "LOADN R0 2"));

    // 2/3 is fixDiv's truncation, 43690/65536, not the double nearest 2/3
    std::string twothirds = mooCompile0("return 2 / 3");
    // moo: the dump prints moo_format.h's shortest round trip of 43690 raw, so "0.66666" (docs/layer3/moo-l3-bytecode-version.md)
    CHECK_MESSAGE(has(twothirds, "[0.66666]"), twothirds);

    // 0.1 + 0.2 is 6554 + 13107 raw units
    std::string tenths = mooCompile0("return 0.1 + 0.2");
    // moo: shortest round trip of 19661 raw is "0.3"
    CHECK_MESSAGE(has(tenths, "[0.3]"), tenths);

    // division by zero is the run time's named error, never a folded infinity
    CHECK(!has(mooCompile0("return 1 / 0"), "LOADK"));
    CHECK(has(mooCompile0("return 1 / 0"), "DIV"));

    // moo: ^ with an integer exponent folds through moo_pow_int (docs/layer4/moo-l4-pow-integer-fold.md)
    CHECK(has(mooCompile0("return 10 ^ 2"), "LOADN R0 100"));
}

TEST_CASE("MooBuiltinsDoNotFold")
{
    CHECK(has(mooCompile0("return math.abs(-1)"), "FASTCALL"));
    CHECK(has(mooCompile0("return math.sqrt(4)"), "FASTCALL"));
    CHECK(has(mooCompile0("return string.char(65)"), "FASTCALL"));
    CHECK(has(mooCompile0("return math.pi"), "GETIMPORT"));
    CHECK(has(mooCompile0("return math.huge"), "GETIMPORT"));
    CHECK(has(mooCompile0("return vector.create(1, 2, 3)"), "FASTCALL"));
}

// DESIGN.md "Literals": a literal beyond the range is a compile error, and the constant is the exact rational
// rounded once to 2^-16, ties to even, by moo's own parser
TEST_CASE("MooLiteralsAreExactAndInRange")
{
    CHECK_THROWS_WITH(mooCompile0("return 1e15"), doctest::Contains("Number literal out of range"));
    CHECK_THROWS_WITH(mooCompile0("return 0x800000000000"), doctest::Contains("Number literal out of range"));
    CHECK_THROWS_WITH(mooCompile0("return 1e300"), doctest::Contains("Number literal out of range"));
    CHECK(has(mooCompile0("return 140737488355327"), "140737488355327"));
    // moo: the constant is 6554 raw, which the dump prints by shortest round trip as "0.1"
    CHECK(has(mooCompile0("return 0.1"), "[0.1]"));
    // half a unit, 2^-17, is a tie: to even, which is 0
    CHECK(has(mooCompile0("return 0.00000762939453125"), "LOADN R0 0"));
}

TEST_SUITE_END();
