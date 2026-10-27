// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Numbers: Q48.16 arithmetic, the named overflow errors of a checked build, and the exact-integer
// rule for integer arguments, on the library and the fastcall paths (docs/DESIGN.md "Representation",
// "Overflow", "Exact integers").

#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "doctest.h"
#include "MooHost.h"
#include "MooWide.h"
#include "moo.h"
#include "Luau/Compiler.h"

#include <stdint.h>
#include <string>
#include <vector>

TEST_SUITE_BEGIN("number");

// Checked arithmetic (docs/DESIGN.md, "Overflow"): release saturates; a build with MOO_OVERFLOW_ERRORS
// (moo_number.h; on with assertions) raises "number overflow in '<op>'" at the operation instead, and the
// compiler keeps a saturating constant operation for run time. Every case here runs in both builds and pins
// the build it is in. The saturation tests in moo_number.h are checked against an exact reference in
// MooWide 128-bit arithmetic (tests/MooWide.h), never against themselves.

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
TEST_CASE("OverflowSaturationTestsAreExact")
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
TEST_CASE("OverflowAdd")
{
    checkEdge({"+", runtime("+", kMax, "1"), "return moo_test.raw(140737488355327 + 1)", kMax, runtime("+", "9223372036854775806", "1"), kMax});
}

// '-': MIN - 2^-16
TEST_CASE("OverflowSub")
{
    checkEdge({"-", runtime("-", kMin, "1"), "return moo_test.raw(-140737488355327 - 1)", kMin, runtime("-", "-9223372036854775806", "1"), kMin});
}

// '*': MAX * (1 + 2^-16); MAX * 1 is exact
TEST_CASE("OverflowMul")
{
    checkEdge({"*", runtime("*", kMax, "65537"), "return moo_test.raw(70368744177664 * 2)", kMax, runtime("*", kMax, "65536"), kMax});
}

// '/': MAX / (1 - 2^-16); MAX / 1 is exact
TEST_CASE("OverflowDiv")
{
    checkEdge({"/", runtime("/", kMax, "65535"), "return moo_test.raw(70368744177664 / 0.5)", kMax, runtime("/", kMax, "65536"), kMax});
}

// '//': MAX // (1 - 2^-16) is beyond the integer range; MAX // 1 is 2^47 - 1
TEST_CASE("OverflowFloorDiv")
{
    checkEdge({"//", runtime("//", kMax, "65535"), "return moo_test.raw(70368744177664 // 0.5)", kMax, runtime("//", kMax, "65536"),
        "9223372036854710272"});
}

// '^': 2 ^ 47 saturates; 2 ^ 46 is exact (docs/DESIGN.md "pow and ^")
TEST_CASE("OverflowPow")
{
    checkEdge({"^", runtime("^", "131072", "3080192"), "return moo_test.raw(2 ^ 47)", kMax, runtime("^", "131072", "3014656"),
        "4611686018427387904"});
}

// unary - and % are exact on the symmetric domain: no edge raises in either build
TEST_CASE("OverflowUnaryMinusAndModNeverRaise")
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
TEST_CASE("OverflowCoercedOperand")
{
    std::string source = std::string("local function f(a, b) return a + b end return moo_test.raw(f(\"140737488355327\", moo_test.fromraw(\"") + kMax + "\")))";
#if MOO_OVERFLOW_ERRORS
    CHECK(runAt(source, 1) == "error: moo:1: number overflow in '+'");
#else
    CHECK(runAt(source, 1) == kMax);
#endif
}


// DESIGN.md "Layer 3 as built": number is Q48.16, so 1 + 1 is the raw value for 2.
TEST_CASE("NumberAddition")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "return 1 + 1") == "2");

    lua_close(L);
}

// DESIGN.md "Exact integers": a function that needs an integer accepts only an integer-valued number
// (the 16 fraction bits are zero) and raises the named error "number has no integer representation",
// as Lua 5.3 does, instead of Luau's silent truncation. Each call below goes through a local alias so
// the bytecode cannot take a fastcall; the fastcall path is IntegerArgsFastcallsMatchAcrossOptimizationLevels.

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

// moo: DESIGN.md "Exact integers": a fastcall in VM/src/lbuiltins.cpp that converts a number argument to an
// integer checks luai_numisint first and returns -1 on a fraction so the library path raises "number has no
// integer representation", matching -O0 (no fastcall) and -O1/-O2 (fastcall).

namespace
{

std::string runAtOpt(const std::string& source, int optLevel)
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    luaL_sandbox(L);
    luaL_sandboxthread(L);

    Luau::CompileOptions options;
    options.optimizationLevel = optLevel;
    std::string bytecode = Luau::compile(source, options);

    int status = luau_load(L, "=moo", bytecode.data(), bytecode.size(), 0);
    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);

    std::string result;
    if (status != 0)
        result = std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    else
        result = luaL_tolstring(L, -1, nullptr), lua_pop(L, 1);

    lua_pop(L, 1);
    lua_close(L);
    return result;
}

struct FastcallCase
{
    const char* name;
    const char* badCall;
    const char* badMessage;
    const char* okCall;
    const char* okExpected;
};

} // namespace

// DESIGN.md "Exact integers": every fastcall-able builtin that takes an integer refuses 1.5 with the same
// error text and accepts 2 with the same result at -O0, -O1 and -O2.
TEST_CASE("IntegerArgsFastcallsMatchAcrossOptimizationLevels")
{
    const FastcallCase cases[] = {
        // bit32.*
        {"bit32.arshift arg 1", "return bit32.arshift(1.5, 2)", "invalid argument #1 to 'arshift' (number has no integer representation)", "return bit32.arshift(8, 2)", "2"},
        {"bit32.arshift arg 2", "return bit32.arshift(8, 1.5)", "invalid argument #2 to 'arshift' (number has no integer representation)", "return bit32.arshift(2, 1)", "1"},
        {"bit32.band arg 1", "return bit32.band(1.5, 2)", "invalid argument #1 to 'band' (number has no integer representation)", "return bit32.band(3, 2)", "2"},
        {"bit32.band arg 2", "return bit32.band(3, 1.5)", "invalid argument #2 to 'band' (number has no integer representation)", "return bit32.band(2, 3)", "2"},
        {"bit32.band arg 3", "return bit32.band(7, 3, 1.5)", "invalid argument #3 to 'band' (number has no integer representation)", "return bit32.band(7, 3, 2)", "2"},
        {"bit32.bnot", "return bit32.bnot(1.5)", "invalid argument #1 to 'bnot' (number has no integer representation)", "return bit32.bnot(2)", "4294967293"},
        {"bit32.bor arg 1", "return bit32.bor(1.5, 2)", "invalid argument #1 to 'bor' (number has no integer representation)", "return bit32.bor(1, 2)", "3"},
        {"bit32.bor arg 2", "return bit32.bor(1, 1.5)", "invalid argument #2 to 'bor' (number has no integer representation)", "return bit32.bor(2, 4)", "6"},
        {"bit32.bor arg 3", "return bit32.bor(1, 2, 1.5)", "invalid argument #3 to 'bor' (number has no integer representation)", "return bit32.bor(1, 4, 2)", "7"},
        {"bit32.bxor arg 1", "return bit32.bxor(1.5, 2)", "invalid argument #1 to 'bxor' (number has no integer representation)", "return bit32.bxor(3, 2)", "1"},
        {"bit32.bxor arg 2", "return bit32.bxor(3, 1.5)", "invalid argument #2 to 'bxor' (number has no integer representation)", "return bit32.bxor(2, 1)", "3"},
        {"bit32.bxor arg 3", "return bit32.bxor(7, 1, 1.5)", "invalid argument #3 to 'bxor' (number has no integer representation)", "return bit32.bxor(7, 1, 2)", "4"},
        {"bit32.btest arg 1", "return bit32.btest(1.5, 2)", "invalid argument #1 to 'btest' (number has no integer representation)", "return bit32.btest(3, 2)", "true"},
        {"bit32.btest arg 2", "return bit32.btest(3, 1.5)", "invalid argument #2 to 'btest' (number has no integer representation)", "return bit32.btest(2, 1)", "false"},
        {"bit32.btest arg 3", "return bit32.btest(7, 3, 1.5)", "invalid argument #3 to 'btest' (number has no integer representation)", "return bit32.btest(7, 3, 2)", "true"},
        {"bit32.extract 2-arg arg 1", "local f = 1 return bit32.extract(1.5, f)", "invalid argument #1 to 'extract' (number has no integer representation)", "local f = 1 return bit32.extract(2, f)", "1"},
        {"bit32.extract 2-arg arg 2", "return bit32.extract(6, 1.5)", "invalid argument #2 to 'extract' (number has no integer representation)", "return bit32.extract(6, 2)", "1"},
        {"bit32.extract 3-arg nonconst arg 3", "local f, w = 1, 1.5 f, w = f, w return bit32.extract(6, f, w)", "invalid argument #3 to 'extract' (number has no integer representation)", "local f, w = 1, 2 f, w = f, w return bit32.extract(6, f, w)", "3"},
        {"bit32.extract FASTCALL2K arg 1", "return bit32.extract(1.5, 1, 2)", "invalid argument #1 to 'extract' (number has no integer representation)", "return bit32.extract(6, 1, 2)", "3"},
        {"bit32.extract 3-arg nonconst arg 2", "local f, w = 1.5, 2 f, w = f, w return bit32.extract(6, f, w)", "invalid argument #2 to 'extract' (number has no integer representation)", "local f, w = 2, 2 f, w = f, w return bit32.extract(12, f, w)", "3"},
        {"bit32.lrotate arg 1", "return bit32.lrotate(1.5, 2)", "invalid argument #1 to 'lrotate' (number has no integer representation)", "return bit32.lrotate(1, 2)", "4"},
        {"bit32.lrotate arg 2", "return bit32.lrotate(1, 1.5)", "invalid argument #2 to 'lrotate' (number has no integer representation)", "return bit32.lrotate(2, 1)", "4"},
        {"bit32.lshift arg 1", "return bit32.lshift(1.5, 2)", "invalid argument #1 to 'lshift' (number has no integer representation)", "return bit32.lshift(1, 2)", "4"},
        {"bit32.lshift arg 2", "return bit32.lshift(1, 1.5)", "invalid argument #2 to 'lshift' (number has no integer representation)", "return bit32.lshift(2, 1)", "4"},
        {"bit32.replace 3-arg arg 1", "local v = 1 return bit32.replace(1.5, v, 2)", "invalid argument #1 to 'replace' (number has no integer representation)", "local v = 1 return bit32.replace(0, v, 2)", "4"},
        {"bit32.replace 3-arg arg 2", "local n = 0 return bit32.replace(n, 1.5, 2)", "invalid argument #2 to 'replace' (number has no integer representation)", "local n = 0 return bit32.replace(n, 2, 1)", "0"},
        {"bit32.replace 3-arg arg 3", "local n = 0 return bit32.replace(n, 1, 1.5)", "invalid argument #3 to 'replace' (number has no integer representation)", "local n = 0 return bit32.replace(n, 1, 2)", "4"},
        {"bit32.replace 4-arg arg 4", "return bit32.replace(0, 3, 1, 1.5)", "invalid argument #4 to 'replace' (number has no integer representation)", "return bit32.replace(0, 3, 1, 2)", "6"},
        {"bit32.rrotate arg 1", "return bit32.rrotate(1.5, 2)", "invalid argument #1 to 'rrotate' (number has no integer representation)", "return bit32.rrotate(8, 2)", "2"},
        {"bit32.rrotate arg 2", "return bit32.rrotate(8, 1.5)", "invalid argument #2 to 'rrotate' (number has no integer representation)", "return bit32.rrotate(4, 2)", "1"},
        {"bit32.rshift arg 1", "return bit32.rshift(1.5, 2)", "invalid argument #1 to 'rshift' (number has no integer representation)", "return bit32.rshift(8, 2)", "2"},
        {"bit32.rshift arg 2", "return bit32.rshift(8, 1.5)", "invalid argument #2 to 'rshift' (number has no integer representation)", "return bit32.rshift(2, 1)", "1"},
        {"bit32.countlz", "return bit32.countlz(1.5)", "invalid argument #1 to 'countlz' (number has no integer representation)", "return bit32.countlz(2)", "30"},
        {"bit32.countrz", "return bit32.countrz(1.5)", "invalid argument #1 to 'countrz' (number has no integer representation)", "return bit32.countrz(2)", "1"},
        {"bit32.byteswap", "return bit32.byteswap(1.5)", "invalid argument #1 to 'byteswap' (number has no integer representation)", "return bit32.byteswap(2)", "33554432"},

        // string.byte, string.char, string.sub
        {"string.byte 2-arg", "local s = 'abc' return string.byte(s, 1.5)", "invalid argument #2 to 'byte' (number has no integer representation)", "local s = 'abc' return string.byte(s, 2)", "98"},
        {"string.byte 3-arg", "local s = 'abc' return string.byte(s, 1, 1.5)", "invalid argument #3 to 'byte' (number has no integer representation)", "local s = 'abc' return string.byte(s, 2, 2)", "98"},
        {"string.char arg 1", "return string.char(1.5)", "invalid argument #1 to 'char' (number has no integer representation)", "return string.char(2)", "\x02"},
        {"string.char arg 2", "return string.char(65, 1.5)", "invalid argument #2 to 'char' (number has no integer representation)", "return string.char(65, 2)", "A\x02"},
        {"string.sub arg 2", "local s = 'abcd' return string.sub(s, 1.5, 3)", "invalid argument #2 to 'sub' (number has no integer representation)", "local s = 'abcd' return string.sub(s, 2, 3)", "bc"},
        {"string.sub arg 3", "local s = 'abcd' return string.sub(s, 1, 1.5)", "invalid argument #3 to 'sub' (number has no integer representation)", "local s = 'abcd' return string.sub(s, 1, 2)", "ab"},

        // buffer reads and writes
        {"buffer.readi8", "local b = buffer.create(16) return buffer.readi8(b, 1.5)", "invalid argument #2 to 'readi8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 2, -5) return buffer.readi8(b, 2)", "-5"},
        {"buffer.readu8", "local b = buffer.create(16) return buffer.readu8(b, 1.5)", "invalid argument #2 to 'readu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 2, 7) return buffer.readu8(b, 2)", "7"},
        {"buffer.writei8 offset", "local b = buffer.create(16) buffer.writei8(b, 1.5, 2) return 0", "invalid argument #2 to 'writei8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 2, 2) return buffer.readi8(b, 2)", "2"},
        {"buffer.writei8 value", "local b = buffer.create(16) buffer.writei8(b, 2, 1.5) return 0", "invalid argument #3 to 'writei8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 1, 2) return buffer.readi8(b, 1)", "2"},
        {"buffer.writeu8 offset", "local b = buffer.create(16) buffer.writeu8(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 2, 2) return buffer.readu8(b, 2)", "2"},
        {"buffer.writeu8 value", "local b = buffer.create(16) buffer.writeu8(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 1, 2) return buffer.readu8(b, 1)", "2"},
        {"buffer.readi16", "local b = buffer.create(16) return buffer.readi16(b, 1.5)", "invalid argument #2 to 'readi16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 2, -300) return buffer.readi16(b, 2)", "-300"},
        {"buffer.readu16", "local b = buffer.create(16) return buffer.readu16(b, 1.5)", "invalid argument #2 to 'readu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 2, 300) return buffer.readu16(b, 2)", "300"},
        {"buffer.writei16 offset", "local b = buffer.create(16) buffer.writei16(b, 1.5, 2) return 0", "invalid argument #2 to 'writei16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 2, 2) return buffer.readi16(b, 2)", "2"},
        {"buffer.writei16 value", "local b = buffer.create(16) buffer.writei16(b, 2, 1.5) return 0", "invalid argument #3 to 'writei16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 0, 2) return buffer.readi16(b, 0)", "2"},
        {"buffer.writeu16 offset", "local b = buffer.create(16) buffer.writeu16(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 2, 2) return buffer.readu16(b, 2)", "2"},
        {"buffer.writeu16 value", "local b = buffer.create(16) buffer.writeu16(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 0, 2) return buffer.readu16(b, 0)", "2"},
        {"buffer.readi32", "local b = buffer.create(16) return buffer.readi32(b, 1.5)", "invalid argument #2 to 'readi32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 2, -70000) return buffer.readi32(b, 2)", "-70000"},
        {"buffer.readu32", "local b = buffer.create(16) return buffer.readu32(b, 1.5)", "invalid argument #2 to 'readu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 2, 70000) return buffer.readu32(b, 2)", "70000"},
        {"buffer.writei32 offset", "local b = buffer.create(16) buffer.writei32(b, 1.5, 2) return 0", "invalid argument #2 to 'writei32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 2, 2) return buffer.readi32(b, 2)", "2"},
        {"buffer.writei32 value", "local b = buffer.create(16) buffer.writei32(b, 2, 1.5) return 0", "invalid argument #3 to 'writei32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 0, 2) return buffer.readi32(b, 0)", "2"},
        {"buffer.writeu32 offset", "local b = buffer.create(16) buffer.writeu32(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 2, 2) return buffer.readu32(b, 2)", "2"},
        {"buffer.writeu32 value", "local b = buffer.create(16) buffer.writeu32(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 0, 2) return buffer.readu32(b, 0)", "2"},
        {"buffer.readf32", "local b = buffer.create(16) return buffer.readf32(b, 1.5)", "invalid argument #2 to 'readf32' (number has no integer representation)", "local b = buffer.create(16) buffer.writef32(b, 2, 2) return buffer.readf32(b, 2)", "2"},
        {"buffer.writef32 offset", "local b = buffer.create(16) buffer.writef32(b, 1.5, 2) return 0", "invalid argument #2 to 'writef32' (number has no integer representation)", "local b = buffer.create(16) buffer.writef32(b, 2, 3.5) return buffer.readf32(b, 2)", "3.5"},
        {"buffer.readf64", "local b = buffer.create(16) return buffer.readf64(b, 1.5)", "invalid argument #2 to 'readf64' (number has no integer representation)", "local b = buffer.create(16) buffer.writef64(b, 2, 2) return buffer.readf64(b, 2)", "2"},
        {"buffer.writef64 offset", "local b = buffer.create(16) buffer.writef64(b, 1.5, 2) return 0", "invalid argument #2 to 'writef64' (number has no integer representation)", "local b = buffer.create(16) buffer.writef64(b, 2, 3.5) return buffer.readf64(b, 2)", "3.5"},

        // math.ldexp, table.unpack / unpack, select
        {"math.ldexp", "return math.ldexp(3, 1.5)", "invalid argument #2 to 'ldexp' (number has no integer representation)", "return math.ldexp(3, 2)", "12"},
        {"table.unpack", "local t = {10, 20, 30} return select('#', table.unpack(t, 1, 1.5))", "invalid argument #3 to 'unpack' (number has no integer representation)", "local t = {10, 20, 30} return select('#', table.unpack(t, 1, 2))", "2"},
        {"unpack", "local t = {10, 20, 30} return select('#', unpack(t, 1, 1.5))", "invalid argument #3 to 'unpack' (number has no integer representation)", "local t = {10, 20, 30} return select('#', unpack(t, 1, 2))", "2"},
        {"select vararg", "local function f(...) local x = select(1.5, ...) return x end return f(10, 20, 30)", "invalid argument #1 to 'select' (number has no integer representation)", "local function f(...) local x = select(2, ...) return x end return f(10, 20, 30)", "20"},
    };

    for (const FastcallCase& tc : cases)
    {
        std::string err0 = runAtOpt(tc.badCall, 0);
        std::string err1 = runAtOpt(tc.badCall, 1);
        std::string err2 = runAtOpt(tc.badCall, 2);

        CHECK_MESSAGE(err0.find(tc.badMessage) != std::string::npos, std::string(tc.name) << " (-O0): " << err0);
        CHECK_MESSAGE(err1 == err0, std::string(tc.name) << " (-O1 vs -O0): " << err1 << " != " << err0);
        CHECK_MESSAGE(err2 == err0, std::string(tc.name) << " (-O2 vs -O0): " << err2 << " != " << err0);

        std::string ok0 = runAtOpt(tc.okCall, 0);
        std::string ok1 = runAtOpt(tc.okCall, 1);
        std::string ok2 = runAtOpt(tc.okCall, 2);

        CHECK_MESSAGE(ok0 == tc.okExpected, std::string(tc.name) << " ok (-O0): " << ok0);
        CHECK_MESSAGE(ok1 == ok0, std::string(tc.name) << " ok (-O1 vs -O0): " << ok1 << " != " << ok0);
        CHECK_MESSAGE(ok2 == ok0, std::string(tc.name) << " ok (-O2 vs -O0): " << ok2 << " != " << ok0);
    }
}

TEST_CASE("NumberScriptInSandbox")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    luaL_sandbox(L);
    luaL_sandboxthread(L);

    mooRunFile(L, "spec/number.luau");

    lua_close(L);
}

TEST_SUITE_END();
