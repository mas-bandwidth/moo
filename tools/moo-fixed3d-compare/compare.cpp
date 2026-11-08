// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3b's closing gate, item (2) (docs/DESIGN.md "Conformance (layer 3b)"; docs/history/layer3b/moo-v2-l3b-conformance.md):
// moo's vec3, quat and transform, and layer 3c's mat3 and aabb (docs/DESIGN.md "Layer 3c as built", N4), against
// fixed3d's own b3* functions (b3MakeQuatFromMatrix is compiled into fixed3d's library, not a header forwarder, so
// quat.frommatrix is checked against fixed only, in tests/moo/complete.test.cpp), built with BOX3D_FIXED_SATURATE, on the same
// operands, 2^16 cases per operation. fixed3d's b3* are inline forwarders to its vendored fixed, which
// tools/moo-fixed3d-compare.sh checks is identical to moo's extern/fixed, so every case must agree bit for bit; the
// program exits 1 on any difference. Built with -fwrapv so a plain int64 add that overflowed would be counted as a
// difference instead of being undefined. Not part of Moo.Test: it needs fixed3d, which the script clones at its pin.
// Against a checked moo build (MOO_OVERFLOW_ERRORS) a vec3 + - or * that saturates must raise the named error instead
// (docs/DESIGN.md "Overflow"), as must a mat3 + - or scale; the program asks the VM which build it is.
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
    fixMatrix3 matFull() { return {vecFull(), vecFull(), vecFull()}; }
    fixMatrix3 matSmall() { return {{within(4 * kOne), within(4 * kOne), within(4 * kOne)}, {within(4 * kOne), within(4 * kOne), within(4 * kOne)}, {within(4 * kOne), within(4 * kOne), within(4 * kOne)}}; }
    fixAABB box()
    {
        fixVec3 a = vecIn(), b = vecIn();
        return {fixVecMin(a, b), fixVecMax(a, b)};
    }
};

static lua_State* L;

static void push(fixVec3 v) { moo_pushvec3(L, v.x, v.y, v.z); }
static void push(fixQuat q) { moo_pushquat(L, q.v.x, q.v.y, q.v.z, q.s); }
static void push(fixTransform t) { moo_pushtransform(L, t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s); }
static void push(fixed_t n) { lua_pushnumber(L, n); }
static void push(fixMatrix3 m)
{
    const lua_Number r[9] = {m.cx.x, m.cx.y, m.cx.z, m.cy.x, m.cy.y, m.cy.z, m.cz.x, m.cz.y, m.cz.z};
    moo_pushmat3(L, r);
}
static void push(fixAABB a) { moo_pushaabb(L, a.lowerBound.x, a.lowerBound.y, a.lowerBound.z, a.upperBound.x, a.upperBound.y, a.upperBound.z); }
static void pop(fixVec3& v) { lua_Number r[3]; moo_tovec3(L, -1, r); lua_pop(L, 1); v = {r[0], r[1], r[2]}; }
static void pop(fixQuat& q) { lua_Number r[4]; moo_toquat(L, -1, r); lua_pop(L, 1); q = {{r[0], r[1], r[2]}, r[3]}; }
static void pop(fixTransform& t) { lua_Number r[7]; moo_totransform(L, -1, r); lua_pop(L, 1); t = {{r[0], r[1], r[2]}, {{r[3], r[4], r[5]}, r[6]}}; }
static void pop(fixed_t& n) { n = lua_tonumber(L, -1); lua_pop(L, 1); }
static void pop(bool& b) { b = lua_toboolean(L, -1) != 0; lua_pop(L, 1); }
static void pop(fixMatrix3& m)
{
    lua_Number r[9];
    moo_tomat3(L, -1, r);
    lua_pop(L, 1);
    m = {{r[0], r[1], r[2]}, {r[3], r[4], r[5]}, {r[6], r[7], r[8]}};
}
static void pop(fixAABB& a)
{
    lua_Number r[6];
    moo_toaabb(L, -1, r);
    lua_pop(L, 1);
    a = {{r[0], r[1], r[2]}, {r[3], r[4], r[5]}};
}
static bool same(fixVec3 a, fixVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool same(fixQuat a, fixQuat b) { return same(a.v, b.v) && a.s == b.s; }
static bool same(fixTransform a, fixTransform b) { return same(a.p, b.p) && same(a.q, b.q); }
static bool same(fixed_t a, fixed_t b) { return a == b; }
static bool same(bool a, bool b) { return a == b; }
static bool same(fixMatrix3 a, fixMatrix3 b) { return same(a.cx, b.cx) && same(a.cy, b.cy) && same(a.cz, b.cz); }
static bool same(fixAABB a, fixAABB b) { return same(a.lowerBound, b.lowerBound) && same(a.upperBound, b.upperBound); }

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

static bool addSaturates(fixMatrix3 a, fixMatrix3 b)
{
    return addSaturates(a.cx, b.cx) || addSaturates(a.cy, b.cy) || addSaturates(a.cz, b.cz);
}

static bool subSaturates(fixMatrix3 a, fixMatrix3 b)
{
    return subSaturates(a.cx, b.cx) || subSaturates(a.cy, b.cy) || subSaturates(a.cz, b.cz);
}

static bool scaleSaturates(fixed_t s, fixMatrix3 a)
{
    return scaleSaturates(s, a.cx) || scaleSaturates(s, a.cy) || scaleSaturates(s, a.cz);
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
                                   "function invdirection(t,v) return transform.invdirection(t,v) end "
                                   "function transpose(m) return mat3.transpose(m) end function minverse(m) return mat3.inverse(m) end "
                                   "function inverset(m) return mat3.inverset(m) end function solve(m,v) return mat3.solve(m,v) end "
                                   "function det(m) return mat3.det(m) end function mabs(m) return mat3.abs(m) end "
                                   "function fromquat(q) return mat3.fromquat(q) end "
                                   "function frompoints(a,b,c,r) return aabb.frompoints({a,b,c},r) end function contains(a,b) return aabb.contains(a,b) end "
                                   "function area(a) return aabb.area(a) end function center(a) return aabb.center(a) end "
                                   "function extents(a) return aabb.extents(a) end function union(a,b) return aabb.union(a,b) end "
                                   "function inflate(a,e) return aabb.inflate(a,e) end function overlaps(a,b) return aabb.overlaps(a,b) end "
                                   "function btransform(t,a) return aabb.transform(t,a) end function closest(p,a) return aabb.closestpoint(p,a) end");
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

        // layer 3c: mat3 and aabb
        fixMatrix3 ma = r.matFull(), mb = r.matFull(), sa = r.matSmall(), sb = r.matSmall();
        cmpOrRaise("mat3 + mat3 (full range)", "add", b3AddMM(ma, mb), addSaturates(ma, mb), ma, mb);
        cmpOrRaise("mat3 - mat3 (full range)", "sub", b3SubMM(ma, mb), subSaturates(ma, mb), ma, mb);
        cmpOrRaise("mat3 * number (full range)", "mul", b3MulSM(a.x, mb), scaleSaturates(a.x, mb), mb, a.x);
        cmp("-mat3 (full range)", "unm", b3NegateMat3(ma), ma);
        cmp("mat3 * mat3 (full range)", "mul", b3MulMM(ma, mb), ma, mb);
        cmp("mat3 * mat3 (small)", "mul", b3MulMM(sa, sb), sa, sb);
        cmp("mat3 * vec3 (full range)", "mul", b3MulMV(ma, a), ma, a);
        cmp("mat3.transpose (full range)", "transpose", b3Transpose(ma), ma);
        cmp("mat3.inverse (small)", "minverse", b3InvertMatrix(sa), sa);
        cmp("mat3.inverset (small)", "inverset", b3InvertT(sa), sa);
        cmp("mat3.solve (small, in range)", "solve", b3Solve3(sa, ai), sa, ai);
        cmp("mat3.det (small)", "det", b3Det(sa), sa);
        cmp("mat3.abs (full range)", "mabs", b3AbsMatrix3(ma), ma);
        cmp("mat3.fromquat (unit)", "fromquat", b3MakeMatrixFromQuat(u), u);
        fixAABB ba = r.box(), bb = r.box();
        fixVec3 pts[3] = {ai, bi, r.vecIn()};
        fixed_t radius = r.within(8 * kOne);
        cmp("aabb.frompoints (in range)", "frompoints", b3MakeAABB(pts, 3, radius), pts[0], pts[1], pts[2], radius);
        cmp("aabb.contains (in range)", "contains", b3AABB_Contains(ba, bb), ba, bb);
        cmp("aabb.contains (self)", "contains", b3AABB_Contains(ba, ba), ba, ba);
        cmp("aabb.area (in range)", "area", b3AABB_Area(ba), ba);
        cmp("aabb.center (in range)", "center", b3AABB_Center(ba), ba);
        cmp("aabb.extents (in range)", "extents", b3AABB_Extents(ba), ba);
        cmp("aabb.union (in range)", "union", b3AABB_Union(ba, bb), ba, bb);
        cmp("aabb.inflate (in range)", "inflate", b3AABB_Inflate(ba, radius), ba, radius);
        cmp("aabb.overlaps (in range)", "overlaps", b3AABB_Overlaps(ba, bb), ba, bb);
        cmp("aabb.transform (unit, in range)", "btransform", b3AABB_Transform(t1, ba), t1, ba);
        cmp("aabb.closestpoint (in range)", "closest", b3ClosestPointToAABB(a, ba), a, ba);
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
