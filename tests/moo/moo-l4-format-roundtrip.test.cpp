// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4's formatting check: every formatted value round-trips (docs/DESIGN.md, "Layer 3 as built"
// "Formatting and tonumber are exact", and "Number formatting"). For a pool of seeded raw values and
// a fixed set of edges:
//   - tonumber(tostring(x)) == x bit for bit (through the VM, so through moo_numtostr + moo_numparse);
//   - string.format("%.16f", x) is the exact expansion, checked against an independent decimal expansion
//     computed from raw * 10^16 / 2^16;
//   - %e and %g at precisions 0 to 17 round half to even on the exact value, checked against an
//     independent reference formatter in this file.
#include "lua.h"
#include "lualib.h"

#include "moo.h"
#include "moo_number.h"
#include "moo_parse.h"
#include "moo_format.h"

#include "MooWide.h"
#include "MooHost.h"

#include "doctest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

namespace
{

// a deterministic generator for the raw pool
struct Lcg
{
    uint64_t s = 0x9e3779b97f4a7c15ull;
    uint64_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return s ^ (s >> 29);
    }
};

// 5^16, used to scale a raw value's fraction (m & 0xffff) * 5^16 = the exact 16 fraction digits, because
// 1 / 2^16 = 5^16 / 10^16. raw * 10^16 / 2^16 = raw * 5^16, so the integer part is raw >> 16 and the
// 16 fraction digits are (raw & 0xffff) * 5^16.
const uint64_t kPow5_16 = 152587890625ull;

// the exact decimal expansion of raw / 2^16, fixed at 16 fraction digits: the expected value of
// string.format("%.16f", x)
std::string exactExpansion(int64_t raw)
{
    bool neg = raw < 0;
    uint64_t m = neg ? uint64_t(0) - uint64_t(raw) : uint64_t(raw);
    uint64_t ip = m >> 16;
    uint64_t frac16 = (m & 0xffffu) * kPow5_16;
    std::string out = (neg ? "-" : "") + std::to_string(ip) + ".";
    char tmp[17];
    for (int i = 15; i >= 0; --i)
    {
        tmp[i] = char('0' + frac16 % 10);
        frac16 /= 10;
    }
    out.append(tmp, 16);
    return out;
}

// the full digit string of |raw|/2^16: the integer digits, then 16 fraction digits; returns the digits and
// the number of integer digits
struct Digits
{
    std::string s;
    int nint = 0;
    int ndig = 0;
    int lead = 0; // index of the first nonzero digit (== ndig when the value is zero)
    bool isZero = true;
};

Digits digitsOf(uint64_t m)
{
    Digits d;
    d.s = std::to_string(m >> 16);
    d.nint = int(d.s.size());
    char tmp[17];
    uint64_t frac16 = (m & 0xffffu) * kPow5_16;
    for (int i = 15; i >= 0; --i)
    {
        tmp[i] = char('0' + frac16 % 10);
        frac16 /= 10;
    }
    d.s.append(tmp, 16);
    d.ndig = int(d.s.size());
    while (d.lead < d.ndig && d.s[d.lead] == '0')
        d.lead++;
    d.isZero = d.lead == d.ndig;
    return d;
}

// rounds the digit string to keep `keep` digits starting at `from`; returns the kept string, which may gain a
// leading '1' on a carry (then it has keep + 1 digits)
std::string refRound(const std::string& digits, int from, int keep)
{
    std::string r;
    for (int i = 0; i < keep; ++i)
        r += (from + i < int(digits.size())) ? digits[from + i] : '0';
    if (from + keep < int(digits.size()))
    {
        char nd = digits[from + keep];
        bool rest = false;
        for (int i = from + keep + 1; i < int(digits.size()); ++i)
            if (digits[i] != '0')
            {
                rest = true;
                break;
            }
        bool up;
        if (nd > '5')
            up = true;
        else if (nd < '5')
            up = false;
        else
            up = rest ? true : ((r.back() - '0') & 1); // a tie rounds to even
        if (up)
        {
            int i = keep - 1;
            while (i >= 0 && r[i] == '9')
                r[i--] = '0';
            if (i >= 0)
                r[i]++;
            else
                r.insert(r.begin(), '1');
        }
    }
    return r;
}

// %e body (no sign, no padding): d.ddd e±xx, half to even on the exact digits
std::string refBodyE(const Digits& d, int prec, bool upper, bool stripZeros)
{
    std::string r;
    int x;
    if (d.isZero)
    {
        r = std::string(prec + 1, '0');
        x = 0;
    }
    else
    {
        int keep = prec + 1;
        r = refRound(d.s, d.lead, keep);
        x = d.nint - 1 - d.lead;
        if (int(r.size()) > keep) // a carry added a leading '1'
            x += 1;
    }
    std::string out;
    out += r[0];
    int fd = prec;
    if (stripZeros)
        while (fd > 0 && r[fd] == '0')
            fd--;
    if (fd > 0)
    {
        out += '.';
        out += r.substr(1, fd);
    }
    out += upper ? 'E' : 'e';
    out += x < 0 ? '-' : '+';
    int ax = x < 0 ? -x : x;
    if (ax < 10)
        out += '0';
    out += std::to_string(ax);
    return out;
}

// %f body: fixed notation with prec fraction digits (stripZeros for %g)
std::string refBodyF(const Digits& d, int prec, bool stripZeros)
{
    int keep = d.nint + (prec < 16 ? prec : 16);
    std::string r = refRound(d.s, 0, keep);
    int intDigits = d.nint + (int(r.size()) - keep);
    int start = 0;
    while (start < intDigits - 1 && r[start] == '0')
        start++;
    std::string out(r, start, intDigits - start);
    int fd = prec;
    std::string fr;
    for (int i = 0; i < fd; ++i)
        fr += (intDigits + i < int(r.size())) ? r[intDigits + i] : '0';
    if (stripZeros)
        while (fd > 0 && fr[fd - 1] == '0')
            fd--;
    if (fd > 0)
    {
        out += '.';
        out += fr.substr(0, fd);
    }
    return out;
}

// %g body: %e-style with precision P - 1 when the exponent is out of [-4, P), else fixed with P - 1 - X digits,
// trailing zeros stripped
std::string refBodyG(const Digits& d, int P, bool upper)
{
    int X = d.isZero ? 0 : d.nint - 1 - d.lead;
    if (P > X && X >= -4)
        return refBodyF(d, P - 1 - X, true);
    return refBodyE(d, P - 1, upper, true);
}

// the reference ( sign for a raw value and a conversion
std::string refFormatted(int64_t raw, char conv, int prec)
{
    bool neg = raw < 0;
    uint64_t m = neg ? uint64_t(0) - uint64_t(raw) : uint64_t(raw);
    Digits d = digitsOf(m);
    std::string body;
    char upper = conv == 'E' || conv == 'G';
    if (conv == 'f' || conv == 'F')
        body = refBodyF(d, prec, false);
    else if (conv == 'e' || conv == 'E')
        body = refBodyE(d, prec, upper, false);
    else
        body = refBodyG(d, prec == 0 ? 1 : prec, upper);
    return (neg ? "-" : "") + body;
}

const char* kSpec[18] = {".0", ".1", ".2", ".3", ".4", ".5", ".6", ".7", ".8", ".9", ".10", ".11", ".12", ".13", ".14", ".15", ".16", ".17"};

// the raw values with 1 to 16 significant fraction digits: fraction (2^(16-d)) has v2 = 16 - d, so the
// decimal fraction has exactly d significant digits (2^(16-d) * 5^16 has 16 - d trailing decimal zeros)
int64_t fractionDigitEdge(int d, int ip)
{
    return int64_t(ip) * 65536 + (int64_t(1) << (16 - d));
}

// through the VM: tonumber(tostring(n)) == n bit for bit
bool vmTonumberRoundtrip(lua_State* L, lua_Number n)
{
    lua_getglobal(L, "tostring");
    lua_pushnumber(L, n);
    if (lua_pcall(L, 1, 1, 0) != 0)
        return false;
    lua_getglobal(L, "tonumber");
    lua_pushvalue(L, -2);
    bool ok = lua_pcall(L, 1, 1, 0) == 0;
    if (ok)
    {
        lua_Number back = lua_tonumber(L, -1);
        ok = back == n;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return ok;
}

void checkValue(lua_State* L, int64_t n)
{
    CHECK_MESSAGE(vmTonumberRoundtrip(L, n), "round-trip of raw " << n);

    char buf[128];
    REQUIRE(moo_numformat(buf, sizeof(buf), ".16", 'f', n) >= 0);
    CHECK_MESSAGE(std::string(buf) == exactExpansion(n), "%.16f of raw " << n << ": " << buf << " want " << exactExpansion(n));

    for (int p = 0; p <= 17; ++p)
    {
        REQUIRE(moo_numformat(buf, sizeof(buf), kSpec[p], 'e', n) >= 0);
        CHECK_MESSAGE(std::string(buf) == refFormatted(n, 'e', p), "%.e of raw " << n << ": " << buf << " want " << refFormatted(n, 'e', p));
        REQUIRE(moo_numformat(buf, sizeof(buf), kSpec[p], 'g', n) >= 0);
        CHECK_MESSAGE(std::string(buf) == refFormatted(n, 'g', p), "%.g of raw " << n << ": " << buf << " want " << refFormatted(n, 'g', p));
    }
}

} // namespace

TEST_SUITE_BEGIN("moo");

// DESIGN.md "Layer 3 as built" ("Formatting and tonumber are exact") and "Number formatting": every formatted
// value round-trips, string.format("%.16f") is the exact expansion, and %e/%g round half to even on the exact
// value. Default N = 2^18 for the push gate; MOO_FORMAT_N raises it for the nightly.
TEST_CASE("FormatRoundtrip")
{
    long N = 1L << 18;
    if (const char* e = getenv("MOO_FORMAT_N"))
        N = atol(e);
    if (N < 1)
        N = 1;

    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    Lcg r;
    for (long i = 0; i < N; ++i)
    {
        int64_t n = int64_t(r.next());
        if (n == INT64_MIN)
            n = INT64_MAX;
        checkValue(L, n);
    }

    // the edges: zero, ±1 raw, ±MOO_NUMBER_MAX, every power of two raw, and values with 1..16 fraction digits
    int64_t edges[] = {0, 1, -1, INT64_MAX, -INT64_MAX};
    for (int64_t e : edges)
        checkValue(L, e);
    for (int k = 0; k <= 62; ++k)
    {
        checkValue(L, int64_t(1) << k);
        checkValue(L, -(int64_t(1) << k));
    }
    for (int d = 1; d <= 16; ++d)
    {
        checkValue(L, fractionDigitEdge(d, d));
        checkValue(L, -fractionDigitEdge(d, d + 1));
    }

    // through the VM: string.format reaches the same formatter (moo_numformat)
    mooOpenTestHelpers(L);
    CHECK(mooRun(L, "return string.format('%.16f', moo_test.fromraw('6554'))") == "0.1000061035156250");
    CHECK(mooRun(L, "return string.format('%.0e', moo_test.fromraw('98304'))") == "2e+00");
    CHECK(mooRun(L, "return string.format('%.2g', moo_test.fromraw('98304'))") == "1.5");

    lua_close(L);
}

TEST_SUITE_END();
