// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The completed libraries of layer 3c: the rest of fixed's functions on number, vec3, quat and transform, each bit for
// bit against fixed's C on the same inputs (docs/DESIGN.md "Layer 3c as built: the completed libraries").

#include "lua.h"
#include "lualib.h"
#include "moo_number.h"
#include "fixed/fixed.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed_quantize.h"
#include "fixed/fixed_vec.h"
#include "Luau/Compiler.h"
#include "MooHost.h"
#include "doctest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <utility>

TEST_SUITE_BEGIN("complete");

// moo: layer 3c's oracle for the completed libraries (docs/DESIGN.md "Layer 3c as built"). Every function the layer
// adds runs in moo, through a script function called through the C API so nothing folds, and the fixed C function it
// wraps runs on the same operands; the bits must match. Where moo refuses an argument by name (an alpha outside
// [0, 1], a non-unit axis, a raw value outside fixWiden's asserted range) the C side predicts the refusal from the
// same predicate and the error message is what must match. None of these functions is one of moo's own operators, so
// no case depends on MOO_OVERFLOW_ERRORS: release and checked builds give the same bits (the layer 3b rule for
// compound functions).
//
// Inputs are an edge set (zero, one raw unit, one, a half, 2^47, the range ends, pi and its multiples for the angles)
// mixed with PCG32 draws of random bit length and sign; vectors take the 3b oracle's degenerate shapes, quaternions
// are unit, near-unit, identity, exact half turns or full-range. 2^12 cases per function; MOO_ORACLE_3C_N sets the
// count (the nightly raises it). One summary line is printed.

namespace
{

const fixed_t kOne = MOO_NUMBER_ONE;
const fixed_t kMax = MOO_NUMBER_MAX;
const fixed_t kMin = MOO_NUMBER_MIN;
const fixed_t kInRange = fixed_t(1) << 40; // 2^24 units: no int64 sum inside fixed's rotations overflows
const fixed_t kPi = FIX_PI;

uint64_t caseCount()
{
    const char* text = getenv("MOO_ORACLE_3C_N");
    if (!text)
        return uint64_t(1) << 12;
    char* end = nullptr;
    unsigned long long n = strtoull(text, &end, 10);
    REQUIRE_MESSAGE((*end == 0 && n > 0 && text[0] != '-'), "MOO_ORACLE_3C_N must be a positive decimal count, not '" << text << "'");
    return uint64_t(n);
}

// PCG32 (O'Neill, pcg32_random_r), a fixed seed and stream per function: the same inputs on every platform
struct Rng
{
    uint64_t state = 0;
    uint64_t inc = 0;

    explicit Rng(uint64_t stream)
    {
        inc = (stream << 1) | 1;
        next32();
        state += 0x3c0000c0ffee;
        next32();
    }

    uint32_t next32()
    {
        uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        uint32_t xorshifted = uint32_t(((old >> 18) ^ old) >> 27);
        uint32_t rot = uint32_t(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
    }

    uint64_t next()
    {
        return (uint64_t(next32()) << 32) | next32();
    }

    fixed_t within(fixed_t bound)
    {
        return fixed_t(next() % uint64_t(2 * bound + 1)) - bound;
    }

    // the full range: an edge one time in four, otherwise a random bit length and sign
    fixed_t full()
    {
        static const fixed_t edges[] = {0, 1, -1, kOne, -kOne, kOne / 2, -kOne / 2, fixed_t(1) << 47, -(fixed_t(1) << 47), kMax,
            kMin, kMax - 1, kMin + 1, kPi, -kPi, 2 * kPi, -2 * kPi, kPi / 2};
        uint64_t r = next();
        if ((r & 3) == 0)
            return edges[(r >> 2) % (sizeof(edges) / sizeof(edges[0]))];
        int64_t v = int64_t(next() >> (1 + (r >> 2) % 63));
        return (r & 0x100) ? -v : v;
    }

    fixed_t bounded()
    {
        return within(kInRange);
    }

    // alpha: 0, 1/2, 1, inside [0, 1], or outside it (the refused case) one time in eight
    fixed_t alpha()
    {
        uint64_t shape = next() % 8;
        if (shape == 0)
            return 0;
        if (shape == 1)
            return kOne / 2;
        if (shape == 2)
            return kOne;
        if (shape == 3)
            return (next() & 1) ? kOne + 1 + fixed_t(next() % (uint64_t(kOne) * 4)) : -1 - fixed_t(next() % (uint64_t(kOne) * 4));
        return fixed_t(next() % uint64_t(kOne + 1));
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

    fixVec3 fullVec()
    {
        return vec(&Rng::full);
    }

    fixVec3 inRangeVec()
    {
        return vec(&Rng::bounded);
    }

    // a unit vector through fixNormalize (an axis), a near-unit one, an exact axis, or a vector that is not a unit
    // (the refused case) one time in eight
    fixVec3 unitVec()
    {
        uint64_t shape = next() % 8;
        if (shape == 0)
        {
            static const fixVec3 axes[] = {{kOne, 0, 0}, {0, kOne, 0}, {0, 0, kOne}, {-kOne, 0, 0}, {0, -kOne, 0}, {0, 0, -kOne}};
            return axes[next() % 6];
        }
        if (shape == 1)
            return fullVec();
        fixVec3 v = fixNormalize(FIX_LITERAL(fixVec3){within(kOne), within(kOne), within(kOne)});
        if (shape == 2)
        {
            v.x += within(8);
            v.y += within(8);
            v.z += within(8);
        }
        return v;
    }

    fixQuat fullQuat()
    {
        return FIX_LITERAL(fixQuat){{full(), full(), full()}, full()};
    }

    // a unit quaternion through fixNormalizeQuat, a near-unit one, the identity, an exact half turn, or a full-range
    // quaternion one time in eight
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
        if (shape == 2)
            return fullQuat();
        fixQuat q = fixNormalizeQuat(FIX_LITERAL(fixQuat){{within(kOne), within(kOne), within(kOne)}, within(kOne)});
        if (shape == 3)
        {
            q.v.x += within(8);
            q.v.y += within(8);
            q.v.z += within(8);
            q.s += within(8);
        }
        return q;
    }

    fixTransform rigid()
    {
        fixTransform t;
        t.p = inRangeVec();
        t.q = rotation();
        return t;
    }

    // a full-range position with a rotation: the position adds saturate at the edges
    fixTransform wide()
    {
        fixTransform t;
        t.p = fullVec();
        t.q = rotation();
        return t;
    }

    // a time: positive and in range mostly, zero or negative one time in eight (fixed's early returns)
    fixed_t duration()
    {
        uint64_t shape = next() % 8;
        if (shape == 0)
            return 0;
        if (shape == 1)
            return -fixed_t(next() % uint64_t(kOne * 100));
        return 1 + fixed_t(next() % uint64_t(kOne * 100));
    }

    int shift()
    {
        return int(next() % 63);
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

void push(lua_State* L, int n)
{
    lua_pushnumber(L, moo_numfromint(n));
}

// a script boolean, read as a value
struct Flag
{
    bool value = false;
};

template<typename T>
struct Count
{
    static const int results = 1;
};

template<typename A, typename B>
struct Count<std::pair<A, B>>
{
    static const int results = 2;
};

bool popOne(lua_State* L, int idx, fixVec3& v)
{
    lua_Number raw[3];
    bool ok = moo_tovec3(L, idx, raw) == 1;
    v = FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
    return ok;
}

bool popOne(lua_State* L, int idx, fixQuat& q)
{
    lua_Number raw[4];
    bool ok = moo_toquat(L, idx, raw) == 1;
    q = FIX_LITERAL(fixQuat){{raw[0], raw[1], raw[2]}, raw[3]};
    return ok;
}

bool popOne(lua_State* L, int idx, fixTransform& t)
{
    lua_Number raw[7];
    bool ok = moo_totransform(L, idx, raw) == 1;
    t = FIX_LITERAL(fixTransform){{raw[0], raw[1], raw[2]}, {{raw[3], raw[4], raw[5]}, raw[6]}};
    return ok;
}

bool popOne(lua_State* L, int idx, fixed_t& n)
{
    bool ok = lua_isnumber(L, idx) != 0;
    n = lua_tonumber(L, idx);
    return ok;
}

bool popOne(lua_State* L, int idx, Flag& f)
{
    bool ok = lua_isboolean(L, idx) != 0;
    f.value = lua_toboolean(L, idx) != 0;
    return ok;
}

template<typename R>
bool pop(lua_State* L, R& out)
{
    bool ok = popOne(L, -1, out);
    lua_pop(L, 1);
    return ok;
}

template<typename A, typename B>
bool pop(lua_State* L, std::pair<A, B>& out)
{
    bool ok = popOne(L, -2, out.first) && popOne(L, -1, out.second);
    lua_pop(L, 2);
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

bool same(Flag a, Flag b)
{
    return a.value == b.value;
}

template<typename A, typename B>
bool same(const std::pair<A, B>& a, const std::pair<A, B>& b)
{
    return same(a.first, b.first) && same(a.second, b.second);
}

std::string show(fixed_t n)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return buf;
}

std::string show(int n)
{
    return show(fixed_t(n)) + "i";
}

std::string show(Flag f)
{
    return f.value ? "true" : "false";
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

template<typename A, typename B>
std::string show(const std::pair<A, B>& p)
{
    return "{" + show(p.first) + ", " + show(p.second) + "}";
}

Flag flag(bool b)
{
    return Flag{b};
}

struct Oracle
{
    lua_State* L;
    uint64_t n;
    int ops = 0;
    uint64_t cases = 0;
    uint64_t refused = 0;    // cases where moo refused an argument by name, as predicted
    uint64_t mismatches = 0;
    bool quiet = false;      // count mismatches without failing: the reversed witness expects them
    std::string error;       // the last error a script function raised
    std::map<std::string, uint64_t> mismatchesByOp;

    explicit Oracle(uint64_t count, bool quietRun = false)
        : n(count)
        , quiet(quietRun)
    {
        L = luaL_newstate();
        REQUIRE(L != nullptr);
        luaL_openlibs(L);
        std::string bytecode = Luau::compile(R"(
            function muldiv(a, b, c) return math.muldiv(a, b, c) end
            function halfsum(a, b) return math.halfsum(a, b) end
            function halfdifference(a, b) return math.halfdifference(a, b) end
            function unwindangle(a) return math.unwindangle(a) end
            function cossin(a) return math.cossin(a) end
            function smooth(c, t, v, s, dt) return math.smoothcriticallydamped(c, t, v, s, dt) end
            function smoothupdown(c, t, v, su, sd, dt) return math.smoothcriticallydampedupdown(c, t, v, su, sd, dt) end
            function fits(a, lo, hi) return math.fits(a, lo, hi) end
            function narrow(a, s) return math.narrow(a, s) end
            function widen(a, s) return math.widen(a, s) end

            function distance(a, b) return vec3.distance(a, b) end
            function distancesquared(a, b) return vec3.distancesquared(a, b) end
            function lengthsquared(a) return vec3.lengthsquared(a) end
            function vlerp(a, b, t) return vec3.lerp(a, b, t) end
            function vmin(a, b) return vec3.min(a, b) end
            function vmax(a, b) return vec3.max(a, b) end
            function vclamp(a, lo, hi) return vec3.clamp(a, lo, hi) end
            function vabs(a) return vec3.abs(a) end
            function vsign(a) return vec3.sign(a) end
            function perp(a) return vec3.perp(a) end
            function muladd(a, s, b) return vec3.muladd(a, s, b) end
            function mulsub(a, s, b) return vec3.mulsub(a, s, b) end
            function blend2(s, a, t, b) return vec3.blend2(s, a, t, b) end
            function safescale(a) return vec3.safescale(a) end
            function visnormalized(a) return vec3.isnormalized(a) end
            function getlengthandnormalize(a) return vec3.getlengthandnormalize(a) end
            function normalizewithlength(a) return vec3.normalizewithlength(a) end
            function subpos(a, b) return vec3.subpos(a, b) end
            function offsetpos(a, b) return vec3.offsetpos(a, b) end
            function lerpposition(a, b, t) return vec3.lerpposition(a, b, t) end
            function visvalid(a) return vec3.isvalid(a) end

            function qnormalize(q) return quat.normalize(q) end
            function invmul(a, b) return quat.invmul(a, b) end
            function qdot(a, b) return quat.dot(a, b) end
            function negate(q) return quat.negate(q) end
            function fromaxisangle(a, r) return quat.fromaxisangle(a, r) end
            function axisangle(q) return quat.axisangle(q) end
            function angle(q) return quat.angle(q) end
            function twistangle(q) return quat.twistangle(q) end
            function swingangle(q) return quat.swingangle(q) end
            function between(a, b) return quat.betweenunitvectors(a, b) end
            function qisvalid(q) return quat.isvalid(q) end
            function qisnormalized(q) return quat.isnormalized(q) end

            function tinvmul(a, b) return transform.invmul(a, b) end
            function makeworld(t) return transform.makeworld(t) end
            function mulworld(a, b) return transform.mulworld(a, b) end
            function invmulworld(a, b) return transform.invmulworld(a, b) end
            function worldpoint(t, p) return transform.transformworldpoint(t, p) end
            function invworldpoint(t, p) return transform.invtransformworldpoint(t, p) end
            function torelative(t, p) return transform.torelative(t, p) end
            function tisvalid(t) return transform.isvalid(t) end
        )");
        REQUIRE(luau_load(L, "=moo-3c-oracle", bytecode.data(), bytecode.size(), 0) == 0);
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
        REQUIRE(luau_load(L, "=moo-3c-plant", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }

    template<typename R, typename... A>
    bool run(const char* fn, R& out, A... args)
    {
        lua_getglobal(L, fn);
        (push(L, args), ...);
        if (lua_pcall(L, int(sizeof...(A)), Count<R>::results, 0) != 0)
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
        compare(name, fn, want, nullptr, args...);
    }

    // a case moo refuses by name: the error must carry message
    template<typename R, typename... A>
    void checkRefused(const char* name, const char* fn, const char* message, A... args)
    {
        compare(name, fn, R{}, message, args...);
    }

    template<typename R, typename... A>
    void compare(const char* name, const char* fn, R want, const char* message, A... args)
    {
        R got{};
        error.clear();
        bool ok = run(fn, got, args...);
        ++cases;
        bool good = message ? !ok && error.find(message) != std::string::npos : ok && same(got, want);
        if (message && good)
            ++refused;
        if (!good)
        {
            ++mismatchesByOp[name];
            if (mismatches++ < 8 && !quiet)
            {
                std::string text = std::string(name) + ": moo " + (ok ? show(got) : "raised " + error) + " want " +
                                   (message ? std::string("error '") + message + "'" : show(want));
                ((text += " " + show(args)), ...);
                FAIL_CHECK(text);
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

typedef std::pair<fixed_t, fixed_t> Pair;
typedef std::pair<fixVec3, fixed_t> VecAndLength;

} // namespace

// every function of the layer, n cases each; the C side is the fixed function the moo function wraps, called on
// the same operands, or the predicted refusal
static void runAll(Oracle& o)
{
    // math
    {
        Rng r(0x3c01);
        o.op([&](uint64_t) {
            fixed_t a = r.full(), b = r.full(), c = r.full();
            o.check("math.muldiv", "muldiv", fixMulDiv(a, b, c), a, b, c);
        });
    }
    {
        Rng r(0x3c02);
        o.op([&](uint64_t) {
            fixed_t a = r.full(), b = r.full();
            o.check("math.halfsum", "halfsum", fixHalfSum(a, b), a, b);
        });
    }
    {
        Rng r(0x3c03);
        o.op([&](uint64_t) {
            fixed_t a = r.full(), b = r.full();
            o.check("math.halfdifference", "halfdifference", fixHalfDifference(a, b), a, b);
        });
    }
    {
        Rng r(0x3c04);
        o.op([&](uint64_t) {
            fixed_t a = r.full();
            o.check("math.unwindangle", "unwindangle", fixUnwindAngle(a), a);
        });
    }
    {
        Rng r(0x3c05);
        o.op([&](uint64_t) {
            fixed_t a = r.full();
            fixCosSin cs = fixComputeCosSinPrecise(a);
            o.check("math.cossin", "cossin", Pair(cs.cosine, cs.sine), a);
        });
    }
    {
        Rng r(0x3c06);
        o.op([&](uint64_t) {
            fixed_t c = r.full(), t = r.full(), v = r.full(), s = r.duration(), dt = r.duration();
            fixed_t velocity = v;
            fixed_t result = fixSmoothCriticallyDamped(c, t, &velocity, s, dt);
            o.check("math.smoothcriticallydamped", "smooth", Pair(result, velocity), c, t, v, s, dt);
        });
    }
    {
        Rng r(0x3c07);
        o.op([&](uint64_t) {
            fixed_t c = r.full(), t = r.full(), v = r.full(), su = r.duration(), sd = r.duration(), dt = r.duration();
            fixed_t velocity = v;
            fixed_t result = fixSmoothCriticallyDampedUpDown(c, t, &velocity, su, sd, dt);
            o.check("math.smoothcriticallydampedupdown", "smoothupdown", Pair(result, velocity), c, t, v, su, sd, dt);
        });
    }
    {
        Rng r(0x3c08);
        o.op([&](uint64_t i) {
            fixed_t a = r.full(), lo = r.full(), hi = r.full();
            if (i % 3 == 0)
                a = lo; // on the edge
            o.check("math.fits", "fits", flag(fixFits(a, lo, hi)), a, lo, hi);
        });
    }
    {
        Rng r(0x3c09);
        o.op([&](uint64_t) {
            fixed_t a = r.full();
            int s = r.shift();
            int64_t half = (int64_t(1) << s) >> 1;
            if (a <= INT64_MAX - half)
                o.check("math.narrow", "narrow", fixNarrow(a, s), a, s);
            else
                o.checkRefused<fixed_t>("math.narrow", "narrow", "value out of range for 'narrow'", a, s);
        });
    }
    {
        Rng r(0x3c0a);
        o.op([&](uint64_t i) {
            int s = r.shift();
            int64_t bound = INT64_MAX >> s;
            fixed_t a = (i & 1) ? r.within(bound) : r.full();
            if (a >= -bound && a <= bound)
                o.check("math.widen", "widen", fixWiden(a, s), a, s);
            else
                o.checkRefused<fixed_t>("math.widen", "widen", "value out of range for 'widen'", a, s);
        });
    }

    // vec3
    {
        Rng r(0x3c11);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.check("vec3.distance", "distance", fixDistance(a, b), a, b);
        });
    }
    {
        Rng r(0x3c12);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.check("vec3.distancesquared", "distancesquared", fixDistanceSquared(a, b), a, b);
        });
    }
    {
        Rng r(0x3c13);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec();
            o.check("vec3.lengthsquared", "lengthsquared", fixLengthSquared(a), a);
        });
    }
    {
        Rng r(0x3c14);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            fixed_t t = r.alpha();
            if (t >= 0 && t <= kOne)
                o.check("vec3.lerp", "vlerp", fixVecLerp(a, b, t), a, b, t);
            else
                o.checkRefused<fixVec3>("vec3.lerp", "vlerp", "alpha must be in [0, 1]", a, b, t);
        });
    }
    {
        Rng r(0x3c15);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.check("vec3.min", "vmin", fixVecMin(a, b), a, b);
        });
    }
    {
        Rng r(0x3c16);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.check("vec3.max", "vmax", fixVecMax(a, b), a, b);
        });
    }
    {
        Rng r(0x3c17);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), lo = r.fullVec(), hi = r.fullVec();
            o.check("vec3.clamp", "vclamp", fixVecClamp(a, lo, hi), a, lo, hi);
        });
    }
    {
        Rng r(0x3c18);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec();
            o.check("vec3.abs", "vabs", fixVecAbs(a), a);
        });
    }
    {
        Rng r(0x3c19);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec();
            o.check("vec3.sign", "vsign", fixSign(a), a);
        });
    }
    {
        Rng r(0x3c1a);
        o.op([&](uint64_t i) {
            fixVec3 a = (i & 1) ? r.unitVec() : r.fullVec();
            o.check("vec3.perp", "perp", fixPerp(a), a);
        });
    }
    {
        Rng r(0x3c1b);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            fixed_t s = r.full();
            o.check("vec3.muladd", "muladd", fixMulAdd(a, s, b), a, s, b);
        });
    }
    {
        Rng r(0x3c1c);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            fixed_t s = r.full();
            o.check("vec3.mulsub", "mulsub", fixMulSub(a, s, b), a, s, b);
        });
    }
    {
        Rng r(0x3c1d);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            fixed_t s = r.full(), t = r.full();
            o.check("vec3.blend2", "blend2", fixBlend2(s, a, t, b), s, a, t, b);
        });
    }
    {
        Rng r(0x3c1e);
        o.op([&](uint64_t i) {
            fixVec3 a = (i & 1) ? r.inRangeVec() : r.fullVec();
            o.check("vec3.safescale", "safescale", fixSafeScale(a), a);
        });
    }
    {
        Rng r(0x3c1f);
        o.op([&](uint64_t i) {
            fixVec3 a = (i & 1) ? r.unitVec() : r.fullVec();
            o.check("vec3.isnormalized", "visnormalized", flag(fixIsNormalized(a)), a);
        });
    }
    {
        Rng r(0x3c20);
        o.op([&](uint64_t i) {
            fixVec3 a = (i & 1) ? r.inRangeVec() : r.fullVec();
            fixed_t length = 0;
            fixVec3 unit = fixGetLengthAndNormalize(&length, a);
            o.check("vec3.getlengthandnormalize", "getlengthandnormalize", VecAndLength(unit, length), a);
        });
    }
    {
        Rng r(0x3c21);
        o.op([&](uint64_t i) {
            fixVec3 a = (i & 1) ? r.inRangeVec() : r.fullVec();
            fixed_t length = 0;
            fixVec3 unit = fixNormalizeWithLength(a, &length);
            o.check("vec3.normalizewithlength", "normalizewithlength", VecAndLength(unit, length), a);
        });
    }
    {
        Rng r(0x3c22);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.check("vec3.subpos", "subpos", fixSubPos(a, b), a, b);
        });
    }
    {
        Rng r(0x3c23);
        o.op([&](uint64_t) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            o.check("vec3.offsetpos", "offsetpos", fixOffsetPos(a, b), a, b);
        });
    }
    {
        Rng r(0x3c24);
        o.op([&](uint64_t i) {
            fixVec3 a = r.fullVec(), b = r.fullVec();
            fixed_t t = (i & 1) ? r.alpha() : r.full(); // fixLerpPosition asserts nothing about t
            o.check("vec3.lerpposition", "lerpposition", fixLerpPosition(a, b, t), a, b, t);
        });
    }
    {
        Rng r(0x3c25);
        o.op([&](uint64_t i) {
            fixVec3 a = r.fullVec();
            if (i % 4 == 0)
                a.y = INT64_MIN; // only a C host can make one, and it is not valid
            o.check("vec3.isvalid", "visvalid", flag(fixIsValidVec3(a)), a);
        });
    }

    // quat
    {
        Rng r(0x3c31);
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            o.check("quat.normalize", "qnormalize", fixNormalizeQuat(q), q);
        });
    }
    {
        Rng r(0x3c32);
        o.op([&](uint64_t) {
            fixQuat a = r.rotation(), b = r.rotation();
            o.check("quat.invmul", "invmul", fixInvMulQuat(a, b), a, b);
        });
    }
    {
        Rng r(0x3c33);
        o.op([&](uint64_t) {
            fixQuat a = r.rotation(), b = r.rotation();
            o.check("quat.dot", "qdot", fixDotQuat(a, b), a, b);
        });
    }
    {
        Rng r(0x3c34);
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            o.check("quat.negate", "negate", fixNegateQuat(q), q);
        });
    }
    {
        Rng r(0x3c35);
        o.op([&](uint64_t) {
            fixVec3 axis = r.unitVec();
            fixed_t radians = r.full();
            if (fixIsNormalized(axis))
                o.check("quat.fromaxisangle", "fromaxisangle", fixMakeQuatFromAxisAngle(axis, radians), axis, radians);
            else
                o.checkRefused<fixQuat>("quat.fromaxisangle", "fromaxisangle", "axis must be a unit vector", axis, radians);
        });
    }
    {
        Rng r(0x3c36);
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            fixed_t radians = 0;
            fixVec3 axis = fixGetAxisAngle(&radians, q);
            o.check("quat.axisangle", "axisangle", VecAndLength(axis, radians), q);
        });
    }
    {
        Rng r(0x3c37);
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            o.check("quat.angle", "angle", fixGetQuatAngle(q), q);
        });
    }
    {
        Rng r(0x3c38);
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            o.check("quat.twistangle", "twistangle", fixGetTwistAngle(q), q);
        });
    }
    {
        Rng r(0x3c39);
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            o.check("quat.swingangle", "swingangle", fixGetSwingAngle(q), q);
        });
    }
    {
        Rng r(0x3c3a);
        o.op([&](uint64_t i) {
            fixVec3 a = r.unitVec();
            fixVec3 b = (i % 4 == 0) ? fixVecNeg(a) : r.unitVec(); // the anti-parallel branch too
            if (!fixIsNormalized(a))
                o.checkRefused<fixQuat>("quat.betweenunitvectors", "between", "vector must be a unit vector", a, b);
            else if (!fixIsNormalized(b))
                o.checkRefused<fixQuat>("quat.betweenunitvectors", "between", "vector must be a unit vector", a, b);
            else
                o.check("quat.betweenunitvectors", "between", fixComputeQuatBetweenUnitVectors(a, b), a, b);
        });
    }
    {
        Rng r(0x3c3b);
        o.op([&](uint64_t i) {
            fixQuat q = r.rotation();
            if (i % 8 == 0)
                q.s = INT64_MIN;
            o.check("quat.isvalid", "qisvalid", flag(fixIsValidQuat(q)), q);
        });
    }
    {
        Rng r(0x3c3c);
        o.op([&](uint64_t) {
            fixQuat q = r.rotation();
            o.check("quat.isnormalized", "qisnormalized", flag(fixIsNormalizedQuat(q)), q);
        });
    }

    // transform: rigid transforms inside fixed's range, and full-range positions one time in four
    auto transformPair = [](Rng& r, uint64_t i, fixTransform& a, fixTransform& b) {
        if (i % 4 == 3)
        {
            a = r.wide();
            b = r.wide();
        }
        else
        {
            a = r.rigid();
            b = r.rigid();
        }
    };
    {
        Rng r(0x3c41);
        o.op([&](uint64_t i) {
            fixTransform a, b;
            transformPair(r, i, a, b);
            o.check("transform.invmul", "tinvmul", fixInvMulTransforms(a, b), a, b);
        });
    }
    {
        Rng r(0x3c42);
        o.op([&](uint64_t i) {
            fixTransform t = (i % 4 == 3) ? r.wide() : r.rigid();
            o.check("transform.makeworld", "makeworld", fixMakeWorldTransform(t), t);
        });
    }
    {
        Rng r(0x3c43);
        o.op([&](uint64_t i) {
            fixTransform a, b;
            transformPair(r, i, a, b);
            o.check("transform.mulworld", "mulworld", fixMulWorldTransforms(a, b), a, b);
        });
    }
    {
        Rng r(0x3c44);
        o.op([&](uint64_t i) {
            fixTransform a, b;
            transformPair(r, i, a, b);
            o.check("transform.invmulworld", "invmulworld", fixInvMulWorldTransforms(a, b), a, b);
        });
    }
    {
        Rng r(0x3c45);
        o.op([&](uint64_t i) {
            fixTransform t = (i % 4 == 3) ? r.wide() : r.rigid();
            fixVec3 p = (i % 4 == 3) ? r.fullVec() : r.inRangeVec();
            o.check("transform.transformworldpoint", "worldpoint", fixTransformWorldPoint(t, p), t, p);
        });
    }
    {
        Rng r(0x3c46);
        o.op([&](uint64_t i) {
            fixTransform t = (i % 4 == 3) ? r.wide() : r.rigid();
            fixVec3 p = (i % 4 == 3) ? r.fullVec() : r.inRangeVec();
            o.check("transform.invtransformworldpoint", "invworldpoint", fixInvTransformWorldPoint(t, p), t, p);
        });
    }
    {
        Rng r(0x3c47);
        o.op([&](uint64_t i) {
            fixTransform t = (i % 4 == 3) ? r.wide() : r.rigid();
            fixVec3 base = (i % 4 == 3) ? r.fullVec() : r.inRangeVec();
            o.check("transform.torelative", "torelative", fixToRelativeTransform(t, base), t, base);
        });
    }
    {
        Rng r(0x3c48);
        o.op([&](uint64_t i) {
            fixTransform t = (i % 4 == 3) ? r.wide() : r.rigid();
            if (i % 8 == 0)
                t.p.z = INT64_MIN;
            o.check("transform.isvalid", "tisvalid", flag(fixIsValidTransform(t)), t);
        });
    }
}

const int kOps = 51; // 10 math, 21 vec3, 12 quat, 8 transform

TEST_CASE("CompleteOracle")
{
    Oracle o(caseCount());
    runAll(o);

    printf("moo-3c-complete-oracle ops=%d cases=%llu per-op=%llu mismatches=%llu refused=%llu\n", o.ops, (unsigned long long)o.cases,
        (unsigned long long)o.n, (unsigned long long)o.mismatches, (unsigned long long)o.refused);

    CHECK(o.mismatches == 0);
    CHECK(o.ops == kOps);
    // the corpus reaches every named refusal
    CHECK(o.refused > 0);
}

// The reversed witness: the gate must go red when a function is wrong, and name that function and no other. Each
// plant replaces one script function with a wrong one: one raw unit off, a swapped operand, the sibling function.
// Unplanted, the same small run has no mismatch, so the red comes from the plant.
TEST_CASE("CompleteOracleGoesRedOnAPlantedWrongOp")
{
    const uint64_t n = 256;
    {
        Oracle o(n, /* quiet= */ true);
        runAll(o);
        CHECK(o.ops == kOps);
        CHECK(o.mismatches == 0);
    }

    struct Plant
    {
        const char* op;
        const char* source;
    };
    const Plant plants[] = {
        {"math.muldiv", "function muldiv(a, b, c) return math.muldiv(a, c, b) end"},
        {"math.halfsum", "function halfsum(a, b) return math.halfdifference(a, b) end"},
        {"math.cossin", "function cossin(a) local c, s = math.cossin(a) return s, c end"},
        {"math.widen", "function widen(a, s) return math.widen(a, s) + 1 / 65536 end"},
        {"vec3.distance", "function distance(a, b) return vec3.distancesquared(a, b) end"},
        {"vec3.lerp", "function vlerp(a, b, t) return vec3.lerp(b, a, t) end"},
        {"vec3.getlengthandnormalize", "function getlengthandnormalize(a) return vec3.normalizewithlength(a) end"},
        {"quat.invmul", "function invmul(a, b) return quat.invmul(b, a) end"},
        {"quat.twistangle", "function twistangle(q) return quat.swingangle(q) end"},
        {"transform.invmul", "function tinvmul(a, b) return transform.inverse(a) * b end"},
        {"transform.torelative", "function torelative(t, p) return transform.new(t.p + p, t.q) end"},
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

// Each argument the libraries refuse, by its message.
TEST_CASE("CompleteRefusals")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    struct Refusal
    {
        const char* source;
        const char* error;
    };
    const Refusal refusals[] = {
        {"return math.muldiv(1, 2)", "missing argument #3 to 'muldiv' (number expected)"},
        {"return math.cossin('a')", "invalid argument #1 to 'cossin' (number expected, got string)"},
        {"return math.narrow(1, 63)", "invalid argument #2 to 'narrow' (shift must be in [0, 63))"},
        {"return math.narrow(1, -1)", "invalid argument #2 to 'narrow' (shift must be in [0, 63))"},
        {"return math.narrow(1, 0.5)", "invalid argument #2 to 'narrow' (number has no integer representation)"},
        {"return math.narrow(math.huge, 1)", "invalid argument #1 to 'narrow' (value out of range for 'narrow')"},
        {"return math.widen(1, 63)", "invalid argument #2 to 'widen' (shift must be in [0, 63))"},
        {"return math.widen(2147483648, 16)", "invalid argument #1 to 'widen' (value out of range for 'widen')"},
        {"return math.widen(-2147483648, 16)", "invalid argument #1 to 'widen' (value out of range for 'widen')"},
        {"return math.smoothcriticallydamped(1, 2, 3, 4)", "missing argument #5 to 'smoothcriticallydamped' (number expected)"},
        {"return vec3.lerp(v, v, 2)", "invalid argument #3 to 'lerp' (alpha must be in [0, 1])"},
        {"return vec3.lerp(v, v, -0.5)", "invalid argument #3 to 'lerp' (alpha must be in [0, 1])"},
        {"return vec3.distance(v, 1)", "invalid argument #2 to 'distance' (vec3 expected, got number)"},
        {"return vec3.muladd(v, v, v)", "invalid argument #2 to 'muladd' (number expected, got vec3)"},
        {"return vec3.blend2(1, v, v, v)", "invalid argument #3 to 'blend2' (number expected, got vec3)"},
        {"return vec3.clamp(v, v)", "missing argument #3 to 'clamp' (vec3 expected)"},
        {"return quat.fromaxisangle(v, 1)", "invalid argument #1 to 'fromaxisangle' (axis must be a unit vector)"},
        {"return quat.fromaxisangle(vec3.new(), 1)", "invalid argument #1 to 'fromaxisangle' (axis must be a unit vector)"},
        {"return quat.betweenunitvectors(vec3.new(1, 0, 0), v)", "invalid argument #2 to 'betweenunitvectors' (vector must be a unit vector)"},
        {"return quat.betweenunitvectors(v, vec3.new(1, 0, 0))", "invalid argument #1 to 'betweenunitvectors' (vector must be a unit vector)"},
        {"return quat.invmul(q, v)", "invalid argument #2 to 'invmul' (quat expected, got vec3)"},
        {"return quat.dot(q)", "missing argument #2 to 'dot' (quat expected)"},
        {"return quat.axisangle(v)", "invalid argument #1 to 'axisangle' (quat expected, got vec3)"},
        {"return transform.invmul(t, q)", "invalid argument #2 to 'invmul' (transform expected, got quat)"},
        {"return transform.torelative(t, t)", "invalid argument #2 to 'torelative' (vec3 expected, got transform)"},
        {"return transform.transformworldpoint(v, v)", "invalid argument #1 to 'transformworldpoint' (transform expected, got vec3)"},
        {"return transform.isvalid()", "missing argument #1 to 'isvalid' (transform expected)"},
    };
    for (const Refusal& r : refusals)
    {
        INFO(std::string(r.source));
        std::string source = std::string("local v, q, t = vec3.new(1, 2, 3), quat.new(), transform.new() ") + r.source;
        std::string got = mooRun(L, source.c_str());
        CHECK(got.rfind("error: ", 0) == 0);
        CHECK(got.find(r.error) != std::string::npos);
    }
    lua_close(L);
}

// The expected-behaviour script, tests/conformance/moo/spec/complete.luau.
TEST_CASE("CompleteScript")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK(mooRunFile(L, "spec/complete.luau") == "OK");
    lua_close(L);
}

TEST_SUITE_END();
