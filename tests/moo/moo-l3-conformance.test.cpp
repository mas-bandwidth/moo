// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The layer 3 closing gate, piece 1: the oracle (moo-l3-oracle-harness.test.cpp) extended to the layer 3
// surfaces that landed since. The oracle is fixed's own C (tests/moo/moo_oracle_fixed.c), reused here by name;
// each surface below is checked against an independent reference written in this file.
//   literals and folded constants beyond 2^37 (moo-l3-ast-raw-literals): a decimal literal is read as the exact
//     rational and rounded once, ties to even (DESIGN.md "Literals"), and the folded constant is exact at every
//     magnitude (DESIGN.md "The compiler"), so operands beyond 2^37 (2^53 raw) fold and run to the oracle's bits;
//   the boundary (DESIGN.md "Boundary"): moo_fromdouble/moo_fromfloat/moo_tofloat/moo_todouble against an
//     independent exact-rational reference here (IEEE bits decoded in integer arithmetic, ties to even);
//   float bytes (DETERMINISM.md 18): buffer.writef32/f64 and string.pack 'f'/'d' store the boundary rule's bits;
//   the integer-argument rule (DESIGN.md "Exact integers"): a fraction is refused by name, an integer value works.
// The suite names start "moo-oracle" so the nightly's -ts=moo-oracle* runs them; MOO_ORACLE_N sets the random
// counts, as the harness does (a small default here, under 1 s in Moo.Test).
#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include "moo_number.h"
#include "moo.h"

#include "Luau/Compiler.h"

#include "doctest.h"
#include "MooHost.h"

#include <cstdio>
#include <cstring>
#include <string>

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

TEST_SUITE_BEGIN("moo-oracle-conformance");

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

// A VM for the oracle, reusing the harness's compile/load/call pattern (moo-l3-oracle-harness.test.cpp).
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
// "Layer 3 as built", "until the bytecode version PR ... exact below 2^37").
TEST_CASE("MooOracleLiteralsBeyondTwoPowThirtySeven")
{
    struct Op
    {
        const char* sym;
        int64_t (*oracle)(int64_t, int64_t);
        bool divKind;
    };
    const Op ops[] = {
        {"+", moo_oracle_add, false},
        {"-", moo_oracle_sub, false},
        {"*", moo_oracle_mul, false},
        {"/", moo_oracle_div, true},
        {"//", moo_oracle_idiv, true},
        {"%", moo_oracle_mod, true},
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
            int64_t want = op.oracle(a, b);
            INFO("op=" << op.sym << " a=" << a << " b=" << b);
            std::string la = lit(a), lb = lit(b);
            int64_t fold = s.run("return " + la + " " + op.sym + " " + lb);
            int64_t rt = s.run(std::string("local f=function(x,y)return x ") + op.sym + " y end return f(" + la + "," + lb + ")");
            CHECK(fold == want);
            CHECK(rt == want);
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
            int64_t want = op.oracle(p[0], p[1]);
            std::string la = lit(p[0]), lb = lit(p[1]);
            CHECK(s.run("return " + la + " " + op.sym + " " + lb) == want);
            CHECK(s.run(std::string("local f=function(x,y)return x ") + op.sym + " y end return f(" + la + "," + lb + ")") == want);
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
TEST_CASE("MooOracleBoundaryFunctions")
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
TEST_CASE("MooOracleBufferAndPackFloatBytes")
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
TEST_CASE("MooOracleIntegerArgumentRule")
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
