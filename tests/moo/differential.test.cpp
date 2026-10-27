// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Differential checks on fixed inputs: the operators, parse and format round trips against the oracle,
// with a negative control (the libFuzzer targets in fuzz/ run the same checks; docs/DESIGN.md "Layers").

#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "moo_parse.h"
#include "moo_format.h"
#include "MooHost.h"
#include "doctest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

TEST_SUITE_BEGIN("differential");

// The layer 5 differential fuzz target's checks, in Moo.Test (docs/DESIGN.md "Layers", layer 5: "bit-exact agreement
// with fixed's C; fuzzing"; docs/history/layer5/moo-l5-fuzz-differential.md). The libFuzzer runs are functional and never run
// here: these cases pin the same comparison, round trip and negative control on a fixed input set, so a regression in
// the target's logic is caught by the push gate.
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

// the two sides of the differential target (fuzz/moo_arith.cpp), over the domain [MOO_NUMBER_MIN, MOO_NUMBER_MAX]
typedef int64_t (*MooFuzzOp)(int64_t a, int64_t b);

struct MooFuzzCase
{
    const char* name;
    MooFuzzOp moo;
    MooFuzzOp oracle;
};

int64_t mooFuzzUnm(int64_t a, int64_t)
{
    return moo_numunm(a);
}

int64_t mooFuzzOracleUnm(int64_t a, int64_t)
{
    return moo_oracle_unm(a);
}

// docs/DESIGN.md "Semantics", the operator table: one row per moo operator
const MooFuzzCase kMooFuzzOps[] = {
    {"+", [](int64_t a, int64_t b) -> int64_t { return moo_numadd(a, b); }, moo_oracle_add},
    {"-", [](int64_t a, int64_t b) -> int64_t { return moo_numsub(a, b); }, moo_oracle_sub},
    {"*", [](int64_t a, int64_t b) -> int64_t { return moo_nummul(a, b); }, moo_oracle_mul},
    {"/", [](int64_t a, int64_t b) -> int64_t { return moo_numdiv(a, b); }, moo_oracle_div},
    {"//", [](int64_t a, int64_t b) -> int64_t { return moo_numidiv(a, b); }, moo_oracle_idiv},
    {"%", [](int64_t a, int64_t b) -> int64_t { return moo_nummod(a, b); }, moo_oracle_mod},
    {"unary -", mooFuzzUnm, mooFuzzOracleUnm},
    {"<", [](int64_t a, int64_t b) -> int64_t { return a < b; }, moo_oracle_lt},
    {"<=", [](int64_t a, int64_t b) -> int64_t { return a <= b; }, moo_oracle_le},
    {">", [](int64_t a, int64_t b) -> int64_t { return a > b; }, moo_oracle_gt},
    {">=", [](int64_t a, int64_t b) -> int64_t { return a >= b; }, moo_oracle_ge},
    {"==", [](int64_t a, int64_t b) -> int64_t { return a == b; }, moo_oracle_eq},
    {"~=", [](int64_t a, int64_t b) -> int64_t { return a != b; }, moo_oracle_ne},
};

// the target's seeded operand stream: PCG32, fixed everywhere so a failure reproduces
struct MooFuzzPcg32
{
    uint64_t state = 0x853c49e6748fea9bull;
    uint64_t inc = 0xda3e39cb94b95bdbull;

    uint32_t next()
    {
        uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        uint32_t xorshifted = uint32_t(((old >> 18) ^ old) >> 27);
        uint32_t rot = uint32_t(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
    }

    int64_t operand()
    {
        uint64_t bits = (uint64_t(next()) << 32) | next();
        int64_t value;
        memcpy(&value, &bits, sizeof(value));
        if (value < MOO_NUMBER_MIN || value > MOO_NUMBER_MAX) // the domain excludes INT64_MIN (docs/DESIGN.md "Representation")
            value = value < 0 ? MOO_NUMBER_MIN : MOO_NUMBER_MAX;
        return value;
    }
};

// the edge operands the target reaches through its clamp, crossed with itself
void mooFuzzEdgePairs(std::vector<int64_t>& as, std::vector<int64_t>& bs)
{
    const int64_t edges[] = {0, 1, -1, MOO_NUMBER_ONE, -MOO_NUMBER_ONE, MOO_NUMBER_ONE / 2, 0xffff, -0xffff,
        (int64_t(1) << 47) - 1, int64_t(1) << 47, MOO_NUMBER_INT_MAX << 16, MOO_NUMBER_MAX, MOO_NUMBER_MIN};
    for (int64_t a : edges)
        for (int64_t b : edges)
            as.push_back(a), bs.push_back(b);
}

} // namespace


// DESIGN.md "Layers", layer 5: the fuzz target's differential check, every operator, moo_number.h against the oracle's
// fixed C over the edges and a seeded random stream; divides by zero are skipped because the VM raises before them.
TEST_CASE("DifferentialOperators")
{
    std::vector<int64_t> as, bs;
    mooFuzzEdgePairs(as, bs);
    MooFuzzPcg32 rng;
    for (int i = 0; i < 20000; ++i)
        as.push_back(rng.operand()), bs.push_back(rng.operand());

    for (const MooFuzzCase& op : kMooFuzzOps)
        for (size_t i = 0; i < as.size(); ++i)
        {
            if ((strcmp(op.name, "/") == 0 || strcmp(op.name, "//") == 0 || strcmp(op.name, "%") == 0) && bs[i] == 0)
                continue;
            int64_t got = op.moo(as[i], bs[i]);
            int64_t want = op.oracle(as[i], bs[i]);
            CHECK_MESSAGE(got == want, op.name << " a=" << as[i] << " b=" << bs[i] << " moo=" << got << " oracle=" << want);
        }
}

// moo's tonumber strips one sign before moo_numparse (VM/src/lobject.cpp luaO_str2d); the formatter writes the sign
static int mooFuzzParseSigned(const char* text, fixed_t* out)
{
    bool negative = false;
    if (*text == '-' || *text == '+')
        negative = *text++ == '-';
    int status = moo_numparse(text, out);
    if (status == MOO_PARSE_OK && negative)
        *out = -*out;
    return status;
}

// DESIGN.md "Number formatting": tostring is the shortest decimal that reads back to the same raw value, and %.16f is
// the exact expansion, which the parser reads back exactly.
TEST_CASE("DifferentialParseRoundtrip")
{
    std::vector<int64_t> values = {0, 1, -1, MOO_NUMBER_MAX, MOO_NUMBER_MIN, MOO_NUMBER_ONE, 6554, -6554, 98304, 21845};
    MooFuzzPcg32 rng;
    for (int i = 0; i < 20000; ++i)
        values.push_back(rng.operand());

    for (int64_t value : values)
    {
        char buf[64];
        char* end = moo_numtostr(buf, value);
        REQUIRE(end > buf);
        REQUIRE(end < buf + sizeof(buf));
        *end = 0;

        fixed_t back = 0;
        REQUIRE_MESSAGE(mooFuzzParseSigned(buf, &back) == MOO_PARSE_OK, value);
        CHECK_MESSAGE(back == value, value << " printed as " << buf);

        REQUIRE(moo_numformat(buf, sizeof(buf), ".16", 'f', value) >= 0);
        fixed_t exact = 0;
        REQUIRE_MESSAGE(mooFuzzParseSigned(buf, &exact) == MOO_PARSE_OK, value << " printed as " << buf);
        CHECK_MESSAGE(exact == value, value << " printed as " << buf);
    }
}

// The target must go red on a one-raw-unit difference: a planted error in `*` is caught by the same comparison the
// fuzz target aborts on.
TEST_CASE("DifferentialNegativeControl")
{
    int mismatches = 0;
    MooFuzzPcg32 rng;
    for (int i = 0; i < 20000; ++i)
    {
        int64_t a = rng.operand();
        int64_t b = rng.operand();
        int64_t real = moo_nummul(a, b);
        int64_t planted = real < MOO_NUMBER_MAX ? real + 1 : real - 1;
        if (planted != moo_oracle_mul(a, b))
            mismatches++;
    }
    CHECK(mismatches == 20000);
}

// The new expected-behaviour script pins the same rules from inside the VM; MooSpecScripts also runs it, and this case
// names it beside the C++ cases above.
TEST_CASE("DifferentialScript")
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK_EQ(mooRunFile(L, "spec/differential.luau"), "OK");
    lua_close(L);
}

TEST_SUITE_END();
