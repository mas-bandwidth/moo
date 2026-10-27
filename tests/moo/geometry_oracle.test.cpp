// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The geometry oracle: vec3, quat and transform operations bit for bit against fixed's and fixed3d's C
// (docs/DESIGN.md "Conformance (layer 3b)").

#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "fixed/fixed.h"
#include "fixed/fixed_int128.h"
#include "fixed/fixed_vec.h"
#include "Luau/Compiler.h"
#include "MooHost.h"
#include "doctest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>

TEST_SUITE_BEGIN("geometry_oracle");

// moo: layer 3b's oracle (docs/DESIGN.md "Conformance (layer 3b)"; docs/history/layer3b/moo-v2-l3b-conformance.md). Every
// vec3, quat and transform operation runs in moo, through a script function called through the C API so nothing
// folds, and in C on the same operands, and the bits must match. The C side is:
//
// - for moo's own operators (vec3 + - * and unary -, and the component products), the layer 3 oracle's
//   independent definitions in tests/moo/moo_oracle_fixed.c, reused by name, component by component;
// - for the compound functions, fixed's function itself (extern/fixed, FIX_SATURATE, as moo builds it), which is
//   what fixed3d's b3* wrappers call;
// - for transform compose, point and invpoint, fixed's rotation and the layer 3 oracle's saturating add (the rule
//   docs/history/layer3b/moo-v2-l3b-transform.md decision 2 states), and, wherever fixed's own plain add does not overflow,
//   fixed's fixMulTransforms / fixTransformPoint / fixInvTransformPoint as well;
// - for vec3.dot, fixDot, and an independent exact dot product written here, which names the one place fixed's own
//   result is not the exact rounded one (the sum of squares wraps past 2^127; an upstream question, not a moo bug).
//
// Inputs are dense: random operands across the full range, axis-aligned and degenerate vectors, the saturation
// edges, unit and near-unit quaternions, exact half turns, and alpha at 0, 1/2, 1 and random. Compound functions that
// are plain int64 sums inside (cross, rotate, and the transforms built on rotate) take operands inside fixed's
// defined range. 2^14 cases per operation; MOO_ORACLE_3B_N sets the count (the nightly runs 2^22). One summary line is printed.
extern "C"
{
    int64_t moo_oracle_add(int64_t a, int64_t b);
    int64_t moo_oracle_sub(int64_t a, int64_t b);
    int64_t moo_oracle_unm(int64_t a);
    int64_t moo_oracle_mul(int64_t a, int64_t b);
}


namespace
{

const fixed_t kOne = MOO_NUMBER_ONE;
const fixed_t kMax = MOO_NUMBER_MAX;
const fixed_t kMin = MOO_NUMBER_MIN;
const fixed_t kInRange = fixed_t(1) << 40; // 2^24 units: no int64 sum inside fixed's rotations overflows

// MOO_ORACLE_3B_N, or 2^14. Its own variable, not the layer 3 oracle's MOO_ORACLE_N: the nightly runs that one at
// 2^24 pairs of cheap number operations, and 2^24 cases of each of these 22 object operations would take hours.
uint64_t caseCount()
{
    const char* text = getenv("MOO_ORACLE_3B_N");
    if (!text)
        return uint64_t(1) << 14;
    char* end = nullptr;
    unsigned long long n = strtoull(text, &end, 10);
    REQUIRE_MESSAGE((*end == 0 && n > 0 && text[0] != '-'), "MOO_ORACLE_3B_N must be a positive decimal count, not '" << text << "'");
    return uint64_t(n);
}

// splitmix64 with a fixed seed per operation: the same inputs on every platform
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

    fixed_t within(fixed_t bound)
    {
        return fixed_t(next() % uint64_t(2 * bound + 1)) - bound;
    }

    // the full range: an edge one time in four, otherwise a random magnitude (a random bit length) and sign
    fixed_t full()
    {
        static const fixed_t edges[] = {0, 1, -1, kOne, -kOne, kOne / 2, fixed_t(1) << 47, -(fixed_t(1) << 47), kMax, kMin,
            kMax - 1, kMin + 1};
        uint64_t r = next();
        if ((r & 3) == 0)
            return edges[(r >> 2) % (sizeof(edges) / sizeof(edges[0]))];
        int64_t v = int64_t(next() >> (1 + (r >> 2) % 63));
        return (r & 0x100) ? -v : v;
    }

    // full-range components, or one of the degenerate shapes: zero, axis-aligned, two equal components
    fixVec3 vec(fixed_t (Rng::*component)())
    {
        uint64_t shape = next() % 8;
        fixed_t a = (this->*component)(), b = (this->*component)(), c = (this->*component)();
        switch (shape)
        {
        case 0:
            return FIX_LITERAL(fixVec3){0, 0, 0};
        case 1:
            return FIX_LITERAL(fixVec3){a, 0, 0};
        case 2:
            return FIX_LITERAL(fixVec3){0, a, 0};
        case 3:
            return FIX_LITERAL(fixVec3){0, 0, a};
        case 4:
            return FIX_LITERAL(fixVec3){a, a, b};
        default:
            return FIX_LITERAL(fixVec3){a, b, c};
        }
    }

    fixed_t bounded()
    {
        return within(kInRange);
    }

    fixVec3 fullVec()
    {
        return vec(&Rng::full);
    }

    fixVec3 inRangeVec()
    {
        return vec(&Rng::bounded);
    }

    fixQuat fullQuat()
    {
        return FIX_LITERAL(fixQuat){{full(), full(), full()}, full()};
    }

    // a unit quaternion through fixNormalizeQuat, a near-unit one (a few raw units off each component), the
    // identity, or an exact half turn about an axis
    fixQuat rotation()
    {
        uint64_t shape = next() % 8;
        if (shape == 0)
            return fixQuat_identity;
        if (shape == 1)
        {
            static const fixQuat halves[] = {{{kOne, 0, 0}, 0}, {{0, kOne, 0}, 0}, {{0, 0, kOne}, 0}, {{0, 0, 0}, -kOne}};
            return halves[next() % 4];
        }
        fixQuat q = fixNormalizeQuat(FIX_LITERAL(fixQuat){{within(kOne), within(kOne), within(kOne)}, within(kOne)});
        if (shape == 2)
        {
            q.v.x += within(8);
            q.v.y += within(8);
            q.v.z += within(8);
            q.s += within(8);
        }
        return q;
    }

    fixed_t alpha()
    {
        uint64_t shape = next() % 6;
        if (shape == 0)
            return 0;
        if (shape == 1)
            return kOne / 2;
        if (shape == 2)
            return kOne;
        return fixed_t(next() % uint64_t(kOne + 1));
    }

    fixTransform rigid()
    {
        fixTransform t;
        t.p = inRangeVec();
        t.q = rotation();
        return t;
    }

    // the identity rotation with full-range positions: the rotation is exact, so the one position add is all that
    // can saturate, and it does at the edges
    fixTransform translationFull()
    {
        fixTransform t;
        t.p = fullVec();
        t.q = fixQuat_identity;
        return t;
    }
};

// Values on the Lua stack and back, raw components through the C API
void push(lua_State* L, fixVec3 v)
{
    moo_pushvec3(L, v.x, v.y, v.z);
}

void push(lua_State* L, fixQuat q)
{
    moo_pushquat(L, q.v.x, q.v.y, q.v.z, q.s);
}

void push(lua_State* L, fixTransform t)
{
    moo_pushtransform(L, t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s);
}

void push(lua_State* L, fixed_t n)
{
    lua_pushnumber(L, n);
}

bool pop(lua_State* L, fixVec3& v)
{
    lua_Number raw[3];
    bool ok = moo_tovec3(L, -1, raw) == 1;
    lua_pop(L, 1);
    v = FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
    return ok;
}

bool pop(lua_State* L, fixQuat& q)
{
    lua_Number raw[4];
    bool ok = moo_toquat(L, -1, raw) == 1;
    lua_pop(L, 1);
    q = FIX_LITERAL(fixQuat){{raw[0], raw[1], raw[2]}, raw[3]};
    return ok;
}

bool pop(lua_State* L, fixTransform& t)
{
    lua_Number raw[7];
    bool ok = moo_totransform(L, -1, raw) == 1;
    lua_pop(L, 1);
    t = FIX_LITERAL(fixTransform){{raw[0], raw[1], raw[2]}, {{raw[3], raw[4], raw[5]}, raw[6]}};
    return ok;
}

bool pop(lua_State* L, fixed_t& n)
{
    bool ok = lua_isnumber(L, -1) != 0;
    n = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

bool same(fixVec3 a, fixVec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool same(fixQuat a, fixQuat b)
{
    return same(a.v, b.v) && a.s == b.s;
}

bool same(fixTransform a, fixTransform b)
{
    return same(a.p, b.p) && same(a.q, b.q);
}

bool same(fixed_t a, fixed_t b)
{
    return a == b;
}

std::string show(fixed_t n)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return buf;
}

std::string show(fixVec3 v)
{
    return "(" + show(v.x) + ", " + show(v.y) + ", " + show(v.z) + ")";
}

std::string show(fixQuat q)
{
    return "(" + show(q.v.x) + ", " + show(q.v.y) + ", " + show(q.v.z) + "; " + show(q.s) + ")";
}

std::string show(fixTransform t)
{
    return "[" + show(t.p) + " " + show(t.q) + "]";
}

// moo's own saturating component operations, from the layer 3 oracle's definitions
fixVec3 oracleAdd(fixVec3 a, fixVec3 b)
{
    return FIX_LITERAL(fixVec3){moo_oracle_add(a.x, b.x), moo_oracle_add(a.y, b.y), moo_oracle_add(a.z, b.z)};
}

fixVec3 oracleSub(fixVec3 a, fixVec3 b)
{
    return FIX_LITERAL(fixVec3){moo_oracle_sub(a.x, b.x), moo_oracle_sub(a.y, b.y), moo_oracle_sub(a.z, b.z)};
}

// true when the exact sum lies in the domain [-INT64_MAX, INT64_MAX], so fixed's plain int64 add (fixVecAdd,
// fixVecSub) is defined and needs no saturation (INT64_MIN fits int64 but is outside the domain, where moo
// saturates); in fixed's 128-bit arithmetic, so the test needs no compiler builtin (Moo.Test also builds with MSVC)
bool fitsInt64(fixInt128 x)
{
    return !fixInt128Gt(x, fixInt128FromI64(INT64_MAX)) && !fixInt128Lt(x, fixInt128FromI64(-INT64_MAX));
}

bool addInRange(fixVec3 a, fixVec3 b)
{
    return fitsInt64(fixInt128Add(fixInt128FromI64(a.x), fixInt128FromI64(b.x))) &&
           fitsInt64(fixInt128Add(fixInt128FromI64(a.y), fixInt128FromI64(b.y))) &&
           fitsInt64(fixInt128Add(fixInt128FromI64(a.z), fixInt128FromI64(b.z)));
}

bool subInRange(fixVec3 a, fixVec3 b)
{
    return fitsInt64(fixInt128Sub(fixInt128FromI64(a.x), fixInt128FromI64(b.x))) &&
           fitsInt64(fixInt128Sub(fixInt128FromI64(a.y), fixInt128FromI64(b.y))) &&
           fitsInt64(fixInt128Sub(fixInt128FromI64(a.z), fixInt128FromI64(b.z)));
}

// true when a component product saturates, so a checked build raises "number overflow in '*'" (docs/DESIGN.md
// "Overflow"); moo_nummulsaturates is checked against an exact 128-bit reference in tests/moo/number.test.cpp
bool mulSaturates(fixVec3 a, fixVec3 b)
{
    return moo_nummulsaturates(a.x, b.x) || moo_nummulsaturates(a.y, b.y) || moo_nummulsaturates(a.z, b.z);
}

bool scaleSaturates(fixed_t s, fixVec3 a)
{
    return moo_nummulsaturates(s, a.x) || moo_nummulsaturates(s, a.y) || moo_nummulsaturates(s, a.z);
}

// The exact dot product, rounded once half up and saturated into the domain, written without fixDot: the three
// 126-bit products are summed in 128 bits with the overflow of the last add detected by sign, which only a sum
// whose magnitude reaches 2^127 can do, and which then saturates.
fixed_t exactDot(fixVec3 a, fixVec3 b, bool* wrapped)
{
    fixInt128 s = fixInt128Add(fixInt128MulI64(a.x, b.x), fixInt128MulI64(a.y, b.y));
    fixInt128 p = fixInt128MulI64(a.z, b.z);
    fixInt128 t = fixInt128Add(s, p);
    bool sNeg = fixInt128IsNegative(s), pNeg = fixInt128IsNegative(p), tNeg = fixInt128IsNegative(t);
    *wrapped = sNeg == pNeg && tNeg != sNeg;
    if (*wrapped)
        return sNeg ? kMin : kMax;
    fixInt128 r = fixInt128Shr(fixInt128Add(t, fixInt128FromI64(kOne / 2)), 16);
    if (fixInt128Gt(r, fixInt128FromI64(INT64_MAX)))
        return kMax;
    if (fixInt128Lt(r, fixInt128FromI64(-INT64_MAX)))
        return kMin;
    return fixInt128ToI64(r);
}

struct Oracle
{
    lua_State* L;
    uint64_t n;
    int ops = 0;
    uint64_t cases = 0;
    uint64_t mismatches = 0;
    uint64_t alsoFixed = 0;    // transform cases also compared with fixed's own plain-add function
    uint64_t saturated = 0;    // transform cases where the position add saturated
    uint64_t dotWraps = 0;     // inputs where fixDot's 128-bit sum wraps and the exact dot differs
    uint64_t dotWrapsOther = 0; // inputs where fixDot differs from the exact dot without a wrap (must be 0)
    bool quiet = false;         // count mismatches without failing: the reversed witness expects them
    std::string error;          // the last error a script function raised
    std::map<std::string, uint64_t> mismatchesByOp;

    explicit Oracle(uint64_t count, bool quietRun = false)
        : n(count)
        , quiet(quietRun)
    {
        L = luaL_newstate();
        REQUIRE(L != nullptr);
        luaL_openlibs(L);
        std::string bytecode = Luau::compile(R"(
            function add(a, b) return a + b end
            function sub(a, b) return a - b end
            function mul(a, b) return a * b end
            function unm(a) return -a end
            function eq(a, b) if a == b then return 1 else return 0 end end
            function dot(a, b) return vec3.dot(a, b) end
            function cross(a, b) return vec3.cross(a, b) end
            function length(a) return vec3.length(a) end
            function normalize(a) return vec3.normalize(a) end
            function conjugate(q) return quat.conjugate(q) end
            function nlerp(a, b, t) return quat.nlerp(a, b, t) end
            function inverse(t) return transform.inverse(t) end
            function point(t, v) return transform.point(t, v) end
            function direction(t, v) return transform.direction(t, v) end
            function invpoint(t, v) return transform.invpoint(t, v) end
            function invdirection(t, v) return transform.invdirection(t, v) end
        )");
        REQUIRE(luau_load(L, "=moo-3b-oracle", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }

    ~Oracle()
    {
        lua_close(L);
    }

    // replaces script functions by name, so the reversed witness can plant a wrong operation
    void plant(const char* source)
    {
        std::string bytecode = Luau::compile(source);
        REQUIRE(luau_load(L, "=moo-3b-plant", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }

    template<typename R, typename... A>
    bool run(const char* fn, R& out, A... args)
    {
        lua_getglobal(L, fn);
        (push(L, args), ...);
        if (lua_pcall(L, int(sizeof...(A)), 1, 0) != 0)
        {
            error = lua_isstring(L, -1) ? lua_tostring(L, -1) : "?";
            lua_pop(L, 1);
            return false;
        }
        return pop(L, out);
    }

    // one comparison: moo's result for fn on args against want
    template<typename R, typename... A>
    void check(const char* name, const char* fn, R want, A... args)
    {
        compare(name, fn, want, false, args...);
    }

    // a case that release saturates: want in release, and in a checked build the named error instead
    // (docs/DESIGN.md "Overflow")
    template<typename R, typename... A>
    void checkSaturating(const char* name, const char* fn, R want, bool saturates, A... args)
    {
        compare(name, fn, want, MOO_OVERFLOW_ERRORS && saturates, args...);
    }

    template<typename R, typename... A>
    void compare(const char* name, const char* fn, R want, bool raises, A... args)
    {
        R got{};
        error.clear();
        bool ok = run(fn, got, args...);
        ++cases;
        bool good = raises ? !ok && error.find("number overflow in '") != std::string::npos : ok && same(got, want);
        if (!good)
        {
            ++mismatchesByOp[name];
            if (mismatches++ < 8 && !quiet)
            {
                std::string message = std::string(name) + ": moo " + (ok ? show(got) : "raised " + error) + " want " +
                                      (raises ? std::string("number overflow") : show(want));
                ((message += " " + show(args)), ...);
                FAIL_CHECK(message);
            }
        }
    }

    template<typename F>
    void op(F&& body)
    {
        ++ops;
        for (uint64_t i = 0; i < n; ++i)
            body(i);
    }
};

} // namespace

// every operation of the three types, n cases each
static void runAll(Oracle& o)
{
    // vec3 + - * and unary -, moo's own operators, component by component, over the full range
    {
        Rng r{0x3b0001};
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.checkSaturating("vec3 + vec3", "add", oracleAdd(a, b), !addInRange(a, b), a, b);
        });
    }
    {
        Rng r{0x3b0002};
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.checkSaturating("vec3 - vec3", "sub", oracleSub(a, b), !subInRange(a, b), a, b);
        });
    }
    {
        Rng r{0x3b0003};
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            fixVec3 want = {moo_oracle_mul(a.x, b.x), moo_oracle_mul(a.y, b.y), moo_oracle_mul(a.z, b.z)};
            o.checkSaturating("vec3 * vec3", "mul", want, mulSaturates(a, b), a, b);
        });
    }
    {
        Rng r{0x3b0004};
        o.op([&](uint64_t i) {
            fixVec3 a = r.fullVec();
            fixed_t s = r.full();
            fixVec3 want = {moo_oracle_mul(s, a.x), moo_oracle_mul(s, a.y), moo_oracle_mul(s, a.z)};
            if (i & 1)
                o.checkSaturating("vec3 * number", "mul", want, scaleSaturates(s, a), a, s);
            else
                o.checkSaturating("number * vec3", "mul", want, scaleSaturates(s, a), s, a);
        });
    }
    {
        Rng r{0x3b0005};
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec();
            fixVec3 want = {moo_oracle_unm(a.x), moo_oracle_unm(a.y), moo_oracle_unm(a.z)};
            o.check("-vec3", "unm", want, a);
        });
    }
    {
        Rng r{0x3b0006};
        o.op([&](uint64_t i) {
            fixVec3 a = r.fullVec();
            fixVec3 b = (i % 3 == 0) ? a : r.fullVec();
            if (i % 3 == 1)
                b.y = a.y + (a.y < kMax ? 1 : -1); // one component one raw unit off
            o.check("vec3 == vec3", "eq", fixed_t(same(a, b) ? kOne : 0), a, b);
        });
    }

    // vec3 library: fixed's functions; dot also against the exact dot product
    {
        Rng r{0x3b0007};
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            fixed_t want = fixDot(a, b);
            o.check("vec3.dot", "dot", want, a, b);
            bool wrapped = false;
            fixed_t exact = exactDot(a, b, &wrapped);
            if (exact != want)
                ++(wrapped ? o.dotWraps : o.dotWrapsOther);
        });
    }
    {
        Rng r{0x3b0008};
        o.op([&](uint64_t) {
            fixVec3 a = r.inRangeVec(), b = r.inRangeVec();
            o.check("vec3.cross", "cross", fixCross(a, b), a, b);
        });
    }
    {
        Rng r{0x3b0009};
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec();
            o.check("vec3.length", "length", fixLength(a), a);
        });
    }
    {
        Rng r{0x3b000a};
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec();
            o.check("vec3.normalize", "normalize", fixNormalize(a), a);
        });
    }

    // quat
    {
        Rng r{0x3b000b};
        o.op([&](uint64_t i) {
            fixQuat a = (i & 1) ? r.rotation() : r.fullQuat();
            fixQuat b = (i & 1) ? r.rotation() : r.fullQuat();
            o.check("quat * quat", "mul", fixMulQuat(a, b), a, b);
        });
    }
    {
        Rng r{0x3b000c};
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            fixVec3 v = r.inRangeVec();
            o.check("quat * vec3", "mul", fixRotateVector(q, v), q, v);
        });
    }
    {
        Rng r{0x3b000d};
        o.op([&](uint64_t) {
            fixQuat q = r.fullQuat();
            o.check("quat.conjugate", "conjugate", fixConjugate(q), q);
        });
    }
    {
        Rng r{0x3b000e};
        o.op([&](uint64_t) {
            fixQuat a = r.rotation(), b = r.rotation();
            fixed_t t = r.alpha();
            o.check("quat.nlerp", "nlerp", fixNLerp(a, b, t), a, b, t);
        });
    }
    {
        Rng r{0x3b000f};
        o.op([&](uint64_t i) {
            fixQuat a = r.fullQuat();
            fixQuat b = (i & 1) ? a : r.fullQuat();
            o.check("quat == quat", "eq", fixed_t(same(a, b) ? kOne : 0), a, b);
        });
    }

    // transform: rigid transforms inside fixed's range, and identity rotations with full-range positions
    auto transformPair = [](Rng& r, uint64_t i, fixTransform& a, fixTransform& b) {
        if (i % 4 == 3)
        {
            a = r.translationFull();
            b = r.translationFull();
        }
        else
        {
            a = r.rigid();
            b = r.rigid();
        }
    };
    {
        Rng r{0x3b0010};
        o.op([&](uint64_t i) {
            fixTransform a, b;
            transformPair(r, i, a, b);
            fixVec3 rotated = fixRotateVector(a.q, b.p);
            fixTransform want = {oracleAdd(rotated, a.p), fixMulQuat(a.q, b.q)};
            if (addInRange(rotated, a.p))
            {
                ++o.alsoFixed;
                CHECK(same(want, fixMulTransforms(a, b)));
            }
            else
                ++o.saturated;
            o.check("transform * transform", "mul", want, a, b);
        });
    }
    {
        Rng r{0x3b0011};
        o.op([&](uint64_t i) {
            fixTransform t, u;
            transformPair(r, i, t, u);
            fixVec3 v = (i % 4 == 3) ? r.fullVec() : r.inRangeVec();
            fixVec3 rotated = fixRotateVector(t.q, v);
            fixVec3 want = oracleAdd(rotated, t.p);
            if (addInRange(rotated, t.p))
            {
                ++o.alsoFixed;
                CHECK(same(want, fixTransformPoint(t, v)));
            }
            else
                ++o.saturated;
            o.check(i & 1 ? "transform * vec3" : "transform.point", i & 1 ? "mul" : "point", want, t, v);
        });
    }
    {
        Rng r{0x3b0012};
        o.op([&](uint64_t i) {
            fixTransform t, u;
            transformPair(r, i, t, u);
            fixVec3 v = (i % 4 == 3) ? r.fullVec() : r.inRangeVec();
            fixVec3 want = fixInvRotateVector(t.q, oracleSub(v, t.p));
            if (subInRange(v, t.p))
            {
                ++o.alsoFixed;
                CHECK(same(want, fixInvTransformPoint(t, v)));
            }
            else
                ++o.saturated;
            o.check("transform.invpoint", "invpoint", want, t, v);
        });
    }
    {
        Rng r{0x3b0013};
        o.op([&](uint64_t) {
            fixTransform t = r.rigid();
            fixVec3 v = r.inRangeVec();
            o.check("transform.direction", "direction", fixRotateVector(t.q, v), t, v);
        });
    }
    {
        Rng r{0x3b0014};
        o.op([&](uint64_t) {
            fixTransform t = r.rigid();
            fixVec3 v = r.inRangeVec();
            o.check("transform.invdirection", "invdirection", fixInvRotateVector(t.q, v), t, v);
        });
    }
    {
        Rng r{0x3b0015};
        o.op([&](uint64_t i) {
            fixTransform t = (i % 4 == 3) ? r.translationFull() : r.rigid();
            o.check("transform.inverse", "inverse", fixInvertTransform(t), t);
        });
    }
    {
        Rng r{0x3b0016};
        o.op([&](uint64_t i) {
            fixTransform a = r.rigid();
            fixTransform b = (i & 1) ? a : r.rigid();
            o.check("transform == transform", "eq", fixed_t(same(a, b) ? kOne : 0), a, b);
        });
    }

}

TEST_CASE("GeometryOracle")
{
    Oracle o(caseCount());
    runAll(o);

    printf("moo-3b-oracle ops=%d cases=%llu per-op=%llu mismatches=%llu transform-also-fixed=%llu transform-saturated=%llu "
           "fixdot-wraps=%llu\n",
        o.ops, (unsigned long long)o.cases, (unsigned long long)o.n, (unsigned long long)o.mismatches,
        (unsigned long long)o.alsoFixed, (unsigned long long)o.saturated, (unsigned long long)o.dotWraps);

    CHECK(o.mismatches == 0);
    CHECK(o.ops == 22);
    // the corpus reaches both regimes of the transform oracle
    CHECK(o.alsoFixed > 0);
    CHECK(o.saturated > 0);
    // fixDot is the exact rounded dot product everywhere: since fixed #59 its 128-bit sum saturates under FIX_SATURATE
    // instead of wrapping, so the wraps the oracle used to count at the top of the range are gone
    CHECK(o.dotWraps == 0);
    CHECK(o.dotWrapsOther == 0);
}

// The reversed witness: the gate must go red when an operation is wrong, and name that operation and no other. Each
// plant replaces one script function with a wrong one; the subtle ones are one raw unit off or differ only off the
// identity rotation. Unplanted, the same small run has no mismatch, so the red comes from the plant.
TEST_CASE("GeometryOracleGoesRedOnAPlantedWrongOp")
{
    const uint64_t n = 256;
    {
        Oracle o(n, /* quiet= */ true);
        runAll(o);
        CHECK(o.ops == 22);
        CHECK(o.mismatches == 0);
    }

    struct Plant
    {
        const char* op;
        const char* source;
    };
    const Plant plants[] = {
        {"vec3 + vec3", "function add(a, b) return a + b + vec3.new(0, 0, 1 / 65536) end"},
        {"vec3.dot", "function dot(a, b) return vec3.dot(a, b) + 1 / 65536 end"},
        {"quat.nlerp", "function nlerp(a, b, t) return quat.nlerp(b, a, t) end"},
        {"transform.direction", "function direction(t, v) return transform.invdirection(t, v) end"},
        {"transform.point", "function point(t, v) return transform.direction(t, v) end"},
    };
    for (const Plant& plant : plants)
    {
        Oracle o(n, /* quiet= */ true);
        o.plant(plant.source);
        runAll(o);
        INFO("planted: " << std::string(plant.source));
        CHECK(o.mismatches > 0);
        CHECK(o.mismatchesByOp[plant.op] == o.mismatches);
    }
}

// The difference from exact arithmetic the oracle used to name, fixed upstream (fixed #59): the dot product of a
// vector with itself at the top of the range has a sum of squares of about 3 * 2^126, past fixDotRaw's 128 bits.
// fixDot (and vec3.dot) returned the most negative number there; under FIX_SATURATE it now saturates to the most
// positive, as the exact result does. vec3.length was never affected: it reads the same sum as unsigned.
TEST_CASE("GeometryFixDotSaturatesAtTheRangeEnd")
{
    fixVec3 top = {kMax, kMax, kMax};
    bool wrapped = false;
    CHECK(exactDot(top, top, &wrapped) == kMax);
    CHECK(wrapped);
    CHECK(fixDot(top, top) == kMax);
    fixVec3 bottom = {kMin, kMin, kMin};
    CHECK(fixDot(top, bottom) == kMin);
    CHECK(fixLength(top) == kMax);

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    CHECK(mooRun(L, "local v = vec3.new(140737488355327, 140737488355327, 140737488355327) return vec3.dot(v, v) > 0") == "true");
    lua_close(L);

    // just below the wrap the exact and fixed's results agree
    fixVec3 half = {fixed_t(1) << 62, fixed_t(1) << 62, 0};
    CHECK(exactDot(half, half, &wrapped) == fixDot(half, half));
    CHECK(!wrapped);
}

TEST_SUITE_END();
