// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The layer 3 oracle (docs/DESIGN.md "Layers", layer 3: "bit-exact agreement with fixed's C on every operator over
// random and edge operands"; docs/layer3/moo-l3-oracle-harness.md). Every number operator is computed by the oracle,
// fixed's own C in tests/moo/moo_oracle_fixed.c, and by moo four ways, which must all equal it bit for bit:
//   direct  moo_number.h's function;
//   vm      the VM, on operands passed in through the C API (register operands, so nothing folds);
//   fold    the compiler, on the same expression with both operands as literals (constant folding);
//   k       the VM with one operand a literal constant (the K-operand opcodes: ADDK, SUBRK, DIVRK and the rest).
// For / // % by zero the oracle's precondition excludes the operation, and moo must raise `division by zero` on
// every path that runs it. MOO_ORACLE_N sets the number of random pairs (default 2^16; the nightly raises it).
#include "lua.h"
#include "lualib.h"

#include "moo_number.h"

#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"

#include "doctest.h"
#include "MooHost.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

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

// the edge operands (docs/layer3/moo-l3-oracle-harness.md, "Operands"), each with its negation
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
            out.state = message.find("division by zero") != std::string::npos ? MooOracleOutcome::DivisionByZero : MooOracleOutcome::Wrong;
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

        bool agree = true;
        for (int p = 0; p < MooPathCount; ++p)
        {
            const MooOracleOutcome& got = paths[p];
            if (got.state == MooOracleOutcome::NotRun)
                continue;
            pathRuns[p] += 1;
            if (got.state != expected.state || (got.state == MooOracleOutcome::Value && got.value != expected.value))
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

    // a batch of pairs of one operator, all paths
    void checkBatch(const MooOracleOp& op, const std::vector<int64_t>& as, const std::vector<int64_t>& bs)
    {
        std::vector<int64_t> a, b;
        for (size_t i = 0; i < as.size() && !stopped(); ++i)
        {
            if (op.kind == MooOracleDivide && bs[i] == 0)
                checkZero(op, as[i]);
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

TEST_SUITE_BEGIN("moo-oracle");

// DESIGN.md "Layers", layer 3: every operator, bit-exact against fixed's C, over the edge set crossed with itself and
// MOO_ORACLE_N seeded random pairs, on moo_number.h, the VM, the compiler's folding and the K-operand opcodes.
TEST_CASE("MooOracleEveryOperator")
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
TEST_CASE("MooOracleNegativeControlCatchesOneUnit")
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
TEST_CASE("MooOracleNegativeControlCoversEveryPath")
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
TEST_CASE("MooOracleLiteralPathsFoldAndReachKOpcodes")
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
TEST_CASE("MooOracleLiteralsAndGenerator")
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

TEST_SUITE_END();
