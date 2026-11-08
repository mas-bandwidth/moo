// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 3d, operators (W2): the minimal oracle (docs/DESIGN.md "Layer 3d as built"). Every wide, poswide,
// worldtransformwide and time operator runs in moo, through a script function called through the C API so nothing
// folds, and the bits are held two ways: against fixed's C on the same operands (extern/fixed, FIX_SATURATE, as moo
// builds it), and against an exact 256-bit reference written here on 64-bit limbs, with the overflow rule applied in
// this file's own words. A release build must give fixed's saturated result; a checked build (MOO_OVERFLOW_ERRORS)
// must raise the operator's named error exactly when the exact result is out of range, and give the same bits
// otherwise. Division by zero is "division by zero" in both.
//
// Inputs: the edge set (0, +-1 raw, +-2^63, +-2^64, +-2^111, +-(2^127 - 1) for wide; +-1 raw, +-1 s, +-2^31 s and the
// time range's ends for time; number's ends) and 2^12 splitmix64 cases per operator (MOO_ORACLE_WIDE_N sets the
// count). The checked-prefix property is held on accumulation: release saturates and stays; checked raises at the
// first step release saturates, with every earlier value equal.

#include "lua.h"
#include "lualib.h"
#include "doctest.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "moo_number.h"
#include "moo_vec3.h"
#include "moo_transform.h"
#include "moo_wide.h"
#include "Luau/Compiler.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("wide_ops");

namespace wide_w2
{

namespace
{

const fixed_t kOne = MOO_NUMBER_ONE;
const fixed_t kMax = MOO_NUMBER_MAX;
const fixed_t kMin = MOO_NUMBER_MIN;

// ---- the reference: two's complement 256-bit integers on four little-endian limbs (as fixed's wide_arith_test.c) ----

struct Ref
{
    uint64_t limb[4];
};

Ref refFromU64(uint64_t v)
{
    return Ref{{v, 0, 0, 0}};
}

Ref refFromI64(int64_t v)
{
    uint64_t extension = v < 0 ? UINT64_MAX : 0;
    return Ref{{uint64_t(v), extension, extension, extension}};
}

Ref refFromWide(fixedWide_t a)
{
    uint64_t hi = fixInt128Hi(a);
    uint64_t extension = (hi >> 63) != 0 ? UINT64_MAX : 0;
    return Ref{{fixInt128Lo(a), hi, extension, extension}};
}

bool refIsNegative(Ref a)
{
    return (a.limb[3] >> 63) != 0;
}

Ref refAdd(Ref a, Ref b)
{
    Ref r;
    uint64_t carry = 0;
    for (int i = 0; i < 4; i++)
    {
        uint64_t sum = a.limb[i] + b.limb[i];
        uint64_t carryOut = sum < a.limb[i] ? 1 : 0;
        r.limb[i] = sum + carry;
        carryOut += r.limb[i] < sum ? 1 : 0;
        carry = carryOut;
    }
    return r;
}

Ref refNeg(Ref a)
{
    for (int i = 0; i < 4; i++)
        a.limb[i] = ~a.limb[i];
    return refAdd(a, refFromU64(1));
}

Ref refSub(Ref a, Ref b)
{
    return refAdd(a, refNeg(b));
}

bool refLtUnsigned(Ref a, Ref b)
{
    for (int i = 3; i >= 0; i--)
        if (a.limb[i] != b.limb[i])
            return a.limb[i] < b.limb[i];
    return false;
}

bool refLt(Ref a, Ref b)
{
    if (refIsNegative(a) != refIsNegative(b))
        return refIsNegative(a);
    return refLtUnsigned(a, b);
}

Ref refShl1(Ref a)
{
    Ref r;
    r.limb[0] = a.limb[0] << 1;
    for (int i = 1; i < 4; i++)
        r.limb[i] = (a.limb[i] << 1) | (a.limb[i - 1] >> 63);
    return r;
}

// floor division by 2^shift, 1 <= shift <= 63
Ref refShrArithmetic(Ref a, int shift)
{
    Ref r;
    for (int i = 0; i < 4; i++)
    {
        r.limb[i] = a.limb[i] >> shift;
        if (i < 3)
            r.limb[i] |= a.limb[i + 1] << (64 - shift);
    }
    if (refIsNegative(a))
        r.limb[3] |= UINT64_MAX << (64 - shift);
    return r;
}

Ref refAbs(Ref a)
{
    return refIsNegative(a) ? refNeg(a) : a;
}

// 64x64 -> 128 in 32-bit pieces
void refMulU64(uint64_t a, uint64_t b, uint64_t* hi, uint64_t* lo)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
    uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t middle = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    *lo = (p00 & 0xFFFFFFFFu) | (middle << 32);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
}

// signed product of two values of at most 128 bits, exact at 256
Ref refMul(Ref a, Ref b)
{
    Ref x = refAbs(a), y = refAbs(b);
    Ref r = refFromU64(0);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
        {
            uint64_t hi, lo;
            refMulU64(x.limb[i], y.limb[j], &hi, &lo);
            Ref term = refFromU64(0);
            term.limb[i + j] = lo;
            term.limb[i + j + 1] = hi;
            r = refAdd(r, term);
        }
    return refIsNegative(a) != refIsNegative(b) ? refNeg(r) : r;
}

// (product + half) >> 16: the rounding of every multiply here
Ref refRoundProduct(Ref a, Ref b)
{
    return refShrArithmetic(refAdd(refMul(a, b), refFromU64(uint64_t(FIX_HALF))), FIX_FRACTION_BITS);
}

// (a << 16) / b truncated toward zero, b nonzero: magnitudes divide, the sign applies
Ref refQuotient(Ref a, Ref b)
{
    Ref numerator = refAbs(a);
    for (int i = 0; i < FIX_FRACTION_BITS; i++)
        numerator = refShl1(numerator);
    Ref divisor = refAbs(b);
    Ref quotient = refFromU64(0), remainder = refFromU64(0);
    for (int i = 255; i >= 0; i--)
    {
        remainder = refShl1(remainder);
        remainder.limb[0] |= (numerator.limb[i / 64] >> (i % 64)) & 1;
        if (!refLtUnsigned(remainder, divisor))
        {
            remainder = refSub(remainder, divisor);
            quotient.limb[i / 64] |= uint64_t(1) << (i % 64);
        }
    }
    return refIsNegative(a) != refIsNegative(b) ? refNeg(quotient) : quotient;
}

// ---- the rule, in this file's words: the range, and the edge an out-of-range result saturates to ----

const Ref kRefWideMax = {{UINT64_MAX, UINT64_C(0x7FFFFFFFFFFFFFFF), 0, 0}};
const Ref kRefWideMin = {{1, UINT64_C(0x8000000000000000), UINT64_MAX, UINT64_MAX}};

bool wideInRange(Ref exact)
{
    return !refLt(kRefWideMax, exact) && !refLt(exact, kRefWideMin);
}

fixedWide_t wideSaturated(Ref exact)
{
    Ref r = refLt(kRefWideMax, exact) ? kRefWideMax : refLt(exact, kRefWideMin) ? kRefWideMin : exact;
    return fixInt128Make(r.limb[1], r.limb[0]);
}

// number's and time's range are the same, +-INT64_MAX
bool int64InRange(Ref exact)
{
    return !refLt(refFromI64(INT64_MAX), exact) && !refLt(exact, refFromI64(-INT64_MAX));
}

int64_t int64Saturated(Ref exact)
{
    if (refLt(refFromI64(INT64_MAX), exact))
        return INT64_MAX;
    if (refLt(exact, refFromI64(-INT64_MAX)))
        return -INT64_MAX;
    return int64_t(exact.limb[0]);
}

// ---- inputs ----

uint64_t caseCount()
{
    const char* text = getenv("MOO_ORACLE_WIDE_N");
    if (!text)
        return uint64_t(1) << 12;
    char* end = nullptr;
    unsigned long long n = strtoull(text, &end, 10);
    REQUIRE_MESSAGE((*end == 0 && n > 0 && text[0] != '-'), "MOO_ORACLE_WIDE_N must be a positive decimal count, not '" << text << "'");
    return uint64_t(n);
}

const fixedWide_t kWideMax = FIX_WIDE_MAX;
const fixedWide_t kWideMin = FIX_WIDE_MIN;

fixedWide_t wideBit(int bit, bool negative)
{
    fixedWide_t v = bit >= 64 ? fixInt128Make(uint64_t(1) << (bit - 64), 0) : fixInt128Make(0, uint64_t(1) << bit);
    return negative ? fixInt128Neg(v) : v;
}

// splitmix64 with a fixed seed per operator: the same inputs on every platform
struct Rng
{
    uint64_t state;

    uint64_t next()
    {
        uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    // an edge one time in four, otherwise a random bit length and sign; never the reserved 128-bit minimum
    fixedWide_t wide()
    {
        uint64_t r = next();
        if ((r & 3) == 0)
        {
            const fixedWide_t edges[] = {FIX_INT128_ZERO, fixInt128Make(0, 1), fixInt128Neg(fixInt128Make(0, 1)), wideBit(16, false),
                wideBit(16, true), wideBit(63, false), wideBit(63, true), wideBit(64, false), wideBit(64, true), wideBit(111, false),
                wideBit(111, true), kWideMax, kWideMin, fixInt128Sub(kWideMax, fixInt128Make(0, 1)),
                fixInt128Add(kWideMin, fixInt128Make(0, 1))};
            return edges[(r >> 2) % (sizeof(edges) / sizeof(edges[0]))];
        }
        int bits = int((r >> 2) % 128);
        uint64_t hi = next(), lo = next();
        fixedWide_t v = bits >= 64 ? fixInt128Make(hi >> (127 - bits), lo) : fixInt128Make(0, lo >> (63 - bits));
        return (r & 0x200) ? fixInt128Neg(v) : v;
    }

    // a number, raw, in [-INT64_MAX, INT64_MAX]
    fixed_t number()
    {
        uint64_t r = next();
        if ((r & 3) == 0)
        {
            const fixed_t edges[] = {0, 1, -1, kOne, -kOne, kOne / 2, fixed_t(1) << 47, -(fixed_t(1) << 47), kMax, kMin, kMax - 1, kMin + 1};
            return edges[(r >> 2) % (sizeof(edges) / sizeof(edges[0]))];
        }
        int64_t v = int64_t(next() >> (1 + (r >> 2) % 63));
        return (r & 0x200) ? -v : v;
    }

    // a time, raw Q32.32, in [-INT64_MAX, INT64_MAX]: 1 s is 2^32, 2^31 s is the range's end
    fixTime time()
    {
        uint64_t r = next();
        if ((r & 3) == 0)
        {
            const fixTime edges[] = {0, 1, -1, FIX_TIME_ONE, -FIX_TIME_ONE, FIX_TIME_ONE / 2, fixTime(1) << 62, -(fixTime(1) << 62),
                FIX_TIME_MAX, FIX_TIME_MIN, FIX_TIME_MAX - 1, FIX_TIME_MIN + 1};
            return edges[(r >> 2) % (sizeof(edges) / sizeof(edges[0]))];
        }
        int64_t v = int64_t(next() >> (1 + (r >> 2) % 63));
        return (r & 0x200) ? -v : v;
    }

    fixVec3 vec()
    {
        fixVec3 v = {number(), number(), number()};
        return v;
    }

    fixPosWide pos()
    {
        fixPosWide p = {wide(), wide(), wide()};
        return p;
    }
};

// ---- the moo side: one script function per operator, called through the C API ----

const char* const kOperators = R"(
return {
    add = function(a, b) return a + b end,
    sub = function(a, b) return a - b end,
    mul = function(a, b) return a * b end,
    div = function(a, b) return a / b end,
    unm = function(a) return -a end,
    lt = function(a, b) return a < b end,
    le = function(a, b) return a <= b end,
    eq = function(a, b) return a == b end,
}
)";

struct Moo
{
    lua_State* L;

    Moo()
    {
        L = luaL_newstate();
        REQUIRE(L != nullptr);
        luaL_openlibs(L);
        std::string bytecode = Luau::compile(kOperators);
        REQUIRE(luau_load(L, "=wide_ops", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
        lua_setglobal(L, "ops");
    }

    ~Moo()
    {
        lua_close(L);
    }

    void pushWide(fixedWide_t a)
    {
        setwidevalue(L, L->top, moo_wide_new(L, a));
        incr_top(L);
    }

    void pushTime(fixTime t)
    {
        settimevalue(L, L->top, moo_time_new(L, t));
        incr_top(L);
    }

    void pushNumber(fixed_t n)
    {
        setnvalue(L->top, n);
        incr_top(L);
    }

    void pushVec3(fixVec3 v)
    {
        setvec3value(L, L->top, moo_vec3_new(L, v.x, v.y, v.z));
        incr_top(L);
    }

    void pushPosWide(fixPosWide p)
    {
        setposwidevalue(L, L->top, moo_poswide_new(L, p));
        incr_top(L);
    }

    void pushTransform(fixTransform t)
    {
        settransformvalue(L, L->top, moo_transform_new(L, t));
        incr_top(L);
    }

    void pushWorldTransformWide(fixWorldTransformWide t)
    {
        setworldtransformwidevalue(L, L->top, moo_worldtransformwide_new(L, t));
        incr_top(L);
    }

    // pushes ops[name]; then the caller pushes the operands and calls call(n)
    void begin(const char* name)
    {
        lua_getglobal(L, "ops");
        lua_getfield(L, -1, name);
        lua_remove(L, -2);
    }

    // the error message, or "" with the result left on the stack
    std::string call(int args)
    {
        if (lua_pcall(L, args, 1, 0) == 0)
            return "";
        std::string message = lua_tostring(L, -1);
        lua_pop(L, 1);
        // the chunk name and line come first: "wide_ops:3: wide overflow in '+'"
        size_t colon = message.find(": ");
        return colon == std::string::npos ? message : message.substr(colon + 2);
    }

    const TValue* result()
    {
        return L->top - 1;
    }

    void pop()
    {
        lua_pop(L, 1);
    }
};

bool sameWide(fixedWide_t a, fixedWide_t b)
{
    return fixInt128Eq(a, b);
}

bool samePos(fixPosWide a, fixPosWide b)
{
    return sameWide(a.x, b.x) && sameWide(a.y, b.y) && sameWide(a.z, b.z);
}

bool sameVec(fixVec3 a, fixVec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

#if MOO_OVERFLOW_ERRORS
const bool kChecked = true;
#else
const bool kChecked = false;
#endif

// The counts each operator ran, for the summary line, and the mismatches (which fail the case on the spot).
struct Tally
{
    uint64_t cases = 0;
    uint64_t outOfRange = 0; // the exact result is out of range: release saturates, checked raises
    uint64_t raised = 0;
};

// One wide result: the moo call already made; exact is the reference, expected fixed's C; error the checked name.
void checkWide(Moo& m, const char* what, const std::string& error, Ref exact, fixedWide_t fixedResult, const char* name, Tally& tally)
{
    tally.cases++;
    bool inRange = wideInRange(exact);
    tally.outOfRange += inRange ? 0 : 1;
    // fixed's C is the reference's rule
    REQUIRE_MESSAGE(sameWide(fixedResult, wideSaturated(exact)), what << ": fixed's C is not the saturated exact result");
    if (kChecked && !inRange)
    {
        REQUIRE_MESSAGE(error == name, what << ": checked build raised '" << error << "', not '" << name << "'");
        tally.raised++;
        return;
    }
    REQUIRE_MESSAGE(error.empty(), what << ": raised '" << error << "'");
    REQUIRE(ttiswide(m.result()));
    REQUIRE_MESSAGE(sameWide(moo_wide_payload(widevalue(m.result())), fixedResult), what << ": moo's bits differ from fixed's");
    m.pop();
}

void checkTime(Moo& m, const char* what, const std::string& error, Ref exact, fixTime fixedResult, const char* name, Tally& tally)
{
    tally.cases++;
    REQUIRE_MESSAGE(fixedResult == int64Saturated(exact), what << ": fixed's C is not the saturated exact result");
    tally.outOfRange += int64InRange(exact) ? 0 : 1;
    if (kChecked && !int64InRange(exact))
    {
        REQUIRE_MESSAGE(error == name, what << ": checked build raised '" << error << "', not '" << name << "'");
        tally.raised++;
        return;
    }
    REQUIRE_MESSAGE(error.empty(), what << ": raised '" << error << "'");
    REQUIRE(ttistime(m.result()));
    REQUIRE_MESSAGE(moo_time_payload(timevalue(m.result())) == fixedResult, what << ": moo's bits differ from fixed's");
    m.pop();
}

void checkNumber(Moo& m, const char* what, const std::string& error, Ref exact, fixed_t fixedResult, const char* name, Tally& tally)
{
    tally.cases++;
    REQUIRE_MESSAGE(fixedResult == int64Saturated(exact), what << ": fixed's C is not the saturated exact result");
    tally.outOfRange += int64InRange(exact) ? 0 : 1;
    if (kChecked && !int64InRange(exact))
    {
        REQUIRE_MESSAGE(error == name, what << ": checked build raised '" << error << "', not '" << name << "'");
        tally.raised++;
        return;
    }
    REQUIRE_MESSAGE(error.empty(), what << ": raised '" << error << "'");
    REQUIRE(ttisnumber(m.result()));
    REQUIRE_MESSAGE(nvalue(m.result()) == fixedResult, what << ": moo's bits differ from fixed's");
    m.pop();
}

void checkDivisionByZero(Moo& m, const char* what, const std::string& error, Tally& tally)
{
    tally.cases++;
    tally.raised++;
    REQUIRE_MESSAGE(error == "division by zero", what << ": raised '" << error << "'");
}

} // namespace

// wide + - * / and unary -, wide with wide
TEST_CASE("WideOperatorsAgainstFixedAndExact")
{
    Moo m;
    uint64_t n = caseCount();
    Tally add, sub, mul, div, unm;
    Rng rng = {0x3d01};
    for (uint64_t i = 0; i < n; i++)
    {
        fixedWide_t a = rng.wide(), b = rng.wide();
        Ref ra = refFromWide(a), rb = refFromWide(b);

        m.begin("add"), m.pushWide(a), m.pushWide(b);
        checkWide(m, "wide + wide", m.call(2), refAdd(ra, rb), fixWideAdd(a, b), "wide overflow in '+'", add);

        m.begin("sub"), m.pushWide(a), m.pushWide(b);
        checkWide(m, "wide - wide", m.call(2), refSub(ra, rb), fixWideSub(a, b), "wide overflow in '-'", sub);

        m.begin("mul"), m.pushWide(a), m.pushWide(b);
        checkWide(m, "wide * wide", m.call(2), refRoundProduct(ra, rb), fixWideMul(a, b), "wide overflow in '*'", mul);

        m.begin("div"), m.pushWide(a), m.pushWide(b);
        std::string error = m.call(2);
        if (fixInt128Eq(b, FIX_INT128_ZERO))
            checkDivisionByZero(m, "wide / wide", error, div);
        else
            checkWide(m, "wide / wide", error, refQuotient(ra, rb), fixWideDiv(a, b), "wide overflow in '/'", div);

        m.begin("unm"), m.pushWide(a);
        checkWide(m, "-wide", m.call(1), refNeg(ra), fixWideNeg(a), "", unm);
    }
    // the edges are reached: some products and quotients saturate, and some do not
    CHECK(mul.outOfRange > 0);
    CHECK(mul.outOfRange < mul.cases);
    CHECK(unm.outOfRange == 0);
    MESSAGE("wide operators " << (kChecked ? "checked" : "release") << ": cases=" << n << " add out of range=" << add.outOfRange << " raised=" << add.raised
                              << " sub out of range=" << sub.outOfRange << " raised=" << sub.raised << " mul out of range=" << mul.outOfRange << " raised=" << mul.raised << " div out of range=" << div.outOfRange << " raised=" << div.raised);
}

// wide with number: wide +- number (fixWideOffset), wide * number and number * wide (fixWideMulFixed), wide / number
TEST_CASE("WideNumberOperatorsAgainstFixedAndExact")
{
    Moo m;
    uint64_t n = caseCount();
    Tally add, sub, mul, mulLeft, div;
    Rng rng = {0x3d02};
    for (uint64_t i = 0; i < n; i++)
    {
        fixedWide_t a = rng.wide();
        fixed_t s = rng.number();
        Ref ra = refFromWide(a), rs = refFromI64(s);

        m.begin("add"), m.pushWide(a), m.pushNumber(s);
        checkWide(m, "wide + number", m.call(2), refAdd(ra, rs), fixWideOffset(a, s), "wide overflow in '+'", add);

        m.begin("sub"), m.pushWide(a), m.pushNumber(s);
        checkWide(m, "wide - number", m.call(2), refSub(ra, rs), fixWideOffset(a, -s), "wide overflow in '-'", sub);

        m.begin("mul"), m.pushWide(a), m.pushNumber(s);
        checkWide(m, "wide * number", m.call(2), refRoundProduct(ra, rs), fixWideMulFixed(a, s), "wide overflow in '*'", mul);

        m.begin("mul"), m.pushNumber(s), m.pushWide(a);
        checkWide(m, "number * wide", m.call(2), refRoundProduct(ra, rs), fixWideMulFixed(a, s), "wide overflow in '*'", mulLeft);

        m.begin("div"), m.pushWide(a), m.pushNumber(s);
        std::string error = m.call(2);
        if (s == 0)
            checkDivisionByZero(m, "wide / number", error, div);
        else
            checkWide(m, "wide / number", error, refQuotient(ra, rs), fixWideDivFixed(a, s), "wide overflow in '/'", div);
    }
    CHECK(add.outOfRange > 0);
    CHECK(mul.outOfRange > 0);
    MESSAGE("wide-number operators " << (kChecked ? "checked" : "release") << ": cases=" << n << " add out of range=" << add.outOfRange << " raised=" << add.raised
                                     << " mul out of range=" << mul.outOfRange << " raised=" << mul.raised << " div out of range=" << div.outOfRange << " raised=" << div.raised);
}

// wide == < <=: fixWideEq/Lt/Le, held against the reference's signed order
TEST_CASE("WideOrder")
{
    Moo m;
    uint64_t n = caseCount();
    Rng rng = {0x3d03};
    for (uint64_t i = 0; i < n; i++)
    {
        fixedWide_t a = rng.wide(), b = (i & 7) == 0 ? a : rng.wide();
        Ref ra = refFromWide(a), rb = refFromWide(b);
        bool lt = refLt(ra, rb), eq = !refLt(ra, rb) && !refLt(rb, ra);
        REQUIRE(fixWideLt(a, b) == lt);
        const char* names[] = {"lt", "le", "eq"};
        bool expected[] = {lt, lt || eq, eq};
        for (int k = 0; k < 3; k++)
        {
            m.begin(names[k]), m.pushWide(a), m.pushWide(b);
            REQUIRE(m.call(2).empty());
            REQUIRE(lua_toboolean(m.L, -1) == int(expected[k]));
            m.pop();
        }
    }
}

// time + - and unary -, time / time -> number, time * number, time / number, == < <=
TEST_CASE("TimeOperatorsAgainstFixedAndExact")
{
    Moo m;
    uint64_t n = caseCount();
    Tally add, sub, unm, ratio, mul, div;
    Rng rng = {0x3d04};
    for (uint64_t i = 0; i < n; i++)
    {
        fixTime t = rng.time(), u = rng.time();
        fixed_t s = rng.number();
        Ref rt = refFromI64(t), ru = refFromI64(u), rs = refFromI64(s);

        m.begin("add"), m.pushTime(t), m.pushTime(u);
        checkTime(m, "time + time", m.call(2), refAdd(rt, ru), fixTimeAdd(t, u), "time overflow in '+'", add);

        m.begin("sub"), m.pushTime(t), m.pushTime(u);
        checkTime(m, "time - time", m.call(2), refSub(rt, ru), fixTimeSub(t, u), "time overflow in '-'", sub);

        m.begin("unm"), m.pushTime(t);
        checkTime(m, "-time", m.call(1), refNeg(rt), fixTimeNeg(t), "", unm);

        m.begin("div"), m.pushTime(t), m.pushTime(u);
        std::string error = m.call(2);
        if (u == 0)
            checkDivisionByZero(m, "time / time", error, ratio);
        else
            checkNumber(m, "time / time", error, refQuotient(rt, ru), fixTimeDiv(t, u), "time overflow in '/'", ratio);

        m.begin("mul"), m.pushTime(t), m.pushNumber(s);
        checkTime(m, "time * number", m.call(2), refRoundProduct(rt, rs), fixTimeMulFixed(t, s), "time overflow in '*'", mul);

        m.begin("div"), m.pushTime(t), m.pushNumber(s);
        error = m.call(2);
        if (s == 0)
            checkDivisionByZero(m, "time / number", error, div);
        else
            checkTime(m, "time / number", error, refQuotient(rt, rs), fixTimeDivFixed(t, s), "time overflow in '/'", div);

        const char* names[] = {"lt", "le", "eq"};
        bool expected[] = {t < u, t <= u, t == u};
        for (int k = 0; k < 3; k++)
        {
            m.begin(names[k]), m.pushTime(t), m.pushTime(u);
            REQUIRE(m.call(2).empty());
            REQUIRE(lua_toboolean(m.L, -1) == int(expected[k]));
            m.pop();
        }
    }
    CHECK(add.outOfRange > 0);
    CHECK(mul.outOfRange > 0);
    CHECK(unm.outOfRange == 0);
    MESSAGE("time operators " << (kChecked ? "checked" : "release") << ": cases=" << n << " add out of range=" << add.outOfRange << " raised=" << add.raised
                              << " ratio out of range=" << ratio.outOfRange << " raised=" << ratio.raised << " mul out of range=" << mul.outOfRange << " raised=" << mul.raised << " div out of range=" << div.outOfRange << " raised=" << div.raised);
}

// poswide +- vec3 (fixPosWideOffset), poswide - poswide -> vec3 (fixPosWideSub), worldtransformwide * transform and
// * vec3 (compounds of fixed functions: fixed's C, verbatim, in both builds)
TEST_CASE("PosWideAndWorldTransformWideOperators")
{
    Moo m;
    uint64_t n = caseCount();
    Rng rng = {0x3d05};
    uint64_t offsetRaised = 0, differenceRaised = 0;
    for (uint64_t i = 0; i < n; i++)
    {
        fixPosWide p = rng.pos(), q = rng.pos();
        fixVec3 v = rng.vec();
        const fixedWide_t* pc[3] = {&p.x, &p.y, &p.z};
        const fixedWide_t* qc[3] = {&q.x, &q.y, &q.z};
        const fixed_t vc[3] = {v.x, v.y, v.z};

        for (int sign = 0; sign < 2; sign++)
        {
            bool inRange = true;
            fixPosWide saturated;
            fixedWide_t* sc[3] = {&saturated.x, &saturated.y, &saturated.z};
            for (int k = 0; k < 3; k++)
            {
                Ref exact = sign == 0 ? refAdd(refFromWide(*pc[k]), refFromI64(vc[k])) : refSub(refFromWide(*pc[k]), refFromI64(vc[k]));
                inRange = inRange && wideInRange(exact);
                *sc[k] = wideSaturated(exact);
            }
            fixPosWide expected = fixPosWideOffset(p, sign == 0 ? v : fixVecNeg(v));
            REQUIRE(samePos(expected, saturated));
            m.begin(sign == 0 ? "add" : "sub"), m.pushPosWide(p), m.pushVec3(v);
            std::string error = m.call(2);
            if (kChecked && !inRange)
            {
                REQUIRE(error == (sign == 0 ? "poswide overflow in '+'" : "poswide overflow in '-'"));
                offsetRaised++;
                continue;
            }
            REQUIRE_MESSAGE(error.empty(), "poswide +- vec3 raised '" << error << "'");
            REQUIRE(ttisposwide(m.result()));
            REQUIRE(samePos(moo_poswide_payload(poswidevalue(m.result())), expected));
            m.pop();
        }

        bool inRange = true;
        fixed_t d[3];
        for (int k = 0; k < 3; k++)
        {
            Ref exact = refSub(refFromWide(*pc[k]), refFromWide(*qc[k]));
            inRange = inRange && int64InRange(exact);
            d[k] = int64Saturated(exact);
        }
        fixVec3 expected = fixPosWideSub(p, q);
        REQUIRE(sameVec(expected, FIX_LITERAL(fixVec3){d[0], d[1], d[2]}));
        m.begin("sub"), m.pushPosWide(p), m.pushPosWide(q);
        std::string error = m.call(2);
        if (kChecked && !inRange)
        {
            REQUIRE(error == "poswide difference out of local range");
            differenceRaised++;
        }
        else
        {
            REQUIRE_MESSAGE(error.empty(), "poswide - poswide raised '" << error << "'");
            REQUIRE(ttisvec3(m.result()));
            REQUIRE(sameVec(moo_vec3_payload(vec3value(m.result())), expected));
            m.pop();
        }

        // a world transform with a unit rotation by a random angle, and a local transform
        fixQuat rotation = fixMakeQuatFromAxisAngle(fixNormalize(FIX_LITERAL(fixVec3){kOne, 2 * kOne, 3 * kOne}), rng.number() % (kOne * 6));
        fixWorldTransformWide world = {p, rotation};
        fixTransform local = {FIX_LITERAL(fixVec3){v.x / 65536, v.y / 65536, v.z / 65536}, rotation};

        m.begin("mul"), m.pushWorldTransformWide(world), m.pushTransform(local);
        REQUIRE(m.call(2).empty());
        REQUIRE(ttisworldtransformwide(m.result()));
        fixWorldTransformWide composed = moo_worldtransformwide_payload(worldtransformwidevalue(m.result()));
        fixWorldTransformWide expectedComposed = fixMulWorldTransformsWide(world, local);
        REQUIRE(samePos(composed.p, expectedComposed.p));
        REQUIRE(memcmp(&composed.q, &expectedComposed.q, sizeof(fixQuat)) == 0);
        m.pop();

        m.begin("mul"), m.pushWorldTransformWide(world), m.pushVec3(local.p);
        REQUIRE(m.call(2).empty());
        REQUIRE(ttisposwide(m.result()));
        REQUIRE(samePos(moo_poswide_payload(poswidevalue(m.result())), fixTransformWorldPointWide(world, local.p)));
        m.pop();
    }
    if (kChecked)
    {
        CHECK(offsetRaised > 0);
        CHECK(differenceRaised > 0);
    }
}

// The checked-prefix property, on accumulation: release saturates and stays; a checked build raises at the first step
// release saturates, and every value before it is the same bits.
TEST_CASE("AccumulationCheckedIsAPrefixOfRelease")
{
    Moo m;
    struct Run
    {
        fixTime start, step;
    };
    const Run runs[] = {{FIX_TIME_MAX - 5 * FIX_TIME_ONE, FIX_TIME_ONE}, {FIX_TIME_MIN + 3 * FIX_TIME_ONE + 7, -FIX_TIME_ONE},
        {0, fixTime(1) << 61}, {FIX_TIME_MAX - 3, 1}};
    for (const Run& run : runs)
    {
        fixTime t = run.start;
        bool saturated = false;
        for (int i = 0; i < 16; i++)
        {
            Ref exact = refAdd(refFromI64(t), refFromI64(run.step));
            m.begin("add"), m.pushTime(t), m.pushTime(run.step);
            std::string error = m.call(2);
            if (!int64InRange(exact) && kChecked)
            {
                REQUIRE(error == "time overflow in '+'");
                saturated = true;
                break;
            }
            REQUIRE(error.empty());
            fixTime next = moo_time_payload(timevalue(m.result()));
            m.pop();
            REQUIRE(next == int64Saturated(exact));
            if (!int64InRange(exact))
            {
                // release: it saturates and stays
                REQUIRE((next == FIX_TIME_MAX || next == FIX_TIME_MIN));
                saturated = true;
            }
            t = next;
        }
        CHECK(saturated);
    }

    // scaling: wide * number grown until it saturates
    fixedWide_t w = wideBit(100, false);
    bool saturated = false;
    for (int i = 0; i < 64; i++)
    {
        Ref exact = refRoundProduct(refFromWide(w), refFromI64(4 * kOne));
        m.begin("mul"), m.pushWide(w), m.pushNumber(4 * kOne);
        std::string error = m.call(2);
        if (!wideInRange(exact) && kChecked)
        {
            REQUIRE(error == "wide overflow in '*'");
            saturated = true;
            break;
        }
        REQUIRE(error.empty());
        fixedWide_t next = moo_wide_payload(widevalue(m.result()));
        m.pop();
        REQUIRE(sameWide(next, wideSaturated(exact)));
        saturated = saturated || !wideInRange(exact);
        w = next;
    }
    CHECK(saturated);
}

// What layer 3d does not define is the ordinary error, by name: no implicit conversion and no cross-type arithmetic
TEST_CASE("WideOperatorRefusals")
{
    Moo m;
    struct Refusal
    {
        const char* op;
        int left, right; // 0 wide, 1 time, 2 number, 3 poswide, 4 vec3
        const char* error;
    };
    const Refusal refusals[] = {
        {"add", 0, 1, "attempt to perform arithmetic (add) on wide and time"},
        {"add", 2, 0, "attempt to perform arithmetic (add) on number and wide"},
        {"mul", 1, 1, "attempt to perform arithmetic (mul) on time"},
        {"add", 1, 2, "attempt to perform arithmetic (add) on time and number"},
        {"mul", 2, 1, "attempt to perform arithmetic (mul) on number and time"},
        {"add", 3, 3, "attempt to perform arithmetic (add) on poswide"},
        {"add", 4, 3, "attempt to perform arithmetic (add) on vec3 and poswide"},
        {"lt", 0, 1, "attempt to compare wide < time"},
        {"lt", 0, 2, "attempt to compare wide < number"},
        {"lt", 3, 3, "attempt to compare poswide < poswide"},
    };
    for (const Refusal& r : refusals)
    {
        m.begin(r.op);
        const int kinds[2] = {r.left, r.right};
        for (int kind : kinds)
        {
            switch (kind)
            {
            case 0:
                m.pushWide(fixWideFromFixed(kOne));
                break;
            case 1:
                m.pushTime(FIX_TIME_ONE);
                break;
            case 2:
                m.pushNumber(kOne);
                break;
            case 3:
                m.pushPosWide(FIX_LITERAL(fixPosWide){FIX_INT128_ZERO, FIX_INT128_ZERO, FIX_INT128_ZERO});
                break;
            default:
                m.pushVec3(FIX_LITERAL(fixVec3){0, 0, 0});
                break;
            }
        }
        CHECK_MESSAGE(m.call(2) == r.error, r.op << " " << r.left << " " << r.right);
    }

    // 1w ~= 1: no cross-type equality
    m.begin("eq"), m.pushWide(fixWideFromFixed(kOne)), m.pushNumber(kOne);
    REQUIRE(m.call(2).empty());
    CHECK(lua_toboolean(m.L, -1) == 0);
    m.pop();
}

} // namespace wide_w2

TEST_SUITE_END();
