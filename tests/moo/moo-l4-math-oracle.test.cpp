// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4's accuracy oracle (docs/DESIGN.md, "math library", "Accuracy oracle"): the frozen vector file
// tests/moo/vectors/math-oracle.bin holds, for each math function, sampled inputs and the ideal Q48.16 answer
// (CORE-MATH's correctly rounded double, converted by the boundary rule). tools/moo-math-oracle/gen.c writes it
// offline; this test only reads it, and never calls CORE-MATH or libm. For each function with a fixed source
// (docs/DESIGN.md, "math library" table) it prints the maximum error in raw units and the count of inputs at each
// error. It asserts no bound yet: the bounds are layer 4's decision, after measurement. It asserts only that the file
// decodes and that its sample counts match its header (docs/layer4/moo-l4-math-oracle.md).
#include "doctest.h"
#include "MooHost.h"

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"

#include <stdint.h>
#include <stdio.h>

#include <fstream>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct MooOracleRecord
{
    int64_t a;
    int64_t b;
    int64_t ideal;
};

struct MooOracleFunction
{
    std::string name;
    int arity = 0;
    bool exhaustive = false;
    uint64_t count = 0;
    std::vector<MooOracleRecord> records;
};

struct MooOracleFile
{
    std::string header;
    std::vector<MooOracleFunction> functions;
};

std::string mooOracleVectorPath()
{
#ifdef LUAU_CONFORMANCE_SOURCE_DIR
    return std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/../moo/vectors/math-oracle.bin";
#else
    return "tests/moo/vectors/math-oracle.bin";
#endif
}

std::vector<unsigned char> mooOracleReadBytes()
{
    std::string path = mooOracleVectorPath();
    std::ifstream file(path, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "cannot read " << path);
    std::stringstream bytes;
    bytes << file.rdbuf();
    std::string s = bytes.str();
    return std::vector<unsigned char>(s.begin(), s.end());
}

// The reader of tools/moo-math-oracle/README.md "Format". Returns an empty string on success, or what is wrong.
class MooOracleReader
{
public:
    explicit MooOracleReader(const std::vector<unsigned char>& bytes)
        : bytes(bytes)
    {
    }

    std::string read(MooOracleFile& out)
    {
        if (bytes.size() < 8 + 4 + 4 + 4 + 8)
            return "file too short";

        uint64_t stored = 0;
        for (int i = 0; i < 8; i++)
            stored |= uint64_t(bytes[bytes.size() - 8 + i]) << (8 * i);
        if (stored != fnv1a(bytes.data(), bytes.size() - 8))
            return "fnv1a trailer does not match";
        end = bytes.size() - 8;

        std::string magic;
        if (!take(magic, 8) || magic != "MOOMATH1")
            return "bad magic";
        uint32_t version = 0, headerSize = 0, functionCount = 0;
        if (!u32(version) || version != 1)
            return "unknown version";
        if (!u32(headerSize) || !take(out.header, headerSize))
            return "truncated header";
        if (!u32(functionCount))
            return "truncated function count";

        for (uint32_t fi = 0; fi < functionCount; fi++)
        {
            MooOracleFunction f;
            unsigned nameSize = 0, arity = 0, exhaustive = 0;
            uint64_t payloadSize = 0;
            if (!u8(nameSize) || !take(f.name, nameSize) || !u8(arity) || !u8(exhaustive) || !u64(f.count) || !u64(payloadSize))
                return "truncated function block";
            if (arity != 1 && arity != 2)
                return f.name + ": bad arity";
            if (payloadSize > end - pos)
                return f.name + ": payload runs past the end";
            f.arity = int(arity);
            f.exhaustive = exhaustive != 0;

            size_t payloadEnd = pos + size_t(payloadSize);
            uint64_t a = 0, ideal = 0;
            while (pos < payloadEnd)
            {
                uint64_t da = 0, b = 0, di = 0;
                if (!varint(da, payloadEnd) || (f.arity == 2 && !varint(b, payloadEnd)) || !varint(di, payloadEnd))
                    return f.name + ": truncated record";
                a += da;
                ideal += di;
                f.records.push_back({int64_t(a), int64_t(b), int64_t(ideal)});
            }
            if (f.records.size() != f.count)
                return f.name + ": " + std::to_string(f.records.size()) + " records, header says " + std::to_string(f.count);
            for (size_t i = 1; i < f.records.size(); i++)
            {
                const MooOracleRecord& p = f.records[i - 1];
                const MooOracleRecord& q = f.records[i];
                if (p.a > q.a || (p.a == q.a && p.b >= q.b))
                    return f.name + ": records are not sorted and distinct";
            }
            out.functions.push_back(std::move(f));
        }
        if (pos != end)
            return "trailing bytes after the last function";
        return "";
    }

private:
    const std::vector<unsigned char>& bytes;
    size_t pos = 0;
    size_t end = 0;

    static uint64_t fnv1a(const unsigned char* p, size_t n)
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < n; i++)
        {
            h ^= p[i];
            h *= 0x100000001b3ull;
        }
        return h;
    }

    bool take(std::string& s, size_t n)
    {
        if (n > end - pos)
            return false;
        s.assign(reinterpret_cast<const char*>(bytes.data() + pos), n);
        pos += n;
        return true;
    }

    bool u8(unsigned& v)
    {
        if (pos >= end)
            return false;
        v = bytes[pos++];
        return true;
    }

    bool u32(uint32_t& v)
    {
        if (4 > end - pos)
            return false;
        v = 0;
        for (int i = 0; i < 4; i++)
            v |= uint32_t(bytes[pos++]) << (8 * i);
        return true;
    }

    bool u64(uint64_t& v)
    {
        if (8 > end - pos)
            return false;
        v = 0;
        for (int i = 0; i < 8; i++)
            v |= uint64_t(bytes[pos++]) << (8 * i);
        return true;
    }

    // zigzag LEB128, at most 10 bytes, within the payload
    bool varint(uint64_t& v, size_t limit)
    {
        uint64_t z = 0;
        for (int shift = 0; shift < 70; shift += 7)
        {
            if (pos >= limit)
                return false;
            unsigned char c = bytes[pos++];
            z |= uint64_t(c & 0x7f) << shift;
            if (!(c & 0x80))
            {
                v = (z >> 1) ^ (uint64_t(0) - (z & 1));
                return true;
            }
        }
        return false;
    }
};

MooOracleFile mooOracleLoad()
{
    std::vector<unsigned char> bytes = mooOracleReadBytes();
    MooOracleFile file;
    std::string error = MooOracleReader(bytes).read(file);
    REQUIRE_MESSAGE(error.empty(), "math-oracle.bin: " << error);
    return file;
}

// The fixed source of each function, from docs/DESIGN.md "math library". moo's math library itself still crosses the
// libm bridge (docs/DESIGN.md "Layer 3 as built", TEMPORARY bridges), so what is measured is the fixed function the
// table names for it, called directly; the functions the table builds from others (tan, asin, acos, atan, exp, log,
// log10, sinh, cosh, tanh) have no fixed source of their own yet and are listed as not measured.
// `measures` says which records the source is defined on: fixPow is the non-integer-exponent rule and returns 0
// for a base <= 0 (docs/DESIGN.md "pow and ^"), so it is measured on base > 0 only.
struct MooOracleSource
{
    const char* function;
    const char* source;
    int64_t (*call)(int64_t a, int64_t b);
    bool (*measures)(const MooOracleRecord& r);
    int64_t coreA, coreB; // the dense core of gen.c's sample (|a| <= coreA, |b| <= coreB), measured again alone
};

int64_t mooCallSin(int64_t a, int64_t)
{
    return fixSin(a);
}
int64_t mooCallCos(int64_t a, int64_t)
{
    return fixCos(a);
}
int64_t mooCallAtan2(int64_t a, int64_t b)
{
    return fixAtan2(a, b);
}
int64_t mooCallSqrt(int64_t a, int64_t)
{
    return fixSqrt(a);
}
int64_t mooCallLog2(int64_t a, int64_t)
{
    return fixLog2(a);
}
int64_t mooCallPow(int64_t a, int64_t b)
{
    return fixPow(a, b);
}
bool mooEvery(const MooOracleRecord&)
{
    return true;
}
bool mooPositiveBase(const MooOracleRecord& r)
{
    return r.a > 0;
}

const MooOracleSource mooOracleSources[] = {
    {"sin", "fixSin", mooCallSin, mooEvery, 64 * 65536, 0},
    {"cos", "fixCos", mooCallCos, mooEvery, 64 * 65536, 0},
    {"atan2", "fixAtan2", mooCallAtan2, mooEvery, 64 * 65536, 64 * 65536},
    {"sqrt", "fixSqrt", mooCallSqrt, mooEvery, 64 * 65536, 0},
    {"log2", "fixLog2", mooCallLog2, mooEvery, 64 * 65536, 0},
    {"pow", "fixPow (base > 0)", mooCallPow, mooPositiveBase, 16 * 65536, 8 * 65536},
};

const char* const mooOracleFunctionNames[] = {
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "exp", "log", "log10", "log2", "sinh", "cosh", "tanh", "sqrt", "pow"};

// The error bucket of a signed raw error: exact for |e| <= 4, then by powers of two of the magnitude, as a key that
// sorts in order (e itself, or +-(100 + j) for 2^(j-1) < |e| <= 2^j) and a label.
int mooOracleBucket(bool negative, uint64_t magnitude)
{
    if (magnitude <= 4)
        return negative ? -int(magnitude) : int(magnitude);
    int j = 3;
    while (j < 64 && magnitude > (uint64_t(1) << j))
        j++;
    return negative ? -(100 + j) : 100 + j;
}

std::string mooOracleBucketLabel(int key)
{
    if (key >= -4 && key <= 4)
        return std::to_string(key);
    int j = key < 0 ? -key - 100 : key - 100;
    std::string lo = std::to_string((uint64_t(1) << (j - 1)) + 1);
    std::string hi = j == 64 ? "2^64" : std::to_string(uint64_t(1) << j);
    return std::string(key < 0 ? "-" : "+") + "[" + lo + "," + hi + "]";
}

} // namespace

TEST_SUITE_BEGIN("moo-l4-math-oracle");

// The file decodes, its trailer hash matches, every record count matches its block header, every function of
// docs/DESIGN.md's "math library" table that CORE-MATH covers is present, and the header names the CORE-MATH commit.
TEST_CASE("MooL4MathOracleVectorsDecode")
{
    MooOracleFile file = mooOracleLoad();

    CHECK(file.header.find("core-math ") != std::string::npos);
    REQUIRE(file.functions.size() == sizeof(mooOracleFunctionNames) / sizeof(mooOracleFunctionNames[0]));
    for (size_t i = 0; i < file.functions.size(); i++)
    {
        CHECK(file.functions[i].name == mooOracleFunctionNames[i]);
        CHECK(file.functions[i].count == file.functions[i].records.size());
        CHECK(file.functions[i].count > 0);
    }

    // asin and acos are exhaustive over [-1, 1]: every raw value from -65536 to 65536
    for (const MooOracleFunction& f : file.functions)
        if (f.name == "asin" || f.name == "acos")
        {
            CHECK(f.exhaustive);
            REQUIRE(f.count == 131073);
            CHECK(f.records.front().a == -65536);
            CHECK(f.records.back().a == 65536);
        }

    printf("moo-math-oracle file=%s functions=%zu\n", mooOracleVectorPath().c_str(), file.functions.size());
}

// A reversed witness: one flipped byte anywhere in the file must make the reader refuse it, and a record count one
// off in a block header (with the trailer recomputed) must be refused by the count check.
TEST_CASE("MooL4MathOracleVectorsRefuseDamage")
{
    std::vector<unsigned char> bytes = mooOracleReadBytes();
    REQUIRE(bytes.size() > 64);

    for (size_t at : {size_t(0), size_t(20), bytes.size() / 2, bytes.size() - 9, bytes.size() - 1})
    {
        std::vector<unsigned char> damaged = bytes;
        damaged[at] ^= 0x10;
        MooOracleFile file;
        CHECK_MESSAGE(!MooOracleReader(damaged).read(file).empty(), "a flipped byte at " << at << " was accepted");
    }

    // the first block's count is at 8 (magic) + 4 (version) + 4 + header + 4 (function count) + 1 + name + 2
    uint32_t headerSize = uint32_t(bytes[12]) | uint32_t(bytes[13]) << 8 | uint32_t(bytes[14]) << 16 | uint32_t(bytes[15]) << 24;
    size_t countAt = 16 + headerSize + 4;
    countAt += 1 + bytes[countAt] + 2;
    std::vector<unsigned char> miscounted = bytes;
    miscounted[countAt] += 1;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i + 8 < miscounted.size(); i++)
    {
        h ^= miscounted[i];
        h *= 0x100000001b3ull;
    }
    for (int i = 0; i < 8; i++)
        miscounted[miscounted.size() - 8 + i] = (unsigned char)(h >> (8 * i));
    MooOracleFile file;
    std::string error = MooOracleReader(miscounted).read(file);
    CHECK_MESSAGE(error.find("header says") != std::string::npos, "a miscounted block gave: " << error);
}

// The ideal at points where the real answer is exact: these anchor the file to the definition, and
// tests/conformance/moo/l3/moo-l4-math-oracle.luau checks the same points through the VM.
TEST_CASE("MooL4MathOracleExactPoints")
{
    MooOracleFile file = mooOracleLoad();
    auto ideal = [&](const char* name, int64_t a, int64_t b) -> int64_t {
        for (const MooOracleFunction& f : file.functions)
            if (f.name == name)
                for (const MooOracleRecord& r : f.records)
                    if (r.a == a && r.b == b)
                        return r.ideal;
        FAIL("no record " << name << "(" << a << ", " << b << ")");
        return 0;
    };

    const int64_t one = 65536;
    CHECK(ideal("sin", 0, 0) == 0);
    CHECK(ideal("cos", 0, 0) == one);
    CHECK(ideal("tan", 0, 0) == 0);
    CHECK(ideal("asin", 0, 0) == 0);
    CHECK(ideal("acos", one, 0) == 0);
    CHECK(ideal("atan", 0, 0) == 0);
    CHECK(ideal("atan2", 0, one) == 0);
    CHECK(ideal("exp", 0, 0) == one);
    CHECK(ideal("log", one, 0) == 0);
    CHECK(ideal("log10", one, 0) == 0);
    CHECK(ideal("log2", one, 0) == 0);
    CHECK(ideal("sinh", 0, 0) == 0);
    CHECK(ideal("cosh", 0, 0) == one);
    CHECK(ideal("tanh", 0, 0) == 0);
    CHECK(ideal("sqrt", one, 0) == one);
    CHECK(ideal("sqrt", 0, 0) == 0);
    CHECK(ideal("pow", one, one) == one);
    // asin(1) = pi/2 = 102943.708... raw, so 102944; acos(-1) = pi = 205887.416... raw, so 205887
    CHECK(ideal("asin", one, 0) == 102944);
    CHECK(ideal("acos", -one, 0) == 205887);
}

// The table layer 4 decides the bounds from: for each function with a fixed source, the maximum error in raw units
// against the ideal and the count of inputs at each error (exact for |e| <= 4, then by powers of two). Asserts no
// bound (docs/layer4/moo-l4-math-oracle.md, Decision 2).
TEST_CASE("MooL4MathOracleErrorTable")
{
    MooOracleFile file = mooOracleLoad();

    for (const MooOracleFunction& f : file.functions)
    {
        const MooOracleSource* source = nullptr;
        for (const MooOracleSource& s : mooOracleSources)
            if (f.name == s.function)
                source = &s;

        if (!source)
        {
            printf("moo-math-oracle %s samples=%llu not measured: no fixed source yet (math.%s is the libm bridge)\n", f.name.c_str(),
                (unsigned long long)f.count, f.name.c_str());
            continue;
        }

        auto measure = [&](bool core) {
            std::map<int, uint64_t> buckets;
            uint64_t measured = 0, worst = 0;
            bool worstNegative = false;
            MooOracleRecord worstAt = {0, 0, 0};
            int64_t worstGot = 0;
            for (const MooOracleRecord& r : f.records)
            {
                if (!source->measures(r))
                    continue;
                if (core && (r.a > source->coreA || r.a < -source->coreA || r.b > source->coreB || r.b < -source->coreB))
                    continue;
                int64_t got = source->call(r.a, r.b);
                // the signed difference got - ideal as a magnitude and a sign, with no int64 overflow
                bool negative = got < r.ideal;
                uint64_t magnitude = negative ? uint64_t(r.ideal) - uint64_t(got) : uint64_t(got) - uint64_t(r.ideal);
                buckets[mooOracleBucket(negative, magnitude)]++;
                measured++;
                if (magnitude > worst)
                {
                    worst = magnitude;
                    worstNegative = negative;
                    worstAt = r;
                    worstGot = got;
                }
            }

            std::string histogram;
            for (const auto& [key, count] : buckets)
                histogram += " " + mooOracleBucketLabel(key) + ":" + std::to_string(count);

            printf("moo-math-oracle %s source=%s %s samples=%llu measured=%llu max_err=%s%llu", f.name.c_str(), source->source,
                core ? "core" : "all", (unsigned long long)f.count, (unsigned long long)measured, worst == 0 ? "" : worstNegative ? "-" : "+", (unsigned long long)worst);
            if (worst != 0)
            {
                if (f.arity == 2)
                    printf(" at=(%lld,%lld)", (long long)worstAt.a, (long long)worstAt.b);
                else
                    printf(" at=%lld", (long long)worstAt.a);
                printf(" got=%lld ideal=%lld", (long long)worstGot, (long long)worstAt.ideal);
            }
            printf(" errors:%s\n", histogram.c_str());
        };
        measure(false);
        measure(true);
    }
}

TEST_SUITE_END();
