// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The compiler: constant folding and Sccp fold on the raw Q48.16 value with moo_number.h, and give
// the VM's result bit for bit (docs/DESIGN.md "Layer 3 as built").

#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"
#include "doctest.h"
#include "MooHost.h"
#include "Luau/Sccp.h"
#include "Luau/BytecodeGraph.h"
#include "../../Compiler/src/CostModel.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

TEST_SUITE_BEGIN("compiler");

// The compiler's number constants are the raw Q48.16 value (docs/history/layer3/moo-l3-compiler-raw-constants.md;
// docs/DESIGN.md "Layer 3 as built", "The compiler"): a folded constant is the exact raw value, with no double in
// the path and no 2^53 limit, so folding an expression and running it give the same bits at every magnitude.

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
TEST_CASE("CompilerFoldRawAtOrAboveTwoPowFiftyThree")
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
TEST_CASE("CompilerFoldEqualsRuntimeBitForBit")
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

// The bytecode-graph constant propagation (Sccp) folds numbers with moo_number.h, exactly as the compiler does, so a
// folded constant is the VM's result (docs/history/layer3/moo-l3-bytecode-graph-fixed.md; docs/DESIGN.md "Layer 3 as built").
using namespace Luau;
using namespace Luau::Bytecode;


namespace
{

uint64_t nextRandom(uint64_t& state)
{
    state += 0x9e3779b97f4a7c15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// 64 seeded raw Q48.16 operands, including the saturation edges (docs/DESIGN.md "Overflow")
std::vector<int64_t> rawOperands()
{
    std::vector<int64_t> v;
    v.push_back(0);
    v.push_back(1);
    v.push_back(-1);
    v.push_back(MOO_NUMBER_ONE);
    v.push_back(-MOO_NUMBER_ONE);
    v.push_back(MOO_NUMBER_MAX);
    v.push_back(-MOO_NUMBER_MAX);
    v.push_back(MOO_NUMBER_MAX - 1);
    v.push_back(-MOO_NUMBER_MAX + 1);
    v.push_back(int64_t(1) << 47);
    v.push_back(-(int64_t(1) << 47));
    uint64_t state = 1000;
    while (v.size() < 64)
    {
        uint64_t bits = nextRandom(state);
        int shift = int(nextRandom(state) % 63) + 1;
        uint64_t mag = (bits & ((1ull << shift) - 1)) | (1ull << shift);
        int64_t x = mag > uint64_t(INT64_MAX) ? INT64_MAX : int64_t(mag);
        v.push_back(((nextRandom(state) & 1) ? -x : x));
    }
    return v;
}

} // namespace

namespace
{

// the fold Sccp must give: the moo_number.h result, or none where a checked build raises "number overflow"
std::optional<int64_t> wantFold(bool saturates, int64_t value)
{
    if (MOO_OVERFLOW_ERRORS && saturates)
        return std::nullopt;
    return value;
}

} // namespace

TEST_CASE("CompilerSccpFoldOperatorsMatchMooNumber")
{
    std::vector<int64_t> vals = rawOperands();
    for (int64_t a : vals)
    {
        for (int64_t b : vals)
        {
            CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_ADD) == wantFold(moo_numaddsaturates(a, b), moo_numadd(a, b)));
            CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_SUB) == wantFold(moo_numsubsaturates(a, b), moo_numsub(a, b)));
            CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_MUL) == wantFold(moo_nummulsaturates(a, b), moo_nummul(a, b)));
            if (b != 0)
            {
                CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_DIV) == wantFold(moo_numdivsaturates(a, b), moo_numdiv(a, b)));
                CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_IDIV) == wantFold(moo_numidivsaturates(a, b), moo_numidiv(a, b)));
                CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_MOD) == moo_nummod(a, b));
            }
            else
            {
                CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_DIV));
                CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_IDIV));
                CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_MOD));
            }
        }
    }
}

// Sccp refuses a fold that saturates in a checked build, exactly as the compiler's mooFold does, so a re-enabled
// inliner never folds a result the VM would raise "number overflow" for (docs/DESIGN.md "Overflow").
TEST_CASE("CompilerSccpSaturatingFoldRefusedWhenChecked")
{
    using Luau::Bytecode::evaluateNumberBinaryOp;
    const int64_t one = MOO_NUMBER_ONE;
    const int64_t max = MOO_NUMBER_MAX;

    struct Case
    {
        int64_t a;
        int64_t b;
        LuauOpcode op;
        int64_t released;
    };
    const Case cases[] = {
        {max, one, LuauOpcode::LOP_ADD, moo_numadd(max, one)},
        {-max, one, LuauOpcode::LOP_SUB, moo_numsub(-max, one)},
        {max, 2 * one, LuauOpcode::LOP_MUL, moo_nummul(max, 2 * one)},
        {max, 1, LuauOpcode::LOP_DIV, moo_numdiv(max, 1)},
        {max, 1, LuauOpcode::LOP_IDIV, moo_numidiv(max, 1)},
    };
    for (const Case& c : cases)
    {
        std::optional<int64_t> folded = evaluateNumberBinaryOp(c.a, c.b, c.op);
#if MOO_OVERFLOW_ERRORS
        CHECK(!folded);
#else
        CHECK(folded == c.released);
#endif
    }

    // the edges themselves are exact, never saturation, and fold in every build
    CHECK(evaluateNumberBinaryOp(max - one, one, LuauOpcode::LOP_ADD) == max);
    CHECK(evaluateNumberBinaryOp(-max + one, one, LuauOpcode::LOP_SUB) == -max);
    CHECK(evaluateNumberBinaryOp(max, one, LuauOpcode::LOP_MUL) == max);

    // division, floor division and remainder by zero are never folded in any build
    CHECK(!evaluateNumberBinaryOp(one, 0, LuauOpcode::LOP_DIV));
    CHECK(!evaluateNumberBinaryOp(one, 0, LuauOpcode::LOP_IDIV));
    CHECK(!evaluateNumberBinaryOp(one, 0, LuauOpcode::LOP_MOD));
}

TEST_CASE("CompilerSccpPowNeverFolded")
{
    std::vector<int64_t> vals = rawOperands();
    for (int64_t a : vals)
        for (int64_t b : vals)
            CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_POW));
}

TEST_CASE("CompilerSccpGraphFoldsAddOnRaw")
{
    BcFunction<BcVmConst> func;
    BcOp entry = func.entryBlock = func.addBlock();
    BcOp exit = func.exitBlock = func.addBlock();

    BcOp lhs = func.addInst(LOP_LOADN, entry, {func.addImmInt(3)}, Reg(0));
    BcOp rhs = func.addInst(LOP_LOADN, entry, {func.addImmInt(5)}, Reg(1));
    BcOp sum = func.addInst(LOP_ADD, entry, {lhs, rhs}, Reg(2));
    func.addInst(LOP_RETURN, entry, {func.addImmInt(1), sum});
    func.addEdge(entry, exit, BcBlockEdgeKind::Fallthrough);

    BcVmConstImpl impl(func);
    Sccp<BcVmConst> sccp(func, &impl);
    sccp.propagate();

    auto it = sccp.state.opConstness.find(sum);
    REQUIRE(it != nullptr);
    CHECK(it->kind == Constness::Constant);
    CHECK(impl.asNumber(it->constant.value()) == moo_numadd(moo_numfromint(3), moo_numfromint(5)));
}

// The constant folder leaves an arithmetic or ordering operator alone when an operand is a constant that is not a
// number ("a" + 1, true < 2), and the VM raises the ordinary error at run time. Layer 3 removed upstream's vector
// cases for these operators from foldBinary (Compiler/src/ConstantFolding.cpp), so a checked build asserted
// "Unexpected binary operation" while compiling such an expression (tests/determinism/errors.luau has one).

TEST_CASE("CompilerFoldLeavesNonNumberOperandsToTheVM")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    struct Case
    {
        const char* source;
        const char* error;
    };
    const Case cases[] = {
        {"return 'a' + 1", "attempt to perform arithmetic (add) on string and number"},
        {"return 1 - true", "attempt to perform arithmetic (sub) on number and boolean"},
        {"return nil * 2", "attempt to perform arithmetic (mul) on nil and number"},
        {"return 'x' / 'y'", "attempt to perform arithmetic (div) on string"},
        {"return true // 1", "attempt to perform arithmetic (idiv) on boolean and number"},
        {"return 1 % false", "attempt to perform arithmetic (mod) on number and boolean"},
        {"return 'b' ^ 2", "attempt to perform arithmetic (pow) on string and number"},
        {"return true < 2", "attempt to compare boolean < number"},
        {"return 1 <= nil", "attempt to compare number <= nil"},
        {"return 'a' > 1", "attempt to compare number < string"},
        {"return false >= 0", "attempt to compare number <= boolean"},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.source);
        std::string got = mooRun(L, c.source);
        CHECK(got.rfind("error: ", 0) == 0);
        CHECK(got.find(c.error) != std::string::npos);
    }

    // a string that reads as a number still converts at run time, as in Luau
    CHECK(mooRun(L, "return '2' + 1") == "3");
    CHECK(mooRun(L, "return '2' < '10'") == "false");

    lua_close(L);
}

// The loop unroller's trip count reads the raw Q48.16 bounds, with no double, and equals the VM's count
// (moo_numfortrips, LOP_FORNPREP) for every bound it admits: integers in [-32767, 32767].
TEST_CASE("CompilerUnrollTripCountIsTheVMs")
{
    const int64_t bounds[] = {-32767, -10, -3, -1, 0, 1, 2, 3, 7, 10, 32767};
    const int64_t steps[] = {-32767, -3, -2, -1, 1, 2, 3, 32767};
    for (int64_t from : bounds)
        for (int64_t to : bounds)
            for (int64_t step : steps)
            {
                fixed_t f = moo_numfromint(from), t = moo_numfromint(to), s = moo_numfromint(step);
                INFO("for i = " << from << ", " << to << ", " << step);
                CHECK_EQ(int64_t(Luau::Compile::getTripCount(f, t, s)), int64_t(moo_numfortrips(f, t, s)));
            }

    // refused: a zero step, a fraction, a bound past 32767, a raw value past 2^53
    const fixed_t one = moo_numfromint(1);
    CHECK_EQ(Luau::Compile::getTripCount(one, moo_numfromint(10), 0), -1);
    CHECK_EQ(Luau::Compile::getTripCount(one + MOO_NUMBER_ONE / 2, moo_numfromint(10), one), -1);
    CHECK_EQ(Luau::Compile::getTripCount(one, moo_numfromint(32768), one), -1);
    CHECK_EQ(Luau::Compile::getTripCount(one, (int64_t(1) << 53) + 1, one), -1);
}

// An unrolled loop (-O2) returns the same bits as the loop the VM runs (-O0).
TEST_CASE("CompilerUnrolledLoopEqualsTheVM")
{
    MooState s;
    const char* source = "local t = {} for i = -3, 9, 4 do t[#t + 1] = i * 0.1 + i end return t[1] + t[2] * 3 + t[3] * 5 + t[4] * 7";

    int64_t raws[2] = {};
    for (int level = 0; level <= 2; level += 2)
    {
        lua_CompileOptions options = {};
        options.optimizationLevel = level;
        size_t size = 0;
        char* bytecode = luau_compile(source, strlen(source), &options, &size);
        int status = luau_load(s.L, "=moo", bytecode, size, 0);
        free(bytecode);
        if (status == 0)
            status = lua_pcall(s.L, 0, 1, 0);
        REQUIRE(status == 0);
        raws[level / 2] = int64_t(lua_tonumber(s.L, -1));
        lua_pop(s.L, 1);
    }
    CHECK_EQ(raws[0], raws[1]);
}

TEST_SUITE_END();
