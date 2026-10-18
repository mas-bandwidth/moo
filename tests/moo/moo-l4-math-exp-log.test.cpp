// This file is part of moo, a fork of the Luau programming language; moo changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4 exp/log family (docs/DESIGN.md, "math library"): fixed composition through fixExp2/fixLog2.
// Each library function and its bytecode fastcall must return the same Q48.16 bits; edge cases and
// 4096 seeded inputs also pin the documented scaling, saturation, and named log-domain errors.
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"
#include "moo_number.h"

#include "fixed/fixed_math.h"

#include "MooHost.h"

#include "doctest.h"

#include <stdint.h>
#include <stdio.h>

#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

TEST_SUITE_BEGIN("moo-l4-math-exp-log");

namespace
{

constexpr fixed_t kLog2E = 94548;          // nearest Q48.16: (1 / ln(2)) * 65536 = 94548.4621996991...
constexpr fixed_t kInvLog2Ten = 19728;     // nearest Q48.16: (ln(2) / ln(10)) * 65536 = 19728.3017958347...
constexpr fixed_t kRawRange = 64 * MOO_NUMBER_ONE;

struct Lcg
{
    uint64_t state = 0x6d6f6f5f6c345f65ull;
    uint64_t next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state ^ (state >> 29);
    }
};

int compileFuncRef(lua_State* L, const std::string& source)
{
    Luau::CompileOptions options;
    options.optimizationLevel = 2;
    std::string bytecode = Luau::compile(source, options);
    REQUIRE(luau_load(L, "=math-exp-log", bytecode.data(), bytecode.size(), 0) == 0);
    REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    return ref;
}

int64_t call1(lua_State* L, int ref, int64_t a)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    int status = lua_pcall(L, 1, 1, 0);
    REQUIRE_MESSAGE(status == 0, "math call failed: " << lua_tostring(L, -1));
    int64_t result = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return result;
}

int64_t call2(lua_State* L, int ref, int64_t a, int64_t b)
{
    lua_getref(L, ref);
    lua_pushnumber(L, a);
    lua_pushnumber(L, b);
    int status = lua_pcall(L, 2, 1, 0);
    REQUIRE_MESSAGE(status == 0, "math call failed: " << lua_tostring(L, -1));
    int64_t result = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return result;
}

fixed_t expRef(fixed_t x)
{
    return fixExp2(fixMul(x, kLog2E));
}

// Divide the exponent by two before fixExp2, keeping each intermediate representable through sinh/cosh saturation.
fixed_t halfExpRef(fixed_t x)
{
    return fixExp2(moo_numsub(fixMul(x, kLog2E), MOO_NUMBER_ONE));
}

fixed_t sinhRef(fixed_t x)
{
    return moo_numsub(halfExpRef(x), halfExpRef(moo_numunm(x)));
}

fixed_t coshRef(fixed_t x)
{
    return moo_numadd(halfExpRef(x), halfExpRef(moo_numunm(x)));
}

fixed_t tanhRef(fixed_t x)
{
    fixed_t magnitude = x < 0 ? moo_numunm(x) : x;
    fixed_t twice = moo_numadd(magnitude, magnitude);
    fixed_t inverse = expRef(moo_numunm(twice));
    fixed_t result = moo_numdiv(moo_numsub(MOO_NUMBER_ONE, inverse), moo_numadd(MOO_NUMBER_ONE, inverse));
    return x < 0 ? moo_numunm(result) : result;
}

fixed_t logRef(fixed_t x)
{
    return moo_numdiv(fixLog2(x), kLog2E);
}

fixed_t log10Ref(fixed_t x)
{
    return fixMul(fixLog2(x), kInvLog2Ten);
}

fixed_t logBaseRef(fixed_t x, fixed_t base)
{
    if (base == 2 * MOO_NUMBER_ONE)
        return fixLog2(x);
    if (base == 10 * MOO_NUMBER_ONE)
        return log10Ref(x);
    return moo_numdiv(fixLog2(x), fixLog2(base));
}

std::vector<int64_t> signedInputs()
{
    std::vector<int64_t> values = {
        MOO_NUMBER_MIN, MOO_NUMBER_MAX, -40 * MOO_NUMBER_ONE, 40 * MOO_NUMBER_ONE,
        -10 * MOO_NUMBER_ONE, 10 * MOO_NUMBER_ONE, -MOO_NUMBER_ONE, MOO_NUMBER_ONE,
        -1, 0, 1,
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t raw = int64_t(rng.next() % uint64_t(2 * kRawRange + 1)) - kRawRange;
        values.push_back(raw);
    }
    return values;
}

std::vector<int64_t> positiveInputs()
{
    std::vector<int64_t> values = {1, MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE, 10 * MOO_NUMBER_ONE, MOO_NUMBER_MAX};
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
        values.push_back(1 + int64_t(rng.next() % uint64_t(kRawRange)));
    return values;
}

std::vector<std::pair<int64_t, int64_t>> positivePairs()
{
    std::vector<std::pair<int64_t, int64_t>> values = {
        {MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE},
        {8 * MOO_NUMBER_ONE, 2 * MOO_NUMBER_ONE},
        {100 * MOO_NUMBER_ONE, 10 * MOO_NUMBER_ONE},
        {MOO_NUMBER_MAX, MOO_NUMBER_ONE + 1},
    };
    Lcg rng;
    for (int i = 0; i < 4096; ++i)
    {
        int64_t x = 1 + int64_t(rng.next() % uint64_t(kRawRange));
        int64_t base = 2 * MOO_NUMBER_ONE + int64_t(rng.next() % uint64_t(kRawRange));
        values.push_back({x, base});
    }
    return values;
}

template<typename Ref>
void checkUnary(lua_State* L, const char* name, Ref reference, const std::vector<int64_t>& values)
{
    int library = compileFuncRef(L, "local f = math." + std::string(name) + "; return function(x) return f(x) end");
    int fastcall = compileFuncRef(L, "return function(x) return math." + std::string(name) + "(x) end");
    unsigned mismatches = 0;
    std::string first;
    for (int64_t x : values)
    {
        int64_t expected = reference(x);
        int64_t lib = call1(L, library, x);
        int64_t fc = call1(L, fastcall, x);
        if (lib != expected || fc != expected)
        {
            if (mismatches++ == 0)
                first = " x=" + std::to_string(x) + " expected=" + std::to_string(expected) +
                    " library=" + std::to_string(lib) + " fastcall=" + std::to_string(fc);
        }
    }
    CHECK_MESSAGE(mismatches == 0, name << ": " << mismatches << " mismatches;" << first);
    lua_unref(L, library);
    lua_unref(L, fastcall);
}

void checkLogBase(lua_State* L)
{
    int library = compileFuncRef(L, "local f = math.log; return function(x, b) return f(x, b) end");
    int fastcall = compileFuncRef(L, "return function(x, b) return math.log(x, b) end");
    unsigned mismatches = 0;
    std::string first;
    for (const auto& value : positivePairs())
    {
        int64_t expected = logBaseRef(value.first, value.second);
        int64_t lib = call2(L, library, value.first, value.second);
        int64_t fc = call2(L, fastcall, value.first, value.second);
        if (lib != expected || fc != expected)
        {
            if (mismatches++ == 0)
                first = " x=" + std::to_string(value.first) + " base=" + std::to_string(value.second) +
                    " expected=" + std::to_string(expected) + " library=" + std::to_string(lib) +
                    " fastcall=" + std::to_string(fc);
        }
    }
    CHECK_MESSAGE(mismatches == 0, "log(x, base): " << mismatches << " mismatches;" << first);
    lua_unref(L, library);
    lua_unref(L, fastcall);
}

struct OracleRecord
{
    int64_t a = 0;
    int64_t b = 0;
    int64_t ideal = 0;
};

uint64_t fnv1a(const std::vector<unsigned char>& bytes, size_t length)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool readOracleVarint(const std::vector<unsigned char>& bytes, size_t& pos, size_t limit, int64_t& value)
{
    uint64_t encoded = 0;
    for (unsigned shift = 0; shift < 70; shift += 7)
    {
        if (pos >= limit)
            return false;
        unsigned char byte = bytes[pos++];
        if (shift == 63 && byte > 1)
            return false;
        encoded |= uint64_t(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0)
        {
            value = int64_t((encoded >> 1) ^ (uint64_t(0) - (encoded & 1)));
            return true;
        }
    }
    return false;
}

bool readOracleU32(const std::vector<unsigned char>& bytes, size_t& pos, size_t limit, uint32_t& value)
{
    if (limit - pos < 4)
        return false;
    value = 0;
    for (int i = 0; i < 4; ++i)
        value |= uint32_t(bytes[pos++]) << (8 * i);
    return true;
}

bool readOracleU64(const std::vector<unsigned char>& bytes, size_t& pos, size_t limit, uint64_t& value)
{
    if (limit - pos < 8)
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value |= uint64_t(bytes[pos++]) << (8 * i);
    return true;
}

std::string oraclePath()
{
#ifdef LUAU_CONFORMANCE_SOURCE_DIR
    return std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/../moo/vectors/math-oracle.bin";
#else
    return "tests/moo/vectors/math-oracle.bin";
#endif
}

bool loadOracle(std::unordered_map<std::string, std::vector<OracleRecord>>& records, std::string& error)
{
    std::ifstream input(oraclePath(), std::ios::binary);
    if (!input)
    {
        error = "cannot open " + oraclePath();
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() < 28 || std::string(bytes.begin(), bytes.begin() + 8) != "MOOMATH1")
    {
        error = "short file or bad magic";
        return false;
    }
    size_t end = bytes.size() - 8;
    size_t checksumPos = end;
    uint64_t checksum = 0;
    if (!readOracleU64(bytes, checksumPos, bytes.size(), checksum) || checksum != fnv1a(bytes, end))
    {
        error = "bad fnv1a trailer";
        return false;
    }

    size_t pos = 8;
    uint32_t version = 0, headerSize = 0, functionCount = 0;
    if (!readOracleU32(bytes, pos, end, version) || version != 1 ||
        !readOracleU32(bytes, pos, end, headerSize) || headerSize > end - pos)
    {
        error = "bad version or header";
        return false;
    }
    pos += headerSize;
    if (!readOracleU32(bytes, pos, end, functionCount))
    {
        error = "missing function count";
        return false;
    }

    for (uint32_t fi = 0; fi < functionCount; ++fi)
    {
        if (pos >= end)
        {
            error = "truncated function";
            return false;
        }
        unsigned nameSize = bytes[pos++];
        if (nameSize > end - pos)
        {
            error = "truncated name";
            return false;
        }
        std::string name(bytes.begin() + pos, bytes.begin() + pos + nameSize);
        pos += nameSize;
        if (end - pos < 2)
        {
            error = "truncated arity";
            return false;
        }
        unsigned arity = bytes[pos++];
        ++pos; // exhaustive flag
        uint64_t count = 0, payloadSize = 0;
        if ((arity != 1 && arity != 2) || !readOracleU64(bytes, pos, end, count) ||
            !readOracleU64(bytes, pos, end, payloadSize) || payloadSize > end - pos)
        {
            error = name + ": bad block header";
            return false;
        }
        size_t payloadEnd = pos + size_t(payloadSize);
        int64_t a = 0, b = 0, ideal = 0;
        uint64_t seen = 0;
        while (pos < payloadEnd)
        {
            int64_t da = 0, db = 0, di = 0;
            if (!readOracleVarint(bytes, pos, payloadEnd, da) ||
                (arity == 2 && !readOracleVarint(bytes, pos, payloadEnd, db)) ||
                !readOracleVarint(bytes, pos, payloadEnd, di))
            {
                error = name + ": bad record";
                return false;
            }
            a += da;
            b += db;
            ideal += di;
            if (name == "exp" || name == "log" || name == "log10" || name == "sinh" || name == "cosh" || name == "tanh")
                records[name].push_back({a, b, ideal});
            ++seen;
        }
        if (pos != payloadEnd || seen != count)
        {
            error = name + ": payload/count mismatch";
            return false;
        }
    }
    return pos == end;
}

fixed_t oracleRef(const std::string& name, fixed_t x)
{
    if (name == "exp")
        return expRef(x);
    if (name == "log")
        return logRef(x);
    if (name == "log10")
        return log10Ref(x);
    if (name == "sinh")
        return sinhRef(x);
    if (name == "cosh")
        return coshRef(x);
    return tanhRef(x);
}

} // namespace

TEST_CASE("MathExpLogLibraryFastcallAndComposition")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);

    const std::vector<int64_t> signedValues = signedInputs();
    const std::vector<int64_t> positiveValues = positiveInputs();
    checkUnary(L, "exp", expRef, signedValues);
    checkUnary(L, "sinh", sinhRef, signedValues);
    checkUnary(L, "cosh", coshRef, signedValues);
    checkUnary(L, "tanh", tanhRef, signedValues);
    checkUnary(L, "log", logRef, positiveValues);
    checkUnary(L, "log10", log10Ref, positiveValues);
    checkLogBase(L);

    lua_close(L);
}

TEST_CASE("MathExpLogNamedDomainErrors")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* scripts[] = {
        "local ok, err = pcall(function() return math.log(0) end); return tostring(ok) .. '|' .. tostring(err)",
        "local f = math.log; local ok, err = pcall(function() return f(-1) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log(1, 0) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log(1, -1) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log10(0) end); return tostring(ok) .. '|' .. tostring(err)",
        "local ok, err = pcall(function() return math.log10(-1) end); return tostring(ok) .. '|' .. tostring(err)",
    };
    for (const char* script : scripts)
    {
        std::string result = mooRun(L, script);
        bool matched = result.rfind("false|", 0) == 0 &&
            result.find("math.log of a non-positive number") != std::string::npos;
        CHECK_MESSAGE(matched, result);
    }

    const char* division[] = {
        "local ok, err = pcall(function() return math.log(1, 1) end); return tostring(ok) .. '|' .. tostring(err)",
        "local f = math.log; local ok, err = pcall(function() return f(1, 1) end); return tostring(ok) .. '|' .. tostring(err)",
    };
    for (const char* script : division)
    {
        std::string result = mooRun(L, script);
        bool matched = result.rfind("false|", 0) == 0 && result.find("division by zero") != std::string::npos;
        CHECK_MESSAGE(matched, result);
    }

    lua_close(L);
}

TEST_CASE("MathExpLogOracleMaximumErrors")
{
    std::unordered_map<std::string, std::vector<OracleRecord>> records;
    std::string error;
    REQUIRE_MESSAGE(loadOracle(records, error), error);
    for (const char* name : {"exp", "log", "log10", "sinh", "cosh", "tanh"})
    {
        const auto found = records.find(name);
        REQUIRE(found != records.end());
        uint64_t maximum = 0;
        int64_t at = 0, got = 0, ideal = 0;
        for (const OracleRecord& record : found->second)
        {
            int64_t result = oracleRef(name, record.a);
            uint64_t magnitude = result < record.ideal ? uint64_t(record.ideal) - uint64_t(result)
                                                        : uint64_t(result) - uint64_t(record.ideal);
            if (magnitude > maximum)
            {
                maximum = magnitude;
                at = record.a;
                got = result;
                ideal = record.ideal;
            }
        }
        printf("moo-math-exp-log-oracle %s samples=%zu max_err=%llu at=%lld got=%lld ideal=%lld\n", name,
            found->second.size(), (unsigned long long)maximum, (long long)at, (long long)got, (long long)ideal);
    }
}

TEST_CASE("MathExpLogConformanceScript")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK_EQ(mooRunFile(L, "l3/moo-l4-math-exp-log.luau"), "OK");
    lua_close(L);
}

TEST_SUITE_END();
