// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Exact text for Q112.16 wide and Q32.32 time (Common/include/moo_fixtext.h; docs/DESIGN.md "Layer 3d as built",
// W4): moo_fixtostr writes the shortest decimal that moo_fixparse reads back to the same raw, bit for bit.
//
// Three checks per raw, each by a route other than the formatter's own:
//   - parse(format(x)) == x, through moo_fixparse;
//   - the printed digits are the exact decimal expansion rounded half to even at the printed length, the expansion
//     computed here on 32-bit limbs, independently of moo_fixtext's 256-bit helper;
//   - no shorter correctly rounded prefix reads back (through moo_fixparse), so the output is the shortest.
// At 16 fraction bits and 64-bit raws the formatter and the parser are also held to number's own (moo_numtostr,
// moo_numparse), byte for byte. Default N = 2^16 per width; MOO_FORMAT_N raises it for the nightly.

#include "moo_fixtext.h"
#include "moo_format.h"
#include "moo_parse.h"
#include "doctest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("fixtext");

namespace
{

struct Rng
{
    uint64_t s = 0x2545f4914f6cdd1dull;
    uint64_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

struct Raw
{
    uint64_t hi;
    uint64_t lo;
};

Raw negate(Raw r)
{
    Raw n;
    n.lo = uint64_t(0) - r.lo;
    n.hi = uint64_t(0) - r.hi - (r.lo != 0 ? 1 : 0);
    return n;
}

// a raw of a random bit length below the width's top bit, with a random sign
Raw randomRaw(Rng& r, int bits)
{
    int b = int(r.next() % uint64_t(bits));
    Raw x = {0, 0};
    if (b > 0 && b <= 64)
        x.lo = r.next() >> (64 - b);
    else if (b > 64)
    {
        x.lo = r.next();
        x.hi = r.next() >> (128 - b);
    }
    if (bits == 64)
        x.lo &= uint64_t(INT64_MAX);
    if (r.next() & 1)
        x = negate(x);
    if (bits == 64)
        x.hi = (x.lo >> 63) ? UINT64_MAX : 0;
    return x;
}

// the exact decimal expansion of |raw| / 2^frac: integer digits, then exactly frac fraction digits, on 32-bit limbs
void exactDecimal(Raw x, int frac, std::string& ip, std::string& fp)
{
    if (x.hi >> 63)
        x = negate(x);
    uint32_t limb[4] = {uint32_t(x.lo), uint32_t(x.lo >> 32), uint32_t(x.hi), uint32_t(x.hi >> 32)};
    uint64_t f = x.lo & ((uint64_t(1) << frac) - 1);
    // integer part: shift the limbs right by frac (16 or 32)
    uint32_t ipl[4];
    for (int i = 0; i < 4; ++i)
    {
        uint64_t pair = uint64_t(limb[i]) | (i + 1 < 4 ? uint64_t(limb[i + 1]) << 32 : 0);
        ipl[i] = uint32_t(pair >> frac);
    }
    ip.clear();
    for (;;)
    {
        uint64_t rem = 0;
        bool zero = true;
        for (int i = 3; i >= 0; --i)
        {
            uint64_t cur = (rem << 32) | ipl[i];
            ipl[i] = uint32_t(cur / 10);
            rem = cur % 10;
            zero = zero && ipl[i] == 0;
        }
        ip.insert(ip.begin(), char('0' + rem));
        if (zero)
            break;
    }
    fp.clear();
    for (int i = 0; i < frac; ++i)
    {
        f *= 10;
        fp.push_back(char('0' + (f >> frac)));
        f &= (uint64_t(1) << frac) - 1;
    }
}

// ip.fp rounded half to even to k fraction digits, trailing zeros stripped, as "int[.frac]"
std::string roundedPrefix(std::string ip, const std::string& fp, int k)
{
    std::string q = fp.substr(0, size_t(k));
    bool up = false;
    if (size_t(k) < fp.size())
    {
        char nd = fp[size_t(k)];
        bool rest = fp.find_first_not_of('0', size_t(k) + 1) != std::string::npos;
        char last = k > 0 ? q.back() : ip.back();
        up = nd > '5' || (nd == '5' && (rest || ((last - '0') & 1)));
    }
    if (up)
    {
        int i = k - 1;
        while (i >= 0 && q[size_t(i)] == '9')
            q[size_t(i--)] = '0';
        if (i >= 0)
            q[size_t(i)]++;
        else
        {
            int j = int(ip.size()) - 1;
            while (j >= 0 && ip[size_t(j)] == '9')
                ip[size_t(j--)] = '0';
            if (j >= 0)
                ip[size_t(j)]++;
            else
                ip.insert(ip.begin(), '1');
        }
    }
    while (!q.empty() && q.back() == '0')
        q.pop_back();
    return q.empty() ? ip : ip + "." + q;
}

bool parsesTo(const std::string& s, int bits, int frac, Raw want)
{
    uint64_t hi = 0, lo = 0;
    int status = moo_fixparse(s.c_str(), bits, frac, &hi, &lo);
    return (status == MOO_PARSE_OK || status == MOO_PARSE_BELOW_RESOLUTION) && hi == want.hi && lo == want.lo;
}

void checkRaw(Raw x, int bits, int frac)
{
    char buf[MOO_FIXTOSTR_MAX + 8];
    char* end = moo_fixtostr(buf, x.hi, x.lo, frac);
    REQUIRE(end - buf <= MOO_FIXTOSTR_MAX);
    std::string out(buf, end);
    bool neg = (x.hi >> 63) != 0;

    CHECK_MESSAGE(parsesTo(out, bits, frac, x), out << " does not read back (bits " << bits << ", frac " << frac << ")");

    std::string ip, fp;
    exactDecimal(x, frac, ip, fp);
    std::string body = neg ? out.substr(1) : out;
    size_t dot = body.find('.');
    int k = dot == std::string::npos ? 0 : int(body.size() - dot - 1);
    CHECK_MESSAGE(body == roundedPrefix(ip, fp, k), out << " is not the exact expansion " << ip << "." << fp << " rounded at " << k);
    // shorter: no correctly rounded prefix with fewer digits reads back
    for (int j = 0; j < k; ++j)
    {
        std::string shorter = (neg ? "-" : "") + roundedPrefix(ip, fp, j);
        CHECK_MESSAGE(!parsesTo(shorter, bits, frac, x), out << " is not the shortest: " << shorter << " reads back");
    }
}

long caseCount()
{
    long n = 1L << 16;
    if (const char* e = getenv("MOO_FORMAT_N"))
        n = atol(e);
    return n < 1 ? 1 : n;
}

} // namespace

TEST_CASE("FixTextNumberAgreement")
{
    // at Q48.16 the generalised text is number's text, byte for byte, both ways
    Rng r;
    long n = caseCount();
    for (long i = 0; i < n; ++i)
    {
        Raw x = randomRaw(r, 64);
        int64_t v = int64_t(x.lo);
        char a[64], b[64];
        std::string num(a, moo_numtostr(a, v));
        std::string fix(b, moo_fixtostr(b, x.hi, x.lo, 16));
        CHECK(num == fix);
        fixed_t back = 0;
        const char* digits = num[0] == '-' ? num.c_str() + 1 : num.c_str();
        CHECK(moo_numparse(digits, &back) <= MOO_PARSE_OK);
        uint64_t hi = 0, lo = 0;
        CHECK(moo_fixparse(digits, 64, 16, &hi, &lo) == MOO_PARSE_OK);
        CHECK(int64_t(lo) == back);
    }
}

TEST_CASE("FixTextRoundtrip")
{
    Rng r;
    long n = caseCount();
    for (long i = 0; i < n; ++i)
    {
        checkRaw(randomRaw(r, 128), 128, 16);
        checkRaw(randomRaw(r, 64), 64, 32);
    }

    // the edges: zero, one raw, the largest magnitudes, every power of two
    Raw wideMax = {uint64_t(INT64_MAX), UINT64_MAX};
    Raw timeMax = {0, uint64_t(INT64_MAX)};
    Raw one = {0, 1};
    Raw zero = {0, 0};
    for (Raw x : {zero, one, negate(one), wideMax, negate(wideMax)})
        checkRaw(x, 128, 16);
    for (Raw x : {zero, one, negate(one), timeMax, negate(timeMax)})
        checkRaw(x, 64, 32);
    for (int k = 0; k < 127; ++k)
    {
        Raw p = {k >= 64 ? uint64_t(1) << (k - 64) : 0, k < 64 ? uint64_t(1) << k : 0};
        checkRaw(p, 128, 16);
        checkRaw(negate(p), 128, 16);
        if (k < 63)
        {
            checkRaw(p, 64, 32);
            checkRaw(negate(p), 64, 32);
        }
    }

    char buf[MOO_FIXTOSTR_MAX];
    CHECK(std::string(buf, moo_fixtostr(buf, wideMax.hi, wideMax.lo, 16)) == "2596148429267413814265248164610047.99998");
    CHECK(std::string(buf, moo_fixtostr(buf, timeMax.hi, timeMax.lo, 32)) == "2147483647.9999999998");
    CHECK(std::string(buf, moo_fixtostr(buf, 0, 1, 32)) == "0.0000000002");
    CHECK(std::string(buf, moo_fixtostr(buf, 0, uint64_t(1) << 30, 32)) == "0.25");
}

TEST_CASE("FixTextParse")
{
    uint64_t hi = 0, lo = 0;
    // exact values
    CHECK(moo_fixparse("0.25", 64, 32, &hi, &lo) == MOO_PARSE_OK);
    CHECK((hi == 0 && lo == uint64_t(1) << 30));
    CHECK(moo_fixparse("-1.5", 128, 16, &hi, &lo) == MOO_PARSE_OK);
    CHECK((hi == UINT64_MAX && lo == uint64_t(0) - 98304));
    CHECK(moo_fixparse("0x10", 128, 16, &hi, &lo) == MOO_PARSE_OK);
    CHECK((hi == 0 && lo == uint64_t(16) << 16));
    CHECK(moo_fixparse("0b101", 64, 32, &hi, &lo) == MOO_PARSE_OK);
    CHECK((hi == 0 && lo == uint64_t(5) << 32));
    // 2^111 - 2^-16 is the largest wide; 2^111 is not
    CHECK(moo_fixparse("2596148429267413814265248164610047.99998", 128, 16, &hi, &lo) == MOO_PARSE_OK);
    CHECK((hi == uint64_t(INT64_MAX) && lo == UINT64_MAX));
    CHECK(moo_fixparse("2596148429267413814265248164610048", 128, 16, &hi, &lo) == MOO_PARSE_OUT_OF_RANGE);
    CHECK(moo_fixparse("0x7fffffffffffffffffffffffffff", 128, 16, &hi, &lo) == MOO_PARSE_OK);
    CHECK(moo_fixparse("0x8000000000000000000000000000", 128, 16, &hi, &lo) == MOO_PARSE_OUT_OF_RANGE);
    // time: 2^31 s is out, its last raw below is in
    CHECK(moo_fixparse("2147483648", 64, 32, &hi, &lo) == MOO_PARSE_OUT_OF_RANGE);
    CHECK(moo_fixparse("2147483647.9999999998", 64, 32, &hi, &lo) == MOO_PARSE_OK);
    CHECK(lo == uint64_t(INT64_MAX));
    CHECK(moo_fixparse("0x80000000", 64, 32, &hi, &lo) == MOO_PARSE_OUT_OF_RANGE);
    // below resolution, and a tie to even at the finest raw: 2^-33 rounds to 0, 3 * 2^-33 to 2 * 2^-32
    CHECK(moo_fixparse("1e-11", 64, 32, &hi, &lo) == MOO_PARSE_BELOW_RESOLUTION);
    CHECK(moo_fixparse("0.000000000116415321826934814453125", 64, 32, &hi, &lo) == MOO_PARSE_BELOW_RESOLUTION);
    CHECK(moo_fixparse("0.000000000349245965480804443359375", 64, 32, &hi, &lo) == MOO_PARSE_OK);
    CHECK(lo == 2);
    CHECK(moo_fixparse("1e40", 128, 16, &hi, &lo) == MOO_PARSE_OUT_OF_RANGE);
    // malformed
    CHECK(moo_fixparse("", 128, 16, &hi, &lo) == MOO_PARSE_MALFORMED);
    CHECK(moo_fixparse(".", 128, 16, &hi, &lo) == MOO_PARSE_MALFORMED);
    CHECK(moo_fixparse("1w", 128, 16, &hi, &lo) == MOO_PARSE_MALFORMED);
    CHECK(moo_fixparse("0x", 64, 32, &hi, &lo) == MOO_PARSE_MALFORMED);
    CHECK(moo_fixparse("1e", 64, 32, &hi, &lo) == MOO_PARSE_MALFORMED);
}

TEST_SUITE_END();
