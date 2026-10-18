// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// One test per file: tests/moo/*.test.cpp is built into Moo.Test by the CMake glob, so a layer 3 card
// adds its cases in its own file and never edits tests/Moo.test.cpp (docs/layer3/README.md).
//
// Checked arithmetic (docs/DESIGN.md, "Overflow"): release saturates; a build with MOO_OVERFLOW_ERRORS
// (moo_number.h; on with assertions) raises "number overflow in '<op>'" at the operation instead, and the
// compiler keeps a saturating constant operation for run time. Every case here runs in both builds and pins
// the build it is in. The saturation tests in moo_number.h are checked against an exact reference in
// MooWide 128-bit arithmetic (tests/MooWide.h), never against themselves.
#include "lua.h"
#include "lualib.h"
#include "moo_number.h"

#include "doctest.h"
#include "MooHost.h"
#include "MooWide.h"

#include <stdint.h>

#include <string>
#include <vector>

TEST_SUITE_BEGIN("moo-overflow-errors-debug");

namespace
{

const char* const kMax = "9223372036854775807";
const char* const kMin = "-9223372036854775807";

// compiles source at the optimization level and runs it in a fresh VM with the moo_test helpers; the result
// as a string, or "error: " and the message
std::string runAt(const std::string& source, int level)
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    Luau::CompileOptions options;
    options.optimizationLevel = level;
    std::string bytecode = Luau::compile(source, options);

    int status = luau_load(L, "=moo", bytecode.data(), bytecode.size(), 0);
    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);

    std::string result = status != 0 ? std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?")
                                     : std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    lua_close(L);
    return result;
}

// the raw result of f(a, b) for raw operands, through registers, so nothing folds
std::string runtime(const char* op, const char* a, const char* b)
{
    return std::string("local function f(a, b) return a ") + op + " b end return moo_test.raw(f(moo_test.fromraw(\"" + a +
           "\"), moo_test.fromraw(\"" + b + "\")))";
}

struct Edge
{
    const char* op;
    std::string over;        // past the edge at run time: release gives saturated, a checked build raises
    std::string folded;      // the same with constant operands (mooFold refuses saturated results today, and
                             // its checked-build guard keeps them for run time once that bound is lifted)
    const char* saturated;   // release's raw result for both
    std::string inside;      // one step inside the range: never raises
    const char* insideRaw;   // its raw result
};

void checkEdge(const Edge& e)
{
    std::string error = std::string("error: moo:1: number overflow in '") + e.op + "'";

    for (int level = 0; level <= 2; ++level)
    {
        CAPTURE(level);
#if MOO_OVERFLOW_ERRORS
        CHECK(runAt(e.over, level) == error);
        CHECK(runAt(e.folded, level) == error);
#else
        CHECK(runAt(e.over, level) == e.saturated);
        CHECK(runAt(e.folded, level) == e.saturated);
#endif
        CHECK(runAt(e.inside, level) == e.insideRaw);
    }
}

// exact reference for the saturation tests: the exact (for *, rounded half up) result in 128 bits, outside the domain
bool outside(MooWide v)
{
    return MooWide::from(INT64_MAX) < v || v < MooWide::from(-INT64_MAX);
}

MooWide wabs(int64_t a)
{
    return a < 0 ? -MooWide::from(a) : MooWide::from(a);
}

std::vector<int64_t> operands()
{
    std::vector<int64_t> v = {0, 1, -1, 2, 3, 32767, 32768, 32769, 65535, 65536, 65537, 131072, INT64_MAX, -INT64_MAX, INT64_MAX - 1,
        -INT64_MAX + 1, INT64_MAX - 65535, INT64_MAX - 65536, int64_t(1) << 62, -(int64_t(1) << 62), (int64_t(1) << 62) - 1,
        int64_t(1) << 47, (int64_t(1) << 47) + 1, (int64_t(1) << 47) - 1, int64_t(1) << 31, int64_t(1) << 32, (int64_t(1) << 32) + 1,
        int64_t(3037000499), int64_t(3037000500), -(int64_t(1) << 47), int64_t(0x5555555555555555), -int64_t(0x5555555555555555)};

    // and a deterministic spread of magnitudes (64-bit LCG, no clock)
    uint64_t x = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < 200; ++i)
    {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        int shift = int(x >> 58); // 0..63
        int64_t m = int64_t((x >> 1) >> shift);
        v.push_back((x & 1) ? -m : m);
    }
    return v;
}

} // namespace

// moo_number.h's saturation tests say exactly when the release operation clamps (docs/DESIGN.md, "Overflow").
TEST_CASE("MooOverflowSaturationTestsAreExact")
{
    std::vector<int64_t> values = operands();
    int64_t half = int64_t(1) << 15;

    for (int64_t a : values)
    {
        for (int64_t b : values)
        {
            CAPTURE(a);
            CAPTURE(b);

            bool add = outside(MooWide::from(a) + MooWide::from(b));
            REQUIRE(moo_numaddsaturates(a, b) == add);
            if (!add)
                REQUIRE(moo_numadd(a, b) == a + b);

            bool sub = outside(MooWide::from(a) - MooWide::from(b));
            REQUIRE(moo_numsubsaturates(a, b) == sub);
            if (!sub)
                REQUIRE(moo_numsub(a, b) == a - b);

            MooWide product = (mooMulWide(a, b) + MooWide::from(half)).shr(16);
            bool mul = outside(product);
            REQUIRE(moo_nummulsaturates(a, b) == mul);
            if (!mul)
                REQUIRE(moo_nummul(a, b) == int64_t(product.lo));

            if (b != 0)
            {
                // trunc(|a| * 2^16 / |b|) > INT64_MAX exactly when |a| * 2^16 >= 2^63 * |b|
                MooWide lhs = wabs(a).shl(16);
                MooWide rhs = mooMulWide(int64_t(wabs(b).lo), int64_t(1) << 62).shl(1);
                bool div = !(lhs < rhs);
                REQUIRE(moo_numdivsaturates(a, b) == div);
                if (div)
                    REQUIRE((moo_numdiv(a, b) == MOO_NUMBER_MAX || moo_numdiv(a, b) == MOO_NUMBER_MIN));

                // a floor quotient in range is a multiple of 2^16, never an edge, so only saturation gives one
                int64_t q = moo_numidiv(a, b);
                REQUIRE(moo_numidivsaturates(a, b) == (q == MOO_NUMBER_MAX || q == MOO_NUMBER_MIN));
            }
            else
            {
                CHECK(!moo_numdivsaturates(a, b)); // division by zero is its own error
                CHECK(!moo_numidivsaturates(a, b));
            }
        }
    }

    // the edges themselves are exact: MAX * 1, MAX / 1 and MAX + 0 give MAX without saturating
    CHECK(!moo_nummulsaturates(MOO_NUMBER_MAX, MOO_NUMBER_ONE));
    CHECK(!moo_numdivsaturates(MOO_NUMBER_MAX, MOO_NUMBER_ONE));
    CHECK(!moo_numaddsaturates(MOO_NUMBER_MAX, 0));
    CHECK(moo_nummulsaturates(MOO_NUMBER_MAX, MOO_NUMBER_ONE + 1));
    CHECK(moo_numdivsaturates(MOO_NUMBER_MAX, MOO_NUMBER_ONE - 1));
}

// '+': MAX + 2^-16 (docs/DESIGN.md, "Overflow")
TEST_CASE("MooOverflowAdd")
{
    checkEdge({"+", runtime("+", kMax, "1"), "return moo_test.raw(140737488355327 + 1)", kMax, runtime("+", "9223372036854775806", "1"), kMax});
}

// '-': MIN - 2^-16
TEST_CASE("MooOverflowSub")
{
    checkEdge({"-", runtime("-", kMin, "1"), "return moo_test.raw(-140737488355327 - 1)", kMin, runtime("-", "-9223372036854775806", "1"), kMin});
}

// '*': MAX * (1 + 2^-16); MAX * 1 is exact
TEST_CASE("MooOverflowMul")
{
    checkEdge({"*", runtime("*", kMax, "65537"), "return moo_test.raw(70368744177664 * 2)", kMax, runtime("*", kMax, "65536"), kMax});
}

// '/': MAX / (1 - 2^-16); MAX / 1 is exact
TEST_CASE("MooOverflowDiv")
{
    checkEdge({"/", runtime("/", kMax, "65535"), "return moo_test.raw(70368744177664 / 0.5)", kMax, runtime("/", kMax, "65536"), kMax});
}

// '//': MAX // (1 - 2^-16) is beyond the integer range; MAX // 1 is 2^47 - 1
TEST_CASE("MooOverflowFloorDiv")
{
    checkEdge({"//", runtime("//", kMax, "65535"), "return moo_test.raw(70368744177664 // 0.5)", kMax, runtime("//", kMax, "65536"),
        "9223372036854710272"});
}

// '^' with an integer exponent is checked through moo_pow_int's status (moo-l4-pow-integer-vm):
// 2 ^ 47 saturates in release and raises in a checked build; 2 ^ 46 is inside
TEST_CASE("MooOverflowPowInteger")
{
    for (int level = 0; level <= 2; ++level)
    {
        CAPTURE(level);
#if MOO_OVERFLOW_ERRORS
        CHECK(runAt(runtime("^", "131072", "3080192"), level) == "error: moo:1: number overflow in '^'");
        CHECK(runAt("return moo_test.raw(2 ^ 47)", level) == "error: moo:1: number overflow in '^'");
#else
        CHECK(runAt(runtime("^", "131072", "3080192"), level) == kMax);
        CHECK(runAt("return moo_test.raw(2 ^ 47)", level) == kMax);
#endif
        CHECK(runAt(runtime("^", "131072", "3014656"), level) == "4611686018427387904");
    }
}

// unary - and % are exact on the symmetric domain: no edge raises in either build
TEST_CASE("MooOverflowUnaryMinusAndModNeverRaise")
{
    for (int level = 0; level <= 2; ++level)
    {
        CAPTURE(level);
        CHECK(runAt(std::string("local function f(a) return -a end return moo_test.raw(f(moo_test.fromraw(\"") + kMax + "\")))", level) == kMin);
        CHECK(runAt(std::string("local function f(a) return -a end return moo_test.raw(f(moo_test.fromraw(\"") + kMin + "\")))", level) == kMax);
        CHECK(runAt("return moo_test.raw(-140737488355327)", level) == "-9223372036854710272");
        CHECK(runAt(runtime("%", "9223372036854775806", kMax), level) == "9223372036854775806");
        CHECK(runAt(runtime("%", "9223372036854775806", kMin), level) == "-1");
        CHECK(runAt(runtime("%", kMin, "1"), level) == "0");
        CHECK(runAt("return moo_test.raw(7 % -2)", level) == "-65536");
    }
}

// the string coercion path (luaV_doarithimpl after luaV_tonumber) raises the same named error
TEST_CASE("MooOverflowCoercedOperand")
{
    std::string source = std::string("local function f(a, b) return a + b end return moo_test.raw(f(\"140737488355327\", moo_test.fromraw(\"") + kMax + "\")))";
#if MOO_OVERFLOW_ERRORS
    CHECK(runAt(source, 1) == "error: moo:1: number overflow in '+'");
#else
    CHECK(runAt(source, 1) == kMax);
#endif
}

TEST_SUITE_END();
