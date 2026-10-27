// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The number oracle: every operator, literal path, boundary function and integer-argument rule checked
// bit for bit against fixed's C (tests/moo/moo_oracle_fixed.c; docs/DESIGN.md "Conformance (layer 3)").

#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"
#include "doctest.h"
#include "MooHost.h"
#include "luacode.h"
#include "moo.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

TEST_SUITE_BEGIN("oracle");

// The layer 3 oracle (docs/DESIGN.md "Layers", layer 3: "bit-exact agreement with fixed's C on every operator over
// random and edge operands"; docs/history/layer3/moo-l3-oracle-harness.md). Every number operator is computed by the oracle,
// fixed's own C in tests/moo/moo_oracle_fixed.c, and by moo four ways, which must all equal it bit for bit:
//   direct  moo_number.h's function;
//   vm      the VM, on operands passed in through the C API (register operands, so nothing folds);
//   fold    the compiler, on the same expression with both operands as literals (constant folding);
//   k       the VM with one operand a literal constant (the K-operand opcodes: ADDK, SUBRK, DIVRK and the rest).
// For / // % by zero the oracle's precondition excludes the operation, and moo must raise `division by zero` on
// every path that runs it. MOO_ORACLE_N sets the number of random pairs (default 2^16; the nightly raises it).
extern "C"
{
    int64_t moo_oracle_add(int64_t a, int64_t b);
    int64_t moo_oracle_sub(int64_t a, int64_t b);
    int64_t moo_oracle_unm(int64_t a);
    int64_t moo_oracle_mul(int64_t a, int64_t b);
    int64_t moo_oracle_div(int64_t a, int64_t b);
    int64_t moo_oracle_idiv(int64_t a, int64_t b);
    int64_t moo_oracle_mod(int64_t a, int64_t b);
    int64_t moo_oracle_lt(int64_t a, int64_t b);
    int64_t moo_oracle_le(int64_t a, int64_t b);
    int64_t moo_oracle_gt(int64_t a, int64_t b);
    int64_t moo_oracle_ge(int64_t a, int64_t b);
    int64_t moo_oracle_eq(int64_t a, int64_t b);
    int64_t moo_oracle_ne(int64_t a, int64_t b);
}

namespace
{

typedef int64_t (*MooOracleFn)(int64_t a, int64_t b);

enum MooOracleKind
{
    MooOracleArith,   // a number result
    MooOracleDivide,  // a number result; a zero divisor is the error `division by zero` (DESIGN.md "Division by zero")
    MooOracleCompare, // a boolean result, as 0 or 1
    MooOracleUnary,   // -a; b is unused
};

struct MooOracleOp
{
    const char* sym;
    MooOracleKind kind;
    MooOracleFn oracle;
    MooOracleFn direct;
};

int64_t mooOracleUnm(int64_t a, int64_t)
{
    return moo_oracle_unm(a);
}

// docs/DESIGN.md "Semantics", the operator table: one row per moo operator
const MooOracleOp kMooOracleOps[] = {
    {"+", MooOracleArith, moo_oracle_add, [](int64_t a, int64_t b) -> int64_t { return moo_numadd(a, b); }},
    {"-", MooOracleArith, moo_oracle_sub, [](int64_t a, int64_t b) -> int64_t { return moo_numsub(a, b); }},
    {"*", MooOracleArith, moo_oracle_mul, [](int64_t a, int64_t b) -> int64_t { return moo_nummul(a, b); }},
    {"/", MooOracleDivide, moo_oracle_div, [](int64_t a, int64_t b) -> int64_t { return moo_numdiv(a, b); }},
    {"//", MooOracleDivide, moo_oracle_idiv, [](int64_t a, int64_t b) -> int64_t { return moo_numidiv(a, b); }},
    {"%", MooOracleDivide, moo_oracle_mod, [](int64_t a, int64_t b) -> int64_t { return moo_nummod(a, b); }},
    {"unary -", MooOracleUnary, mooOracleUnm, [](int64_t a, int64_t) -> int64_t { return moo_numunm(a); }},
    {"<", MooOracleCompare, moo_oracle_lt, [](int64_t a, int64_t b) -> int64_t { return a < b; }},
    {"<=", MooOracleCompare, moo_oracle_le, [](int64_t a, int64_t b) -> int64_t { return a <= b; }},
    {">", MooOracleCompare, moo_oracle_gt, [](int64_t a, int64_t b) -> int64_t { return a > b; }},
    {">=", MooOracleCompare, moo_oracle_ge, [](int64_t a, int64_t b) -> int64_t { return a >= b; }},
    {"==", MooOracleCompare, moo_oracle_eq, [](int64_t a, int64_t b) -> int64_t { return a == b; }},
    {"~=", MooOracleCompare, moo_oracle_ne, [](int64_t a, int64_t b) -> int64_t { return a != b; }},
};

const int kMooOracleOpCount = int(sizeof(kMooOracleOps) / sizeof(kMooOracleOps[0]));

// true when release saturates this pair, so a checked build (MOO_OVERFLOW_ERRORS) raises `number overflow in '<op>'`
// instead (docs/DESIGN.md "Overflow"); unary -, % and the comparisons never saturate
bool mooOracleSaturates(const MooOracleOp& op, int64_t a, int64_t b)
{
    if (strcmp(op.sym, "+") == 0)
        return moo_numaddsaturates(a, b);
    if (strcmp(op.sym, "-") == 0)
        return moo_numsubsaturates(a, b);
    if (strcmp(op.sym, "*") == 0)
        return moo_nummulsaturates(a, b);
    if (strcmp(op.sym, "/") == 0)
        return moo_numdivsaturates(a, b);
    if (strcmp(op.sym, "//") == 0)
        return moo_numidivsaturates(a, b);
    return false;
}

// true when this build raises for the pair instead of saturating
bool mooOracleRaises(const MooOracleOp& op, int64_t a, int64_t b)
{
    return MOO_OVERFLOW_ERRORS && mooOracleSaturates(op, a, b);
}

// PCG32 (O'Neill, pcg32_random_r), with a fixed seed and stream, so every run checks the same pairs
struct MooOraclePcg32
{
    uint64_t state = 0;
    uint64_t inc = 0;

    MooOraclePcg32(uint64_t seed, uint64_t stream)
    {
        inc = (stream << 1) | 1;
        next();
        state += seed;
        next();
    }

    uint32_t next()
    {
        uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        uint32_t xorshifted = uint32_t(((old >> 18) ^ old) >> 27);
        uint32_t rot = uint32_t(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
    }

    uint64_t next64()
    {
        uint64_t hi = next();
        return (hi << 32) | next();
    }
};

const uint64_t kMooOracleSeed = 0x6d6f6f2d6f72636cull; // "moo-orcl"
const uint64_t kMooOracleStream = 0x0000000000000003ull;

// the edge operands (docs/history/layer3/moo-l3-oracle-harness.md, "Operands"), each with its negation
std::vector<int64_t> mooOracleEdges()
{
    const int64_t one = MOO_NUMBER_ONE;
    const int64_t magnitudes[] = {
        0,
        1,                                // one raw unit
        2,
        3,                                // an odd raw: 3 * 0.5 drops exactly half (a fixMul tie)
        0x7fff,
        one / 2,                          // 0.5
        one / 2 + 1,
        0xffff,                           // every fraction bit set
        one,                              // 1
        one + 1,
        one + one / 2,                    // 1.5: times one raw drops exactly half
        2 * one - 1,                      // 1 + every fraction bit
        int64_t(1) << 31,                 // 2^31 raw
        int64_t(1) << 32,                 // 2^32 raw, the number 2^16
        int64_t(0xB504F333F9),            // about 2^39.5 raw: the square saturates fixMul
        int64_t(1) << 40,
        (int64_t(1) << 47) - 1,           // 2^47 raw: fixDiv's fast-path boundary
        int64_t(1) << 47,                 // the number 2^31
        (int64_t(1) << 47) + 1,
        int64_t(1) << 48,                 // the number 2^32
        (int64_t(1) << 53) - 1,           // the largest raw a literal carries exactly (DESIGN.md "Layer 3 as built")
        int64_t(1) << 53,
        MOO_NUMBER_INT_MAX << 16,         // the largest integer: the edge of // saturation
        (MOO_NUMBER_INT_MAX << 16) - 1,
        MOO_NUMBER_MAX - one,
        MOO_NUMBER_MAX - 1,
        MOO_NUMBER_MAX,                   // INT64_MAX raw; every fraction bit set
    };

    std::vector<int64_t> edges;
    for (int64_t m : magnitudes)
    {
        edges.push_back(m);
        if (m != 0)
            edges.push_back(-m);
    }
    return edges;
}

// a random operand: the whole domain, small magnitudes, any bit width, or near an edge
int64_t mooOracleRandomOperand(MooOraclePcg32& rng, const std::vector<int64_t>& edges)
{
    uint32_t mode = rng.next() % 4;
    uint64_t bits = rng.next64();
    uint64_t magnitude = 0;

    if (mode == 0)
        magnitude = bits >> 1; // the whole domain
    else if (mode == 1)
        magnitude = bits >> (64 - (1 + rng.next() % 24)); // small: below 2^24 raw, the number 256
    else if (mode == 2)
        magnitude = bits >> (64 - (1 + rng.next() % 63)); // any width from 1 to 63 bits
    else
    {
        int64_t edge = edges[rng.next() % edges.size()];
        int64_t delta = int64_t(rng.next() % 17) - 8;
        if (delta > 0 && edge > MOO_NUMBER_MAX - delta)
            return MOO_NUMBER_MAX;
        if (delta < 0 && edge < MOO_NUMBER_MIN - delta)
            return MOO_NUMBER_MIN;
        return edge + delta;
    }

    int64_t value = int64_t(magnitude); // at most INT64_MAX
    return (bits & 1) ? -value : value;
}

// the number of random pairs: MOO_ORACLE_N, or 2^16
uint64_t mooOracleRandomCount()
{
    const char* text = getenv("MOO_ORACLE_N");
    if (!text || !*text)
        return uint64_t(1) << 16;
    char* end = nullptr;
    unsigned long long n = strtoull(text, &end, 10);
    REQUIRE_MESSAGE((*end == 0 && n > 0 && text[0] != '-'), "MOO_ORACLE_N must be a positive decimal count, not '" << text << "'");
    return uint64_t(n);
}

// the exact decimal literal of a raw value: r / 2^16 has at most 16 fraction digits (10^16 / 2^16 = 5^16), and the
// literal parser reads it exactly (DESIGN.md "Literals"). Parenthesized, so a negative literal is one operand.
std::string mooOracleLiteral(int64_t r)
{
    uint64_t m = r < 0 ? uint64_t(0) - uint64_t(r) : uint64_t(r);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "(%s%llu.%016llu)", r < 0 ? "-" : "", (unsigned long long)(m >> 16),
        (unsigned long long)((m & 0xffff) * 152587890625ull));
    return buffer;
}

// a literal travels as the double that holds it until the moo bytecode version (DESIGN.md "Layer 3 as built",
// TEMPORARY bridges), which is exact below 2^53 raw; the literal paths check the operands it carries exactly
bool mooOracleLiteralExact(int64_t r)
{
    return r < (int64_t(1) << 53) && r > -(int64_t(1) << 53);
}

std::string mooOracleHex(int64_t r)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "0x%016llx", (unsigned long long)uint64_t(r)); // the raw int64, two's complement
    return buffer;
}

enum MooOraclePath
{
    MooPathDirect,
    MooPathVm,
    MooPathFold,
    MooPathKRight,
    MooPathKLeft,
    MooPathCount,
};

const char* const kMooOraclePathNames[MooPathCount] = {"direct", "vm", "fold", "k-right", "k-left"};

// The literal paths of one batch. fold needs both operands as exact literals, k-right only b, k-left only a; each
// entry is one expression of a returned table. Pairs go in chunks of kMooOracleLiteralChunk, so a chunk holds at most
// 3 constants per pair (the two operands and the folded result) and every constant index stays within the 8 bits a
// K-operand opcode (ADDK, SUBRK, ...) encodes; past 255 the compiler would load the constant into a register instead.
const size_t kMooOracleLiteralChunk = 64;

struct MooOracleEntry
{
    size_t pair;
    int path; // MooPathFold, MooPathKRight or MooPathKLeft
    std::string text;
};

// the expression `x OP y` (or `-x`) in source
std::string mooOracleExpression(const char* sym, bool unary, const std::string& x, const std::string& y)
{
    if (unary)
        return "-" + x;
    return x + " " + sym + " " + y;
}

// the source of one chunk: `local A, B = ...` and a table of the entries in [begin, end)
std::string mooOracleLiteralSource(const std::vector<MooOracleEntry>& entries, size_t begin, size_t end)
{
    std::string source = "local A, B = ...\nreturn {\n";
    for (size_t e = begin; e < end; ++e)
        source += entries[e].text + ",\n";
    return source + "}\n";
}

// one path's result for one pair
struct MooOracleOutcome
{
    enum State
    {
        NotRun,
        Value,
        DivisionByZero,
        Overflow, // `number overflow in '<op>'`, the checked build's error where release saturates
        Wrong, // an error that is not `division by zero`, or a result of the wrong type
    };

    State state = NotRun;
    int64_t value = 0;
    std::string text;

    std::string describe() const
    {
        if (state == NotRun)
            return "-";
        if (state == DivisionByZero)
            return "error(division by zero)";
        if (state == Overflow)
            return "error(number overflow)";
        if (state == Wrong)
            return "wrong(" + text + ")";
        return mooOracleHex(value) + " (" + std::to_string(static_cast<long long>(value)) + ")";
    }
};

struct MooOracleHarness
{
    lua_State* L = nullptr;
    const char* label = "moo-oracle";
    int limit = 10;
    uint64_t checked = 0;
    uint64_t literalSkipped = 0;
    uint64_t zeroDivisors = 0;
    int mismatches = 0;
    uint64_t pathRuns[MooPathCount] = {};
    int corruptPath = -1; // the negative control: one raw unit (or the boolean) wrong on this path's every value

    MooOracleHarness()
    {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    ~MooOracleHarness()
    {
        lua_close(L);
    }

    bool stopped() const
    {
        return mismatches >= limit;
    }

    // compiles and loads source, leaving the function on the stack; a compile or load error is a test failure
    void load(const std::string& source)
    {
        std::string bytecode = Luau::compile(source);
        int status = luau_load(L, "=moo-oracle", bytecode.data(), bytecode.size(), 0);
        REQUIRE_MESSAGE(status == 0, "moo-oracle: cannot load: " << (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?"));
    }

    // reads the value at idx as the outcome of an operator of the given kind
    MooOracleOutcome read(const MooOracleOp& op, int idx)
    {
        MooOracleOutcome out;
        int type = lua_type(L, idx);
        if (op.kind == MooOracleCompare && type == LUA_TBOOLEAN)
        {
            out.state = MooOracleOutcome::Value;
            out.value = lua_toboolean(L, idx) ? 1 : 0;
        }
        else if (op.kind != MooOracleCompare && type == LUA_TNUMBER)
        {
            out.state = MooOracleOutcome::Value;
            out.value = lua_tonumber(L, idx);
        }
        else
        {
            out.state = MooOracleOutcome::Wrong;
            out.text = lua_typename(L, type);
        }
        return out;
    }

    // runs the function under it on the stack with nargs arguments; returns 1 result or the error
    MooOracleOutcome call(const MooOracleOp& op, int nargs)
    {
        MooOracleOutcome out;
        if (lua_pcall(L, nargs, 1, 0) != 0)
        {
            std::string message = lua_isstring(L, -1) ? lua_tostring(L, -1) : "?";
            std::string overflow = std::string("number overflow in '") + op.sym + "'";
            if (message.find("division by zero") != std::string::npos)
                out.state = MooOracleOutcome::DivisionByZero;
            else if (message.find(overflow) != std::string::npos)
                out.state = MooOracleOutcome::Overflow;
            else
                out.state = MooOracleOutcome::Wrong;
            out.text = message;
        }
        else
            out = read(op, -1);
        lua_pop(L, 1);
        return out;
    }

    static std::string expression(const MooOracleOp& op, const std::string& x, const std::string& y)
    {
        return mooOracleExpression(op.sym, op.kind == MooOracleUnary, x, y);
    }

    // the literal entries of a batch, each path gated on the operands it writes as literals
    std::vector<MooOracleEntry> literalEntries(const MooOracleOp& op, const std::vector<int64_t>& a, const std::vector<int64_t>& b)
    {
        std::vector<MooOracleEntry> entries;
        bool unary = op.kind == MooOracleUnary;
        for (size_t i = 0; i < a.size(); ++i)
        {
            bool exactA = mooOracleLiteralExact(a[i]);
            bool exactB = unary || mooOracleLiteralExact(b[i]);
            std::string la = mooOracleLiteral(a[i]);
            std::string lb = mooOracleLiteral(b[i]);
            std::string index = std::to_string(i + 1);
            int paths = unary ? 1 : 3;
            int written = 0;
            if (exactA && exactB)
                entries.push_back({i, MooPathFold, expression(op, la, lb)}), written++;
            if (!unary && exactB)
                entries.push_back({i, MooPathKRight, expression(op, "A[" + index + "]", lb)}), written++;
            if (!unary && exactA)
                entries.push_back({i, MooPathKLeft, expression(op, la, "B[" + index + "]")}), written++;
            literalSkipped += uint64_t(paths - written);
        }
        return entries;
    }

    void pushArray(const std::vector<int64_t>& values)
    {
        lua_createtable(L, int(values.size()), 0);
        for (size_t i = 0; i < values.size(); ++i)
        {
            lua_pushnumber(L, values[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
    }

    // compares every path of one pair against the oracle; prints a mismatch line, at most `limit` of them
    void compare(const MooOracleOp& op, int64_t a, int64_t b, const MooOracleOutcome (&paths)[MooPathCount])
    {
        bool byZero = op.kind == MooOracleDivide && b == 0;
        MooOracleOutcome expected;
        if (byZero)
            expected.state = MooOracleOutcome::DivisionByZero;
        else
        {
            expected.state = MooOracleOutcome::Value;
            expected.value = op.oracle(a, b);
        }

        checked += 1;

        // a checked build raises where release saturates; moo_number.h itself (the direct path) still saturates
        bool raises = !byZero && mooOracleRaises(op, a, b);

        bool agree = true;
        for (int p = 0; p < MooPathCount; ++p)
        {
            const MooOracleOutcome& got = paths[p];
            if (got.state == MooOracleOutcome::NotRun)
                continue;
            pathRuns[p] += 1;
            MooOracleOutcome::State want = raises && p != MooPathDirect ? MooOracleOutcome::Overflow : expected.state;
            if (got.state != want || (got.state == MooOracleOutcome::Value && got.value != expected.value))
                agree = false;
        }

        if (agree || stopped())
            return;

        mismatches += 1;
        std::string line = std::string(label) + " MISMATCH op=" + op.sym + " a=" + mooOracleHex(a) + " (" +
                           std::to_string(static_cast<long long>(a)) + ")";
        if (op.kind != MooOracleUnary)
            line += " b=" + mooOracleHex(b) + " (" + std::to_string(static_cast<long long>(b)) + ")";
        line += " oracle=" + expected.describe();
        for (int p = 0; p < MooPathCount; ++p)
            line += std::string(" ") + kMooOraclePathNames[p] + "=" + paths[p].describe();
        printf("%s\n", line.c_str());
        fflush(stdout);
    }

    // one divide-like pair with a zero divisor: every path runs alone, since each must raise the error
    void checkZero(const MooOracleOp& op, int64_t a)
    {
        zeroDivisors += 1;
        MooOracleOutcome paths[MooPathCount];

        // direct: moo_number.h does not define division by zero; the VM raises before calling it

        load(std::string("local a, b = ...\nreturn a ") + op.sym + " b");
        lua_pushnumber(L, a);
        lua_pushnumber(L, int64_t(0));
        paths[MooPathVm] = call(op, 2);

        load(std::string("local a = ...\nreturn a ") + op.sym + " 0");
        lua_pushnumber(L, a);
        paths[MooPathKRight] = call(op, 1);

        if (mooOracleLiteralExact(a))
        {
            std::string la = mooOracleLiteral(a);
            load("return " + expression(op, la, "0"));
            paths[MooPathFold] = call(op, 0);

            load(std::string("local b = ...\nreturn ") + la + " " + op.sym + " b");
            lua_pushnumber(L, int64_t(0));
            paths[MooPathKLeft] = call(op, 1);
        }
        else
            literalSkipped += 1;

        compare(op, a, 0, paths);
    }

    // one pair a checked build raises for (MOO_OVERFLOW_ERRORS): every path but direct runs alone, since each must
    // raise `number overflow in '<op>'`; the compiler does not fold it, so the fold path raises at run time too
    void checkOverflow(const MooOracleOp& op, int64_t a, int64_t b)
    {
        MooOracleOutcome paths[MooPathCount];
        paths[MooPathDirect].state = MooOracleOutcome::Value;
        paths[MooPathDirect].value = op.direct(a, b);

        load("local a, b = ...\nreturn " + expression(op, "a", "b"));
        lua_pushnumber(L, a);
        lua_pushnumber(L, b);
        paths[MooPathVm] = call(op, 2);

        bool exactA = mooOracleLiteralExact(a);
        bool exactB = mooOracleLiteralExact(b);
        std::string la = mooOracleLiteral(a);
        std::string lb = mooOracleLiteral(b);
        int written = 0;
        if (exactA && exactB)
        {
            load("return " + expression(op, la, lb));
            paths[MooPathFold] = call(op, 0);
            written++;
        }
        if (exactB)
        {
            load("local a = ...\nreturn " + expression(op, "a", lb));
            lua_pushnumber(L, a);
            paths[MooPathKRight] = call(op, 1);
            written++;
        }
        if (exactA)
        {
            load("local b = ...\nreturn " + expression(op, la, "b"));
            lua_pushnumber(L, b);
            paths[MooPathKLeft] = call(op, 1);
            written++;
        }
        literalSkipped += uint64_t(3 - written);

        compare(op, a, b, paths);
    }

    // a batch of pairs of one operator, all paths
    void checkBatch(const MooOracleOp& op, const std::vector<int64_t>& as, const std::vector<int64_t>& bs)
    {
        std::vector<int64_t> a, b;
        for (size_t i = 0; i < as.size() && !stopped(); ++i)
        {
            if (op.kind == MooOracleDivide && bs[i] == 0)
                checkZero(op, as[i]);
            else if (mooOracleRaises(op, as[i], bs[i]))
                checkOverflow(op, as[i], bs[i]);
            else
                a.push_back(as[i]), b.push_back(bs[i]);
        }
        if (a.empty() || stopped())
            return;

        size_t n = a.size();
        std::vector<MooOracleOutcome> vm(n), fold(n), kRight(n), kLeft(n);

        // vm: register operands read from tables the host filled through the C API
        load(std::string("local A, B, R, n = ...\nfor i = 1, n do R[i] = ") + expression(op, "A[i]", "B[i]") + " end\nreturn R");
        pushArray(a);
        pushArray(b);
        lua_createtable(L, int(n), 0);
        lua_pushinteger(L, int(n));
        REQUIRE_MESSAGE(lua_pcall(L, 4, 1, 0) == 0, "moo-oracle: vm batch: " << (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?"));
        for (size_t i = 0; i < n; ++i)
        {
            lua_rawgeti(L, -1, int(i + 1));
            vm[i] = read(op, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        // fold and k: the operands as literals, in chunks of kMooOracleLiteralChunk pairs
        std::vector<MooOracleEntry> entries = literalEntries(op, a, b);
        for (size_t begin = 0; begin < entries.size();)
        {
            size_t end = begin;
            while (end < entries.size() && entries[end].pair < entries[begin].pair + kMooOracleLiteralChunk)
                ++end;

            load(mooOracleLiteralSource(entries, begin, end));
            pushArray(a);
            pushArray(b);
            REQUIRE_MESSAGE(lua_pcall(L, 2, 1, 0) == 0, "moo-oracle: literal batch: " << (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?"));
            for (size_t e = begin; e < end; ++e)
            {
                lua_rawgeti(L, -1, int(e - begin + 1));
                MooOracleOutcome got = read(op, -1);
                lua_pop(L, 1);
                if (entries[e].path == MooPathFold)
                    fold[entries[e].pair] = got;
                else if (entries[e].path == MooPathKRight)
                    kRight[entries[e].pair] = got;
                else
                    kLeft[entries[e].pair] = got;
            }
            lua_pop(L, 1);
            begin = end;
        }

        for (size_t i = 0; i < n && !stopped(); ++i)
        {
            MooOracleOutcome paths[MooPathCount];
            paths[MooPathDirect].state = MooOracleOutcome::Value;
            paths[MooPathDirect].value = op.direct(a[i], b[i]);
            paths[MooPathVm] = vm[i];
            paths[MooPathFold] = fold[i];
            paths[MooPathKRight] = kRight[i];
            paths[MooPathKLeft] = kLeft[i];
            if (corruptPath >= 0 && paths[corruptPath].state == MooOracleOutcome::Value)
            {
                int64_t& v = paths[corruptPath].value;
                v = op.kind == MooOracleCompare ? (v ^ 1) : (v < MOO_NUMBER_MAX ? v + 1 : v - 1);
            }
            compare(op, a[i], b[i], paths);
        }
    }

    // every operator over the pairs, in batches
    void checkAll(const MooOracleOp* ops, int opCount, const std::vector<int64_t>& as, const std::vector<int64_t>& bs)
    {
        const size_t batch = 512;
        for (int k = 0; k < opCount && !stopped(); ++k)
            for (size_t start = 0; start < as.size() && !stopped(); start += batch)
            {
                size_t end = start + batch < as.size() ? start + batch : as.size();
                std::vector<int64_t> a(as.begin() + start, as.begin() + end);
                std::vector<int64_t> b(bs.begin() + start, bs.begin() + end);
                checkBatch(ops[k], a, b);
            }
    }
};

// the edge set crossed with itself
void mooOracleEdgePairs(std::vector<int64_t>& as, std::vector<int64_t>& bs)
{
    std::vector<int64_t> edges = mooOracleEdges();
    for (int64_t x : edges)
        for (int64_t y : edges)
            as.push_back(x), bs.push_back(y);
}

} // namespace


// DESIGN.md "Layers", layer 3: every operator, bit-exact against fixed's C, over the edge set crossed with itself and
// MOO_ORACLE_N seeded random pairs, on moo_number.h, the VM, the compiler's folding and the K-operand opcodes.
TEST_CASE("OracleEveryOperator")
{
    std::vector<int64_t> as, bs;
    mooOracleEdgePairs(as, bs);
    size_t edgePairs = as.size();

    std::vector<int64_t> edges = mooOracleEdges();
    uint64_t randomPairs = mooOracleRandomCount();
    MooOraclePcg32 rng(kMooOracleSeed, kMooOracleStream);
    for (uint64_t i = 0; i < randomPairs; ++i)
    {
        int64_t x = mooOracleRandomOperand(rng, edges);
        int64_t y = mooOracleRandomOperand(rng, edges);
        as.push_back(x), bs.push_back(y);
    }

    MooOracleHarness harness;
    harness.checkAll(kMooOracleOps, kMooOracleOpCount, as, bs);

    printf("moo-oracle pairs=%llu edge=%llu random=%llu seed=0x%016llx zero-divisors=%llu literal-skipped=%llu\n",
        (unsigned long long)as.size(), (unsigned long long)edgePairs, (unsigned long long)randomPairs, (unsigned long long)kMooOracleSeed,
        (unsigned long long)harness.zeroDivisors, (unsigned long long)harness.literalSkipped);
    printf("moo-oracle paths direct=%llu vm=%llu fold=%llu k-right=%llu k-left=%llu\n", (unsigned long long)harness.pathRuns[MooPathDirect],
        (unsigned long long)harness.pathRuns[MooPathVm], (unsigned long long)harness.pathRuns[MooPathFold],
        (unsigned long long)harness.pathRuns[MooPathKRight], (unsigned long long)harness.pathRuns[MooPathKLeft]);
    printf("moo-oracle checked=%llu operators=%d mismatches=%d\n", (unsigned long long)harness.checked, kMooOracleOpCount, harness.mismatches);
    fflush(stdout);

    CHECK(harness.checked == uint64_t(as.size()) * uint64_t(kMooOracleOpCount));
    CHECK(harness.mismatches == 0);
    CHECK(harness.pathRuns[MooPathVm] == harness.checked);
    CHECK(harness.pathRuns[MooPathFold] > 0);
}

// The negative control: a planted one-unit error in `*` on moo's side (one raw unit more on every fixMul rounding
// tie, where the dropped bits are exactly half) must turn the harness red, print the mismatch, and stop after 10.
TEST_CASE("OracleNegativeControlCatchesOneUnit")
{
    MooOracleOp planted[kMooOracleOpCount];
    for (int k = 0; k < kMooOracleOpCount; ++k)
        planted[k] = kMooOracleOps[k];

    int mul = -1;
    for (int k = 0; k < kMooOracleOpCount; ++k)
        if (strcmp(planted[k].sym, "*") == 0)
            mul = k;
    REQUIRE(mul >= 0);

    planted[mul].direct = [](int64_t a, int64_t b) -> int64_t {
        int64_t r = moo_nummul(a, b);
        bool tie = ((uint64_t(a) * uint64_t(b)) & 0xffff) == 0x8000; // the low 16 bits of the exact product
        return tie && r < MOO_NUMBER_MAX ? r + 1 : r;
    };

    std::vector<int64_t> as, bs;
    mooOracleEdgePairs(as, bs);

    MooOracleHarness harness;
    harness.label = "moo-oracle negative-control (expected)";
    harness.checkAll(planted + mul, 1, as, bs);

    CHECK(harness.mismatches == harness.limit);
    CHECK(harness.stopped());
    CHECK(harness.checked < uint64_t(as.size()));

    // and the same pairs with the real `*` agree
    MooOracleHarness clean;
    clean.checkAll(kMooOracleOps + mul, 1, as, bs);
    CHECK(clean.mismatches == 0);
    CHECK(clean.checked == uint64_t(as.size()));
}

// The negative control on the other paths: one raw unit wrong on every value of one path (vm, fold, k-right or
// k-left) must turn the harness red, so a harness that ran a path but stopped comparing it would fail here.
TEST_CASE("OracleNegativeControlCoversEveryPath")
{
    int sub = -1;
    for (int k = 0; k < kMooOracleOpCount; ++k)
        if (strcmp(kMooOracleOps[k].sym, "-") == 0)
            sub = k;
    REQUIRE(sub >= 0);

    std::vector<int64_t> as, bs;
    mooOracleEdgePairs(as, bs);

    const int paths[] = {MooPathVm, MooPathFold, MooPathKRight, MooPathKLeft};
    for (int path : paths)
    {
        MooOracleHarness harness;
        harness.label = "moo-oracle negative-control (expected)";
        harness.limit = 1;
        harness.corruptPath = path;
        harness.checkAll(kMooOracleOps + sub, 1, as, bs);
        CHECK_MESSAGE(harness.mismatches == 1, "path " << kMooOraclePathNames[path]);
    }
}

// the lines of a dump whose first word is opcode
int mooOracleCountOpcode(const std::string& dump, const char* opcode)
{
    int count = 0;
    size_t start = 0;
    std::string word = std::string(opcode) + " ";
    while (start < dump.size())
    {
        size_t end = dump.find('\n', start);
        if (end == std::string::npos)
            end = dump.size();
        if (dump.compare(start, word.size(), word) == 0)
            count++;
        start = end + 1;
    }
    return count;
}

std::string mooOracleDump(const std::string& source)
{
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code);
    Luau::compileOrThrow(bcb, source);
    return bcb.dumpEverything();
}

// The literal paths test what they say: a full chunk of the harness's own literal source folds every fold entry
// (no arithmetic or comparison opcode is left for it) and compiles every k entry with a constant operand to the
// K-operand opcode, up to the chunk's last pair.
TEST_CASE("OracleLiteralPathsFoldAndReachKOpcodes")
{
    std::vector<int64_t> a, b;
    std::vector<int64_t> edges = mooOracleEdges();
    MooOraclePcg32 rng(kMooOracleSeed, kMooOracleStream);
    while (a.size() < kMooOracleLiteralChunk)
    {
        int64_t x = int64_t(rng.next64() >> 34) - (int64_t(1) << 29); // below 2^29 raw: every result is below 2^53, so it folds
        int64_t y = int64_t(rng.next64() >> 34) - (int64_t(1) << 29);
        if (y != 0)
            a.push_back(x), b.push_back(y);
    }

    // the K opcode each operator's k-right (and, where Luau has one, k-left) entries compile to
    struct
    {
        const char* sym;
        const char* right;
        const char* left;
    } kOps[] = {
        {"+", "ADDK", nullptr},
        {"-", "SUBK", "SUBRK"},
        {"*", "MULK", nullptr},
        {"/", "DIVK", "DIVRK"},
        {"//", "IDIVK", nullptr},
        {"%", "MODK", nullptr},
    };

    for (const auto& k : kOps)
    {
        const MooOracleOp* op = nullptr;
        for (const MooOracleOp& o : kMooOracleOps)
            if (strcmp(o.sym, k.sym) == 0)
                op = &o;
        REQUIRE(op);

        MooOracleHarness harness;
        std::vector<MooOracleEntry> entries = harness.literalEntries(*op, a, b);
        REQUIRE(entries.size() == 3 * kMooOracleLiteralChunk);
        std::string dump = mooOracleDump(mooOracleLiteralSource(entries, 0, entries.size()));
        CHECK_MESSAGE(mooOracleCountOpcode(dump, k.right) == int(kMooOracleLiteralChunk), k.sym << "\n" << dump);
        if (k.left)
            CHECK_MESSAGE(mooOracleCountOpcode(dump, k.left) == int(kMooOracleLiteralChunk), k.sym << "\n" << dump);
    }

    // fold: the fold entries alone compile to constants, for every operator
    const char* const kRuntime[] = {"ADD", "SUB", "MUL", "DIV", "IDIV", "MOD", "ADDK", "SUBK", "MULK", "DIVK", "IDIVK", "MODK", "SUBRK",
        "DIVRK", "MINUS", "JUMPIFLT", "JUMPIFLE", "JUMPIFNOTLT", "JUMPIFNOTLE", "JUMPIFEQ", "JUMPIFNOTEQ", "JUMPXEQKN"};
    for (const MooOracleOp& op : kMooOracleOps)
    {
        MooOracleHarness harness;
        std::vector<MooOracleEntry> all = harness.literalEntries(op, a, b), fold;
        for (const MooOracleEntry& e : all)
            if (e.path == MooPathFold)
                fold.push_back(e);
        REQUIRE(fold.size() == kMooOracleLiteralChunk);
        std::string dump = mooOracleDump(mooOracleLiteralSource(fold, 0, fold.size()));
        for (const char* name : kRuntime)
            CHECK_MESSAGE(mooOracleCountOpcode(dump, name) == 0, op.sym << " left " << name << " unfolded\n" << dump);
    }
}

// The harness's own pieces: the literal text is exact, and the PCG32 stream is the reference one.
TEST_CASE("OracleLiteralsAndGenerator")
{
    CHECK(mooOracleLiteral(0) == "(0.0000000000000000)");
    CHECK(mooOracleLiteral(1) == "(0.0000152587890625)");
    CHECK(mooOracleLiteral(-98304) == "(-1.5000000000000000)");
    CHECK(mooOracleLiteral(0xffff) == "(0.9999847412109375)");

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK(mooRun(L, ("return moo_test.raw" + mooOracleLiteral(0xffff)).c_str()) == "65535");
    CHECK(mooRun(L, ("return moo_test.raw" + mooOracleLiteral(-((int64_t(1) << 53) - 1))).c_str()) == "-9007199254740991");
    lua_close(L);

    // pcg32_random_r with initstate 42, initseq 54: the reference demo's first outputs
    MooOraclePcg32 rng(42, 54);
    CHECK(rng.next() == 0xa15c02b7u);
    CHECK(rng.next() == 0x7b47f409u);
    CHECK(rng.next() == 0xba1d3330u);
}

// The layer 3 closing gate, piece 1: the oracle (oracle.test.cpp) extended to the layer 3
// surfaces that landed since. The oracle is fixed's own C (tests/moo/moo_oracle_fixed.c), reused here by name;
// each surface below is checked against an independent reference written in this file.
//   literals and folded constants beyond 2^37 (moo-l3-ast-raw-literals): a decimal literal is read as the exact
//     rational and rounded once, ties to even (DESIGN.md "Literals"), and the folded constant is exact at every
//     magnitude (DESIGN.md "The compiler"), so operands beyond 2^37 (2^53 raw) fold and run to the oracle's bits;
//   the boundary (DESIGN.md "Boundary"): moo_fromdouble/moo_fromfloat/moo_tofloat/moo_todouble against an
//     independent exact-rational reference here (IEEE bits decoded in integer arithmetic, ties to even);
//   float bytes (DETERMINISM.md 18): buffer.writef32/f64 and string.pack 'f'/'d' store the boundary rule's bits;
//   the integer-argument rule (DESIGN.md "Exact integers"): a fraction is refused by name, an integer value works.
// Both parts are the doctest suite "oracle" (build/Moo.Test -ts=oracle); MOO_ORACLE_N sets the random
// counts, as the harness does (a small default here, under 1 s in Moo.Test).
extern "C"
{
    int64_t moo_oracle_add(int64_t a, int64_t b);
    int64_t moo_oracle_sub(int64_t a, int64_t b);
    int64_t moo_oracle_mul(int64_t a, int64_t b);
    int64_t moo_oracle_div(int64_t a, int64_t b);
    int64_t moo_oracle_idiv(int64_t a, int64_t b);
    int64_t moo_oracle_mod(int64_t a, int64_t b);
    int64_t moo_oracle_unm(int64_t a);
}


namespace
{

// xorshift64* with a fixed seed, so every run checks the same values
uint64_t rngState = 0x9e3779b97f4a7c15ull;
uint64_t rnd()
{
    uint64_t x = rngState;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rngState = x * 0x2545f4914f6cdd1dull;
    return rngState;
}

// the random count: MOO_ORACLE_N, or the small default
uint64_t oracleN()
{
    const char* text = getenv("MOO_ORACLE_N");
    if (!text || !*text)
        return uint64_t(1) << 14;
    char* end = nullptr;
    unsigned long long n = strtoull(text, &end, 10);
    REQUIRE_MESSAGE((*end == 0 && n > 0 && text[0] != '-'),
        "MOO_ORACLE_N must be a positive decimal count, not '" << text << "'");
    return uint64_t(n);
}

// the exact decimal literal of a raw value: r / 2^16 has at most 16 fraction digits (10^16 / 2^16 = 5^16), and
// the literal parser reads it exactly (DESIGN.md "Literals"). Parenthesized, so a negative literal is one operand.
std::string lit(int64_t r)
{
    uint64_t m = r < 0 ? uint64_t(0) - uint64_t(r) : uint64_t(r);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "(%s%llu.%016llu)", r < 0 ? "-" : "", (unsigned long long)(m >> 16),
        (unsigned long long)((m & 0xffff) * 152587890625ull));
    return buffer;
}

// the little-endian bytes of a 32-bit or 64-bit integer
std::string leBytes32(uint32_t bits)
{
    std::string out;
    for (int k = 0; k < 4; ++k)
        out.push_back(static_cast<char>(bits >> (8 * k)));
    return out;
}
std::string leBytes64(uint64_t bits)
{
    std::string out;
    for (int k = 0; k < 8; ++k)
        out.push_back(static_cast<char>(bits >> (8 * k)));
    return out;
}

// A VM for the oracle, reusing the harness's compile/load/call pattern (oracle.test.cpp).
struct MooOracleState
{
    lua_State* L;

    MooOracleState()
    {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    ~MooOracleState()
    {
        lua_close(L);
    }

    void load(const std::string& source)
    {
        std::string bytecode = Luau::compile(source);
        int status = luau_load(L, "=moo-oracle-conformance", bytecode.data(), bytecode.size(), 0);
        REQUIRE_MESSAGE(status == 0, "cannot load: " << (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?"));
    }

    // compile and run source, returning its first result as the raw Q48.16 value
    int64_t run(const std::string& source)
    {
        load(source);
        return callInt(0);
    }

    // compile and run source: its first result's raw value as a decimal, or "error: " and the message
    std::string runText(const std::string& source)
    {
        load(source);
        std::string result;
        if (lua_pcall(L, 0, 1, 0) != 0)
            result = std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
        else
            result = std::to_string(static_cast<long long>(lua_tonumber(L, -1)));
        lua_pop(L, 1);
        return result;
    }

    int64_t callInt(int nargs)
    {
        REQUIRE_MESSAGE(lua_pcall(L, nargs, 1, 0) == 0, "moo-oracle-conformance: pcall failed");
        int64_t v = int64_t(lua_tonumber(L, -1));
        lua_pop(L, 1);
        return v;
    }

    std::string callString(int nargs)
    {
        REQUIRE_MESSAGE(lua_pcall(L, nargs, 1, 0) == 0, "moo-oracle-conformance: pcall failed");
        size_t len = 0;
        const char* text = lua_tolstring(L, -1, &len);
        std::string result(text, len);
        lua_pop(L, 1);
        return result;
    }

    void pushNumber(int64_t v)
    {
        lua_pushnumber(L, v);
    }
};

// The nearest Q48.16 value to the double whose bits are given, ties to even, exact (DESIGN.md "Boundary"): the
// rational m * 2^(e-1075) in raw units is m * 2^(e-1059), rounded once to the nearest integer, ties to even.
int refFromDouble(double x, int64_t* out)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bool negative = (bits >> 63) != 0;
    int exponent = int((bits >> 52) & 0x7ff);
    uint64_t mantissa = bits & ((uint64_t(1) << 52) - 1);
    if (exponent == 0x7ff)
        return mantissa ? MOO_NUMBER_NAN : MOO_NUMBER_INFINITE;
    if (exponent == 0)
        exponent = 1;
    else
        mantissa |= uint64_t(1) << 52;
    int shift = exponent - 1059;
    uint64_t magnitude;
    if (mantissa == 0)
        magnitude = 0;
    else if (shift >= 0)
    {
        if (shift >= 63 || mantissa > (uint64_t(INT64_MAX) >> shift))
            return MOO_NUMBER_OUT_OF_RANGE;
        magnitude = mantissa << shift;
    }
    else if (-shift >= 64)
        magnitude = 0;
    else
    {
        int drop = -shift;
        magnitude = mantissa >> drop;
        uint64_t rest = mantissa & ((uint64_t(1) << drop) - 1);
        uint64_t half = uint64_t(1) << (drop - 1);
        if (rest > half || (rest == half && (magnitude & 1)))
            magnitude += 1;
    }
    if (magnitude > uint64_t(INT64_MAX))
        return MOO_NUMBER_OUT_OF_RANGE;
    *out = negative ? -int64_t(magnitude) : int64_t(magnitude);
    return MOO_NUMBER_OK;
}

// the same for a float: m * 2^(e-150) in number units is m * 2^(e-134) in raw units
int refFromFloat(float x, int64_t* out)
{
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bool negative = (bits >> 31) != 0;
    int exponent = int((bits >> 23) & 0xff);
    uint64_t mantissa = bits & 0x7fffff;
    if (exponent == 0xff)
        return mantissa ? MOO_NUMBER_NAN : MOO_NUMBER_INFINITE;
    if (exponent == 0)
        exponent = 1;
    else
        mantissa |= uint64_t(1) << 23;
    int shift = exponent - 134;
    uint64_t magnitude;
    if (mantissa == 0)
        magnitude = 0;
    else if (shift >= 0)
    {
        if (shift >= 63 || mantissa > (uint64_t(INT64_MAX) >> shift))
            return MOO_NUMBER_OUT_OF_RANGE;
        magnitude = mantissa << shift;
    }
    else if (-shift >= 64)
        magnitude = 0;
    else
    {
        int drop = -shift;
        magnitude = mantissa >> drop;
        uint64_t rest = mantissa & ((uint64_t(1) << drop) - 1);
        uint64_t half = uint64_t(1) << (drop - 1);
        if (rest > half || (rest == half && (magnitude & 1)))
            magnitude += 1;
    }
    if (magnitude > uint64_t(INT64_MAX))
        return MOO_NUMBER_OUT_OF_RANGE;
    *out = negative ? -int64_t(magnitude) : int64_t(magnitude);
    return MOO_NUMBER_OK;
}

// the IEEE-754 binary32 bits nearest raw / 2^16, ties to even (DESIGN.md "Boundary"), from first principles
uint32_t refToFloatBits(int64_t raw)
{
    if (raw == 0)
        return 0;
    uint32_t sign = raw < 0 ? 0x80000000u : 0u;
    uint64_t m = raw < 0 ? uint64_t(0) - uint64_t(raw) : uint64_t(raw);
    int b = 0;
    for (uint64_t x = m; x; x >>= 1)
        b += 1;
    int E = b - 17;
    uint64_t M24;
    if (b <= 24)
        M24 = m << (24 - b);
    else
    {
        int shift = b - 24;
        M24 = m >> shift;
        uint64_t rest = m & ((uint64_t(1) << shift) - 1);
        uint64_t half = uint64_t(1) << (shift - 1);
        if (rest > half || (rest == half && (M24 & 1)))
            M24 += 1;
    }
    if (M24 == (uint64_t(1) << 24))
    {
        M24 = uint64_t(1) << 23;
        E += 1;
    }
    return sign | uint32_t(E + 127) << 23 | uint32_t(M24 - (uint64_t(1) << 23));
}

// the IEEE-754 binary64 bits nearest raw / 2^16, ties to even, from first principles
uint64_t refToDoubleBits(int64_t raw)
{
    if (raw == 0)
        return 0;
    uint64_t sign = raw < 0 ? 0x8000000000000000ull : 0ull;
    uint64_t m = raw < 0 ? uint64_t(0) - uint64_t(raw) : uint64_t(raw);
    int b = 0;
    for (uint64_t x = m; x; x >>= 1)
        b += 1;
    int E = b - 17;
    uint64_t M53;
    if (b <= 53)
        M53 = m << (53 - b);
    else
    {
        int shift = b - 53;
        M53 = m >> shift;
        uint64_t rest = m & ((uint64_t(1) << shift) - 1);
        uint64_t half = uint64_t(1) << (shift - 1);
        if (rest > half || (rest == half && (M53 & 1)))
            M53 += 1;
    }
    if (M53 == (uint64_t(1) << 53))
    {
        M53 = uint64_t(1) << 52;
        E += 1;
    }
    return sign | uint64_t(E + 1023) << 52 | (M53 - (uint64_t(1) << 52));
}

} // namespace

// DESIGN.md "Literals" and "The compiler": a literal is the exact rational rounded once, ties to even, and the
// folded constant is the exact raw value, so operands up to 2^53 raw (carried exactly by the temporary bridge) fold
// and run to the oracle's bits even when the result crosses 2^37 (2^53 raw, the folded constant beyond the double's
// exactness line; moo-l3-compiler-raw-constants). The literal-exact boundary is 2^53 raw (2^37 number; DESIGN.md
// "Layer 3 as built", "until the bytecode version PR ... exact below 2^37"). A pair that saturates gives the oracle's
// saturated bits in release and the named error in a checked build, on both paths (DESIGN.md "Overflow").
TEST_CASE("OracleLiteralsBeyondTwoPowThirtySeven")
{
    struct Op
    {
        const char* sym;
        int64_t (*oracle)(int64_t, int64_t);
        bool divKind;
        bool (*saturates)(int64_t, int64_t);
    };
    const Op ops[] = {
        {"+", moo_oracle_add, false, moo_numaddsaturates},
        {"-", moo_oracle_sub, false, moo_numsubsaturates},
        {"*", moo_oracle_mul, false, moo_nummulsaturates},
        {"/", moo_oracle_div, true, moo_numdivsaturates},
        {"//", moo_oracle_idiv, true, moo_numidivsaturates},
        {"%", moo_oracle_mod, true, nullptr},
    };

    // the oracle's raw result as a decimal, or the error a checked build raises where release saturates
    auto expected = [](const Op& op, int64_t a, int64_t b) {
        if (MOO_OVERFLOW_ERRORS && op.saturates && op.saturates(a, b))
            return std::string("error: moo-oracle-conformance:1: number overflow in '") + op.sym + "'";
        return std::to_string(static_cast<long long>(op.oracle(a, b)));
    };

    // the literal and fold paths compile source, so the compiled count is capped independent of MOO_ORACLE_N
    uint64_t n = oracleN();
    if (n > 2048)
        n = 2048;

    MooOracleState s;
    for (uint64_t i = 0; i < n; ++i)
    {
        // operands within [-2^53, 2^53] raw: literals travel exactly, so fold and runtime equal fixed's C
        int64_t a = int64_t(rnd() % (uint64_t(1) << 53));
        if (rnd() & 1)
            a = -a;
        int64_t b = int64_t(rnd() % (uint64_t(1) << 53));
        if (rnd() & 1)
            b = -b;
        for (const Op& op : ops)
        {
            if (op.divKind && b == 0)
                continue;
            std::string want = expected(op, a, b);
            INFO("op=" << op.sym << " a=" << a << " b=" << b);
            std::string la = lit(a), lb = lit(b);
            CHECK(s.runText("return " + la + " " + op.sym + " " + lb) == want);
            CHECK(s.runText(std::string("local f=function(x,y)return x ") + op.sym + " y end return f(" + la + "," + lb + ")") == want);
        }
    }

    // folded constants beyond 2^37 (results past 2^53 raw) equal the oracle on both the fold and runtime paths
    const int64_t big[][2] = {
        {int64_t(1) << 52, int64_t(1) << 52},
        {(int64_t(1) << 53) - 1, 1},
        {(int64_t(1) << 53) - 1, (int64_t(1) << 53) - 1},
        {int64_t(1) << 53, 2},
    };
    for (const auto& p : big)
    {
        for (const Op& op : ops)
        {
            if (op.divKind && p[1] == 0)
                continue;
            std::string want = expected(op, p[0], p[1]);
            std::string la = lit(p[0]), lb = lit(p[1]);
            CHECK(s.runText("return " + la + " " + op.sym + " " + lb) == want);
            CHECK(s.runText(std::string("local f=function(x,y)return x ") + op.sym + " y end return f(" + la + "," + lb + ")") == want);
        }
    }

    // the literal-exact boundary: 2^53 raw (2^37 number) and one below it round-trip exactly
    int64_t edge = int64_t(1) << 53;
    CHECK(s.run("return " + lit(edge)) == edge);
    CHECK(s.run("return " + lit(-edge)) == -edge);
    CHECK(s.run("return " + lit(edge - 1)) == edge - 1);
    CHECK(s.run("return " + lit(-(edge - 1))) == -(edge - 1));
}

// DESIGN.md "Boundary": the C API boundary functions against the independent exact-rational reference above.
// moo_fromdouble/moo_fromfloat decode the IEEE bits and round once, ties to even, returning the status first; the
// moo_todouble/moo_tofloat encode to the nearest bit pattern, ties to even.
TEST_CASE("OracleBoundaryFunctions")
{
    uint64_t n = oracleN();

    // encode: moo_todouble / moo_tofloat bits equal the reference
    for (uint64_t i = 0; i < n; ++i)
    {
        int64_t raw = int64_t(rnd());
        double d = moo_todouble(raw);
        uint64_t dbits;
        memcpy(&dbits, &d, sizeof(dbits));
        CHECK(dbits == refToDoubleBits(raw));
        float f = moo_tofloat(raw);
        uint32_t fbits;
        memcpy(&fbits, &f, sizeof(fbits));
        CHECK(fbits == refToFloatBits(raw));
    }

    // decode: moo_fromdouble over random doubles, including subnormals, NaN, infinity and out of range
    for (uint64_t i = 0; i < n; ++i)
    {
        uint64_t dbits = rnd();
        double d;
        memcpy(&d, &dbits, sizeof(d));
        int64_t out = 0, ref = 0;
        int status = moo_fromdouble(d, &out);
        int rstatus = refFromDouble(d, &ref);
        CHECK(status == rstatus);
        if (status == MOO_NUMBER_OK)
            CHECK(out == ref);
    }

    // decode: moo_fromfloat over random floats
    for (uint64_t i = 0; i < n; ++i)
    {
        uint32_t fbits = uint32_t(rnd());
        float f;
        memcpy(&f, &fbits, sizeof(f));
        int64_t out = 0, ref = 0;
        int status = moo_fromfloat(f, &out);
        int rstatus = refFromFloat(f, &ref);
        CHECK(status == rstatus);
        if (status == MOO_NUMBER_OK)
            CHECK(out == ref);
    }

    // the round trip is self-consistent: todouble produces a finite double, which fromdouble reads back per the ref
    for (uint64_t i = 0; i < n; ++i)
    {
        int64_t raw = int64_t(rnd());
        uint64_t bits = refToDoubleBits(raw);
        double d;
        memcpy(&d, &bits, sizeof(d));
        int64_t out = 0, ref = 0;
        REQUIRE(moo_fromdouble(d, &out) == MOO_NUMBER_OK);
        REQUIRE(refFromDouble(d, &ref) == MOO_NUMBER_OK);
        CHECK(out == ref);
    }
}

// DETERMINISM.md 18: buffer.writef32/f64 and string.pack 'f'/'d' store the boundary rule's bits, and read/unpack
// decode them back by the same rule, so every byte equals the independent reference.
TEST_CASE("OracleBufferAndPackFloatBytes")
{
    uint64_t n = oracleN();
    if (n > (uint64_t(1) << 18))
        n = uint64_t(1) << 18; // the buffer and pack paths run in the VM; cap independent of MOO_ORACLE_N

    MooOracleState s;
    // one closure dispatches by kind so it is compiled once: 1 pack '<f', 2 writef32 bytes, 3 readf32 back,
    // 4 unpack '<f', 5 pack '<d', 6 writef64 bytes, 7 readf64 back, 8 unpack '<d'
    s.load("__moo_buffer_dispatch = function(kind,v) local b=buffer.create(8) "
           "if kind==1 then return string.pack('<f',v) end "
           "if kind==2 then buffer.writef32(b,0,v) local t='' for i=0,3 do t=t..string.char(buffer.readu8(b,i)) end return t end "
           "if kind==3 then buffer.writef32(b,0,v) return buffer.readf32(b,0) end "
           "if kind==4 then return string.unpack('<f',string.pack('<f',v)) end "
           "if kind==5 then return string.pack('<d',v) end "
           "if kind==6 then buffer.writef64(b,0,v) local t='' for i=0,7 do t=t..string.char(buffer.readu8(b,i)) end return t end "
           "if kind==7 then buffer.writef64(b,0,v) return buffer.readf64(b,0) end "
           "return string.unpack('<d',string.pack('<d',v)) end");
    REQUIRE(lua_pcall(s.L, 0, 0, 0) == 0); // run the assignment: __moo_buffer_dispatch is the dispatch function

    auto callBytes = [&](int kind, int64_t v) {
        lua_getglobal(s.L, "__moo_buffer_dispatch");
        lua_pushinteger(s.L, kind);
        lua_pushnumber(s.L, v);
        return s.callString(2);
    };
    auto callNum = [&](int kind, int64_t v) {
        lua_getglobal(s.L, "__moo_buffer_dispatch");
        lua_pushinteger(s.L, kind);
        lua_pushnumber(s.L, v);
        return s.callInt(2);
    };

    for (uint64_t i = 0; i < n; ++i)
    {
        int64_t raw = int64_t(rnd());

        // f
        uint32_t fbits = refToFloatBits(raw);
        CHECK(callBytes(1, raw) == leBytes32(fbits)); // string.pack '<f'
        CHECK(callBytes(2, raw) == leBytes32(fbits)); // buffer.writef32
        float f;
        uint32_t fbitsCopy = fbits;
        memcpy(&f, &fbitsCopy, sizeof(f));
        int64_t fref = 0;
        REQUIRE(refFromFloat(f, &fref) == MOO_NUMBER_OK);
        CHECK(callNum(3, raw) == fref); // buffer.readf32
        CHECK(callNum(4, raw) == fref); // string.unpack '<f'

        // d
        uint64_t dbits = refToDoubleBits(raw);
        CHECK(callBytes(5, raw) == leBytes64(dbits)); // string.pack '<d'
        CHECK(callBytes(6, raw) == leBytes64(dbits)); // buffer.writef64
        double d;
        uint64_t dbitsCopy = dbits;
        memcpy(&d, &dbitsCopy, sizeof(d));
        int64_t dref = 0;
        REQUIRE(refFromDouble(d, &dref) == MOO_NUMBER_OK);
        CHECK(callNum(7, raw) == dref); // buffer.readf64
        CHECK(callNum(8, raw) == dref); // string.unpack '<d'
    }
}

// DESIGN.md "Exact integers": a function that needs an integer accepts only an integer-valued number and raises
// the named error for a fraction; the conformance sweep checks both over the documented surfaces.
TEST_CASE("OracleIntegerArgumentRule")
{
    MooOracleState s;
    const char* tmpls[] = {
        "string.byte('abc', <v>)",
        "string.sub('abc', <v>)",
        "string.char(<v>)",
        "table.insert({10}, <v>, 5)",
        "buffer.readu8(buffer.create(8), <v>)",
        "string.format('%d', <v>)",
        "string.pack('i4', <v>)",
        "bit32.band(<v>)",
    };

    auto run = [&](const char* tmpl, const std::string& v) {
        std::string src = tmpl;
        size_t at = src.find("<v>");
        src.replace(at, 3, v);
        std::string full =
            "local byte,sub,chr,insert,readu8,format,pack,band=string.byte,string.sub,string.char,table.insert,buffer.readu8,string.format,string.pack,bit32.band "
            "local ok,err=pcall(function() return " + src + " end) return tostring(ok)..'|'..tostring(err)";
        return mooRun(s.L, full.c_str());
    };

    for (const char* tmpl : tmpls)
    {
        // a fraction is refused by name
        std::string result = run(tmpl, "2.5");
        CHECK_MESSAGE(result.rfind("false|", 0) == 0, tmpl << ": " << result);
        CHECK_MESSAGE(result.find("number has no integer representation") != std::string::npos, tmpl << ": " << result);
        // an integer-valued number works
        CHECK_MESSAGE(run(tmpl, "2.0").rfind("true|", 0) == 0, tmpl << ": 2.0 refused");
    }
}

TEST_SUITE_END();
