// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's constant folding (Compiler/src/ConstantFolding.cpp, BuiltinFolding.cpp). It replaces the upstream
// compiler cases that asserted folding moo changed on purpose; each is skipped in tests/Compiler.test.cpp with a
// pointer here:
//
// - number arithmetic folds with the VM's own fixed-point operations, so 2/3 folds to fixDiv's truncation and a
//   folded constant is the run time's bits; division by zero and pow are not folded;
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
    CHECK_MESSAGE(has(twothirds, "0.666656494140625"), twothirds);

    // 0.1 + 0.2 is 6554 + 13107 raw units
    std::string tenths = mooCompile0("return 0.1 + 0.2");
    CHECK_MESSAGE(has(tenths, "0.3000030517578125"), tenths);

    // division by zero is the run time's named error, never a folded infinity
    CHECK(!has(mooCompile0("return 1 / 0"), "LOADK"));
    CHECK(has(mooCompile0("return 1 / 0"), "DIV"));

    // pow waits for the math library on fixed
    CHECK(has(mooCompile0("return 10 ^ 2"), "POWK"));
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

TEST_SUITE_END();
