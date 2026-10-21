// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4's closing check (docs/DESIGN.md, "Layers" item 4, "math library", "Accuracy oracle"): every function
// in tests/moo/vectors/math-oracle.bin is measured against the composition docs/DESIGN.md's "math library" table
// names, built from fixed's C called directly, and the maximum error over all records becomes an assertion.
// The vector file is the same one moo-l4-math-oracle built; this case reuses its format and its frozen ideals,
// and never calls libm or CORE-MATH (docs/layer4/moo-l4-conformance.md).
#include "doctest.h"

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"

#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct MooConformanceRecord
{
    int64_t a, b, ideal;
};

struct MooConformanceFunction
{
    std::string name;
    std::vector<MooConformanceRecord> records;
};

std::string mooConformancePath()
{
#ifdef LUAU_CONFORMANCE_SOURCE_DIR
    return std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/../moo/vectors/math-oracle.bin";
#else
    return "tests/moo/vectors/math-oracle.bin";
#endif
}

// The reader is the same format as tests/moo/moo-l4-math-oracle.test.cpp; that case validates the file, so this
// one only decodes what it already proved well formed (tools/moo-math-oracle/README.md).
struct MooConformanceReader
{
    const std::vector<unsigned char>& bytes;
    size_t pos = 0, end = 0;

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
    bool take(std::string& s, size_t n)
    {
        if (n > end - pos)
            return false;
        s.assign(reinterpret_cast<const char*>(bytes.data() + pos), n);
        pos += n;
        return true;
    }
    bool varint(uint64_t& v, size_t limit)
    {
        uint64_t z = 0;
        for (int shift = 0; shift < 70; shift += 7)
        {
            if (pos >= limit)
                return false;
            unsigned char c = bytes[pos++];
            if (shift == 63 && c > 1)
                return false;
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

std::vector<MooConformanceFunction> mooConformanceLoad()
{
    std::string path = mooConformancePath();
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot read " << path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    std::vector<unsigned char> bytes(s.begin(), s.end());
    REQUIRE(bytes.size() > 32);

    MooConformanceReader r{bytes};
    uint64_t stored = 0;
    for (int i = 0; i < 8; i++)
        stored |= uint64_t(bytes[bytes.size() - 8 + i]) << (8 * i);
    r.end = bytes.size() - 8;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < r.end; i++)
    {
        h ^= bytes[i];
        h *= 0x100000001b3ull;
    }
    REQUIRE(stored == h);

    std::string magic, header;
    uint32_t version = 0, headerSize = 0, functionCount = 0;
    REQUIRE(r.take(magic, 8));
    REQUIRE(magic == "MOOMATH1");
    REQUIRE(r.u32(version));
    REQUIRE(version == 1);
    REQUIRE(r.u32(headerSize));
    REQUIRE(r.take(header, headerSize));
    REQUIRE(r.u32(functionCount));

    std::vector<MooConformanceFunction> out;
    for (uint32_t i = 0; i < functionCount; i++)
    {
        MooConformanceFunction f;
        unsigned nameSize = 0, arity = 0, exhaustive = 0;
        uint64_t count = 0, payload = 0;
        REQUIRE(r.u8(nameSize));
        REQUIRE(r.take(f.name, nameSize));
        REQUIRE(r.u8(arity));
        REQUIRE(r.u8(exhaustive));
        REQUIRE(r.u64(count));
        REQUIRE(r.u64(payload));
        size_t payloadEnd = r.pos + size_t(payload);
        REQUIRE(payloadEnd <= r.end);
        uint64_t a = 0, ideal = 0;
        while (r.pos < payloadEnd)
        {
            uint64_t da = 0, b = 0, di = 0;
            REQUIRE(r.varint(da, payloadEnd));
            if (arity == 2)
                REQUIRE(r.varint(b, payloadEnd));
            REQUIRE(r.varint(di, payloadEnd));
            a += da;
            ideal += di;
            f.records.push_back({int64_t(a), int64_t(b), int64_t(ideal)});
        }
        REQUIRE(f.records.size() == count);
        out.push_back(std::move(f));
    }
    return out;
}

// docs/DESIGN.md "Overflow": + and - saturate into [-INT64_MAX, INT64_MAX].
int64_t mooSatAdd(int64_t a, int64_t b)
{
    int64_t r;
    if (__builtin_add_overflow(a, b, &r))
        return b > 0 ? INT64_MAX : -INT64_MAX;
    if (r > INT64_MAX)
        return INT64_MAX;
    if (r < -INT64_MAX)
        return -INT64_MAX;
    return r;
}
int64_t mooSatSub(int64_t a, int64_t b)
{
    int64_t r;
    if (__builtin_sub_overflow(a, b, &r))
        return b < 0 ? INT64_MAX : -INT64_MAX;
    if (r > INT64_MAX)
        return INT64_MAX;
    if (r < -INT64_MAX)
        return -INT64_MAX;
    return r;
}
int64_t mooExpHalf(int64_t a)
{
    return fixExp2(mooSatSub(fixMul(a, 94548), FIX_ONE));
}

const int64_t mooOne = FIX_ONE;
const int64_t mooLog2E = 94548;   // docs/layer4/moo-l4-math-exp-log.md
const int64_t mooInvLog2Ten = 19728;

// docs/DESIGN.md "math library": each source is the fixed composition the table names.
int64_t mooCallTan(int64_t a, int64_t)
{
    fixCosSin cs = fixComputeCosSin(a);
    return cs.cosine == 0 ? 0 : fixDiv(cs.sine, cs.cosine);
}
int64_t mooCallAsin(int64_t a, int64_t)
{
    return fixAtan2(a, fixSqrt(mooSatSub(mooOne, fixMul(a, a))));
}
int64_t mooCallAcos(int64_t a, int64_t)
{
    return fixAtan2(fixSqrt(mooSatSub(mooOne, fixMul(a, a))), a);
}
int64_t mooCallAtan(int64_t a, int64_t)
{
    return fixAtan2(a, mooOne);
}
int64_t mooCallExp(int64_t a, int64_t)
{
    return fixExp2(fixMul(a, mooLog2E));
}
int64_t mooCallLog(int64_t a, int64_t)
{
    return fixDiv(fixLog2(a), mooLog2E);
}
int64_t mooCallLog10(int64_t a, int64_t)
{
    return fixMul(fixLog2(a), mooInvLog2Ten);
}
int64_t mooCallSinh(int64_t a, int64_t)
{
    return mooSatSub(mooExpHalf(a), mooExpHalf(-a));
}
int64_t mooCallCosh(int64_t a, int64_t)
{
    return mooSatAdd(mooExpHalf(a), mooExpHalf(-a));
}
int64_t mooCallTanh(int64_t a, int64_t)
{
    int64_t u = a < 0 ? -a : a;
    int64_t z = fixMul(u, mooLog2E);
    int64_t e = fixExp2(mooSatSub(0, mooSatAdd(z, z)));
    int64_t r = fixDiv(mooSatSub(mooOne, e), mooSatAdd(mooOne, e));
    return a < 0 ? -r : r;
}
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

bool mooEvery(const MooConformanceRecord&)
{
    return true;
}
bool mooPositiveBase(const MooConformanceRecord& r)
{
    return r.a > 0;
}
bool mooUnitDomain(const MooConformanceRecord& r)
{
    return r.a >= -mooOne && r.a <= mooOne;
}
bool mooNonzeroCos(const MooConformanceRecord& r)
{
    return fixCos(r.a) != 0;
}
bool mooPositive(const MooConformanceRecord& r)
{
    return r.a > 0;
}
bool mooNonnegative(const MooConformanceRecord& r)
{
    return r.a >= 0;
}

struct MooConformanceSource
{
    const char* function;
    const char* source;
    int64_t (*call)(int64_t a, int64_t b);
    bool (*measures)(const MooConformanceRecord& r);
    int64_t coreA, coreB;
    uint64_t bound;
};

// The bounds are the measured maxima (docs/layer4/moo-l4-conformance.md); each is asserted, not just printed.
const MooConformanceSource mooConformanceSources[] = {
    {"sin", "fixSin", mooCallSin, mooEvery, 64 * 65536, 0, 131066},
    {"cos", "fixCos", mooCallCos, mooEvery, 64 * 65536, 0, 131068},
    {"tan", "fixDivide(fixSin, fixCos)", mooCallTan, mooNonzeroCos, 64 * 65536, 0, 16661531715ull},
    {"asin", "fixAtan2 with fixSqrt", mooCallAsin, mooUnitDomain, 64 * 65536, 0, 4},
    {"acos", "fixAtan2 with fixSqrt", mooCallAcos, mooUnitDomain, 64 * 65536, 0, 4},
    {"atan", "fixAtan2(x, 1)", mooCallAtan, mooEvery, 64 * 65536, 0, 2},
    {"atan2", "fixAtan2", mooCallAtan2, mooEvery, 64 * 65536, 64 * 65536, 2},
    {"exp", "fixExp2(x * log2(e))", mooCallExp, mooEvery, 64 * 65536, 0, 1503472452145152ull},
    {"log", "fixLog2(x) / log2(e)", mooCallLog, mooPositive, 64 * 65536, 0, 11},
    {"log10", "fixLog2(x) * (1/log2(10))", mooCallLog10, mooPositive, 64 * 65536, 0, 15},
    {"log2", "fixLog2", mooCallLog2, mooPositive, 64 * 65536, 0, 0},
    {"sinh", "(exp(x) - exp(-x)) / 2", mooCallSinh, mooEvery, 64 * 65536, 0, 1498395532686336ull},
    {"cosh", "(exp(x) + exp(-x)) / 2", mooCallCosh, mooEvery, 64 * 65536, 0, 1174843021516800ull},
    {"tanh", "(1 - exp(-2|x|)) / (1 + exp(-2|x|))", mooCallTanh, mooEvery, 64 * 65536, 0, 2},
    {"sqrt", "fixSqrt", mooCallSqrt, mooNonnegative, 64 * 65536, 0, 1},
    {"pow", "fixPow (base > 0)", mooCallPow, mooPositiveBase, 16 * 65536, 8 * 65536, 258932934890496ull},
};

} // namespace

TEST_SUITE_BEGIN("moo-l4-conformance");

// For every function in the frozen oracle file, measure the fixed composition DESIGN.md names, print the maximum
// signed error in raw units with its input and the count at the maximum, and assert the measured bound.
TEST_CASE("MooL4ConformanceMathTable")
{
    std::vector<MooConformanceFunction> functions = mooConformanceLoad();
    REQUIRE(functions.size() == sizeof(mooConformanceSources) / sizeof(mooConformanceSources[0]));

    for (size_t i = 0; i < functions.size(); i++)
    {
        const MooConformanceFunction& f = functions[i];
        const MooConformanceSource& s = mooConformanceSources[i];
        REQUIRE(f.name == s.function);

        uint64_t worst = 0, atWorst = 0;
        bool worstNegative = false;
        MooConformanceRecord worstAt = {0, 0, 0};
        int64_t worstGot = 0;
        uint64_t measured = 0;
        for (const MooConformanceRecord& r : f.records)
        {
            if (!s.measures(r))
                continue;
            int64_t got = s.call(r.a, r.b);
            bool negative = got < r.ideal;
            uint64_t magnitude = negative ? uint64_t(r.ideal) - uint64_t(got) : uint64_t(got) - uint64_t(r.ideal);
            measured++;
            if (magnitude > worst)
            {
                worst = magnitude;
                atWorst = 1;
                worstNegative = negative;
                worstAt = r;
                worstGot = got;
            }
            else if (magnitude == worst)
            {
                atWorst++;
            }
        }

        printf("moo-l4-conformance %s source=%s samples=%llu measured=%llu max_err=%s%llu at=(%lld,%lld) got=%lld ideal=%lld count_at_max=%llu\n",
            f.name.c_str(), s.source, (unsigned long long)f.records.size(), (unsigned long long)measured,
            worst == 0 ? "" : worstNegative ? "-" : "+", (unsigned long long)worst, (long long)worstAt.a,
            (long long)worstAt.b, (long long)worstGot, (long long)worstAt.ideal, (unsigned long long)atWorst);

        CHECK(measured > 0);
        CHECK_MESSAGE(worst <= s.bound, f.name << ": measured max error " << worst << " exceeds the bound " << s.bound);
    }
}

TEST_SUITE_END();
