// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3b's closing gate, item (2) (docs/DESIGN.md "Conformance (layer 3b)"; docs/history/layer3b/moo-v2-l3b-conformance.md):
// moo's vec3, quat and transform against fixed3d's own b3* functions, built with BOX3D_FIXED_SATURATE, on the same
// operands, 2^16 cases per operation. fixed3d's b3* are inline forwarders to its vendored fixed, which
// tools/moo-fixed3d-compare.sh checks is identical to moo's extern/fixed, so every case must agree bit for bit; the
// program exits 1 on any difference. Built with -fwrapv so a plain int64 add that overflowed would be counted as a
// difference instead of being undefined. Not part of Moo.Test: it needs fixed3d, which the script clones at its pin.
// Against a checked moo build (MOO_OVERFLOW_ERRORS) a vec3 + - or * that saturates must raise the named error instead
// (docs/DESIGN.md "Overflow"); the program asks the VM which build it is.
#include "box3d/base.h"
#include "box3d/math_functions.h"

#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "moo_number.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>

#ifndef BOX3D_FIXED_SATURATE
#error "compare against fixed3d built with BOX3D_FIXED_SATURATE"
#endif

// fixed3d's assert hook, from its core library: an assert reached here is a failure of the comparison
extern "C" int b3InternalAssert(const char* condition, const char* fileName, int lineNumber)
{
    fprintf(stderr, "b3 assert %s at %s:%d\n", condition, fileName, lineNumber);
    abort();
}

static const fixed_t kOne = 65536, kMax = INT64_MAX, kMin = -INT64_MAX, kInRange = fixed_t(1) << 40;

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
    fixed_t within(fixed_t bound) { return fixed_t(next() % uint64_t(2 * bound + 1)) - bound; }
    fixed_t full()
    {
        static const fixed_t edges[] = {0, 1, -1, kOne, -kOne, kOne / 2, fixed_t(1) << 47, -(fixed_t(1) << 47), kMax, kMin, kMax - 1, kMin + 1};
        uint64_t r = next();
        if ((r & 3) == 0)
            return edges[(r >> 2) % 12];
        int64_t v = int64_t(next() >> (1 + (r >> 2) % 63));
        return (r & 0x100) ? -v : v;
    }
    fixVec3 vecFull() { return {full(), full(), full()}; }
    fixVec3 vecIn() { return {within(kInRange), within(kInRange), within(kInRange)}; }
    fixQuat unit() { return fixNormalizeQuat(fixQuat{{within(kOne), within(kOne), within(kOne)}, within(kOne)}); }
    fixQuat quatFull() { return {{full(), full(), full()}, full()}; }
};

static lua_State* L;

static void push(fixVec3 v) { moo_pushvec3(L, v.x, v.y, v.z); }
static void push(fixQuat q) { moo_pushquat(L, q.v.x, q.v.y, q.v.z, q.s); }
static void push(fixTransform t) { moo_pushtransform(L, t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s); }
static void push(fixed_t n) { lua_pushnumber(L, n); }
static void pop(fixVec3& v) { lua_Number r[3]; moo_tovec3(L, -1, r); lua_pop(L, 1); v = {r[0], r[1], r[2]}; }
static void pop(fixQuat& q) { lua_Number r[4]; moo_toquat(L, -1, r); lua_pop(L, 1); q = {{r[0], r[1], r[2]}, r[3]}; }
static void pop(fixTransform& t) { lua_Number r[7]; moo_totransform(L, -1, r); lua_pop(L, 1); t = {{r[0], r[1], r[2]}, {{r[3], r[4], r[5]}, r[6]}}; }
static void pop(fixed_t& n) { n = lua_tonumber(L, -1); lua_pop(L, 1); }
static bool same(fixVec3 a, fixVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool same(fixQuat a, fixQuat b) { return same(a.v, b.v) && a.s == b.s; }
static bool same(fixTransform a, fixTransform b) { return same(a.p, b.p) && same(a.q, b.q); }
static bool same(fixed_t a, fixed_t b) { return a == b; }

struct Tally { long cases = 0, differ = 0; std::string first; };
static std::map<std::string, Tally> tally;

// true when the moo build under test is checked: it raises where release saturates
static bool checked = false;

// want, or in a checked build the named overflow error when raises is true
template<typename R, typename... A>
static void cmpOrRaise(const char* name, const char* fn, R want, bool raises, A... args)
{
    lua_getglobal(L, fn);
    (push(args), ...);
    R got{};
    Tally& t = tally[name];
    t.cases++;
    raises = raises && checked;
    if (lua_pcall(L, int(sizeof...(A)), 1, 0) != 0)
    {
        const char* error = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        if (!raises || !strstr(error, "number overflow in '"))
            t.differ++;
        lua_pop(L, 1);
        return;
    }
    pop(got);
    if (raises || !same(got, want))
        t.differ++;
}

template<typename R, typename... A>
static void cmp(const char* name, const char* fn, R want, A... args)
{
    cmpOrRaise(name, fn, want, false, args...);
}

static bool addSaturates(fixVec3 a, fixVec3 b)
{
    return moo_numaddsaturates(a.x, b.x) || moo_numaddsaturates(a.y, b.y) || moo_numaddsaturates(a.z, b.z);
}

static bool subSaturates(fixVec3 a, fixVec3 b)
{
    return moo_numsubsaturates(a.x, b.x) || moo_numsubsaturates(a.y, b.y) || moo_numsubsaturates(a.z, b.z);
}

static bool scaleSaturates(fixed_t s, fixVec3 a)
{
    return moo_nummulsaturates(s, a.x) || moo_nummulsaturates(s, a.y) || moo_nummulsaturates(s, a.z);
}

int main()
{
    L = luaL_newstate();
    luaL_openlibs(L);
    std::string bc = Luau::compile("function add(a,b) return a+b end function sub(a,b) return a-b end function mul(a,b) return a*b end "
                                   "function unm(a) return -a end function dot(a,b) return vec3.dot(a,b) end function cross(a,b) return vec3.cross(a,b) end "
                                   "function length(a) return vec3.length(a) end function normalize(a) return vec3.normalize(a) end "
                                   "function conjugate(q) return quat.conjugate(q) end function nlerp(a,b,t) return quat.nlerp(a,b,t) end "
                                   "function inverse(t) return transform.inverse(t) end function point(t,v) return transform.point(t,v) end "
                                   "function invpoint(t,v) return transform.invpoint(t,v) end function direction(t,v) return transform.direction(t,v) end "
                                   "function invdirection(t,v) return transform.invdirection(t,v) end");
    if (luau_load(L, "=cmp", bc.data(), bc.size(), 0) != 0 || lua_pcall(L, 0, 0, 0) != 0)
        return 2;

    lua_getglobal(L, "add");
    lua_pushnumber(L, kMax);
    lua_pushnumber(L, fixed_t(1));
    checked = lua_pcall(L, 2, 1, 0) != 0;
    lua_pop(L, 1);
    printf("moo-fixed3d-compare: %s build\n", checked ? "checked" : "release");

    const int N = 1 << 16;
    Rng r{0xf1ed3d};
    for (int i = 0; i < N; ++i)
    {
        fixVec3 a = r.vecFull(), b = r.vecFull();
        cmpOrRaise("vec3 + vec3 (full range)", "add", b3Add(a, b), addSaturates(a, b), a, b);
        cmpOrRaise("vec3 - vec3 (full range)", "sub", b3Sub(a, b), subSaturates(a, b), a, b);
        cmpOrRaise("vec3 * number (full range)", "mul", b3MulSV(a.x, b), scaleSaturates(a.x, b), b, a.x);
        cmp("-vec3 (full range)", "unm", b3Neg(a), a);
        cmp("vec3.dot (full range)", "dot", b3Dot(a, b), a, b);
        cmp("vec3.length (full range)", "length", b3Length(a), a);
        cmp("vec3.normalize (full range)", "normalize", b3Normalize(a), a);
        fixVec3 ai = r.vecIn(), bi = r.vecIn();
        cmp("vec3 + vec3 (in range)", "add", b3Add(ai, bi), ai, bi);
        cmp("vec3.cross (in range)", "cross", b3Cross(ai, bi), ai, bi);
        fixQuat qa = r.quatFull(), qb = r.quatFull(), u = r.unit(), w = r.unit();
        cmp("quat * quat (full range)", "mul", b3MulQuat(qa, qb), qa, qb);
        cmp("quat * vec3 (unit, in range)", "mul", b3RotateVector(u, ai), u, ai);
        cmp("quat.conjugate (full range)", "conjugate", b3Conjugate(qa), qa);
        fixed_t alpha = fixed_t(r.next() % uint64_t(kOne + 1));
        cmp("quat.nlerp (unit)", "nlerp", b3NLerp(u, w, alpha), u, w, alpha);
        fixTransform t1 = {ai, u}, t2 = {bi, w};
        cmp("transform * transform (in range)", "mul", b3MulTransforms(t1, t2), t1, t2);
        cmp("transform.point (in range)", "point", b3TransformPoint(t1, bi), t1, bi);
        cmp("transform.invpoint (in range)", "invpoint", b3InvTransformPoint(t1, bi), t1, bi);
        cmp("transform.inverse (in range)", "inverse", b3InvertTransform(t1), t1);
        cmp("transform.direction (in range)", "direction", b3RotateVector(t1.q, bi), t1, bi);
        cmp("transform.invdirection (in range)", "invdirection", b3InvRotateVector(t1.q, bi), t1, bi);
        fixTransform s1 = {a, fixQuat{{0, 0, 0}, kOne}}, s2 = {b, fixQuat{{0, 0, 0}, kOne}};
        cmp("transform * transform (identity rotation, full-range position)", "mul", b3MulTransforms(s1, s2), s1, s2);
        cmp("transform.point (identity rotation, full range)", "point", b3TransformPoint(s1, b), s1, b);
        cmp("transform.invpoint (identity rotation, full range)", "invpoint", b3InvTransformPoint(s1, b), s1, b);
    }
    long total = 0, differ = 0;
    for (auto& [name, t] : tally)
    {
        printf("%-66s cases=%ld differ=%ld\n", name.c_str(), t.cases, t.differ);
        total += t.cases;
        differ += t.differ;
    }
    printf("fixed3d-compare BOX3D_FIXED_SATURATE ops=%zu cases=%ld differ=%ld\n", tally.size(), total, differ);
    lua_close(L);
    return differ == 0 ? 0 : 1;
}
