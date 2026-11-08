// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3c, the operators, component reads and libraries of vec2, mat3, plane, aabb and fix30 (docs/DESIGN.md
// "Layer 3c as built"). Every result is fixed's function on the raw components, so the bits are fixed's.
#include "lualib.h"

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "ldebug.h"
#include "ltm.h"
#include "moo_number.h"
#include "moo_narrow.h"
#include "moo_quat.h"
#include "moo_vec2.h"
#include "moo_mat3.h"
#include "moo_plane.h"
#include "moo_aabb.h"
#include "moo_fix30.h"
#include "moo_vec3.h"

#include "fixed/fixed_math.h"
#include "fixed/fixed_vec.h"

#include <string.h>

fixVec2 moo_vec2_check(lua_State* L, int idx)
{
    lua_Number raw[2];
    if (!moo_tovec2(L, idx, raw))
        luaL_typeerrorL(L, idx, "vec2");
    return FIX_LITERAL(fixVec2){raw[0], raw[1]};
}

fixMatrix3 moo_mat3_check(lua_State* L, int idx)
{
    lua_Number r[9];
    if (!moo_tomat3(L, idx, r))
        luaL_typeerrorL(L, idx, "mat3");
    return FIX_LITERAL(fixMatrix3){{r[0], r[1], r[2]}, {r[3], r[4], r[5]}, {r[6], r[7], r[8]}};
}

fixPlane moo_plane_check(lua_State* L, int idx)
{
    lua_Number r[4];
    if (!moo_toplane(L, idx, r))
        luaL_typeerrorL(L, idx, "plane");
    return FIX_LITERAL(fixPlane){{r[0], r[1], r[2]}, r[3]};
}

fixAABB moo_aabb_check(lua_State* L, int idx)
{
    lua_Number r[6];
    if (!moo_toaabb(L, idx, r))
        luaL_typeerrorL(L, idx, "aabb");
    return FIX_LITERAL(fixAABB){{r[0], r[1], r[2]}, {r[3], r[4], r[5]}};
}

fixed30_t moo_fix30_check(lua_State* L, int idx)
{
    int32_t raw = 0;
    if (!moo_tofix30(L, idx, &raw))
        luaL_typeerrorL(L, idx, "fix30");
    fixed30_t r = {raw};
    return r;
}

void moo_mat3_push(lua_State* L, fixMatrix3 m)
{
    const lua_Number r[9] = {m.cx.x, m.cx.y, m.cx.z, m.cy.x, m.cy.y, m.cy.z, m.cz.x, m.cz.y, m.cz.z};
    moo_pushmat3(L, r);
}

void moo_aabb_push(lua_State* L, fixAABB a)
{
    moo_pushaabb(L, a.lowerBound.x, a.lowerBound.y, a.lowerBound.z, a.upperBound.x, a.upperBound.y, a.upperBound.z);
}

namespace
{

#if MOO_OVERFLOW_ERRORS
// a checked build raises number's error where a per-component operation saturates (the vec3 rule)
bool mooMat3AddSaturates(fixMatrix3 a, fixMatrix3 b, bool sub)
{
    const fixed_t x[9] = {a.cx.x, a.cx.y, a.cx.z, a.cy.x, a.cy.y, a.cy.z, a.cz.x, a.cz.y, a.cz.z};
    const fixed_t y[9] = {b.cx.x, b.cx.y, b.cx.z, b.cy.x, b.cy.y, b.cy.z, b.cz.x, b.cz.y, b.cz.z};
    for (int i = 0; i < 9; ++i)
        if (sub ? moo_numsubsaturates(x[i], y[i]) : moo_numaddsaturates(x[i], y[i]))
            return true;
    return false;
}

bool mooMat3ScaleSaturates(fixed_t s, fixMatrix3 a)
{
    const fixed_t x[9] = {a.cx.x, a.cx.y, a.cx.z, a.cy.x, a.cy.y, a.cy.z, a.cz.x, a.cz.y, a.cz.z};
    for (int i = 0; i < 9; ++i)
        if (moo_nummulsaturates(s, x[i]))
            return true;
    return false;
}
#endif

// The result is stored before the GC check, as mooVec3SetResult does: a GC step can reallocate the stack.
void mooMat3SetResult(lua_State* L, StkId ra, fixMatrix3 m)
{
    setmat3value(L, ra, moo_mat3_new(L, m));
    luaC_checkGC(L);
}

void mooVec3Result(lua_State* L, StkId ra, fixVec3 v)
{
    setvec3value(L, ra, moo_vec3_new(L, v.x, v.y, v.z));
    luaC_checkGC(L);
}

} // namespace

// mat3 + mat3, mat3 - mat3 (fixAddMM, fixSubMM: per component, number's saturation and checked error), -mat3
// (fixNegateMat3), mat3 * mat3 (fixMulMM) and mat3 * vec3 (fixMulMV), sums of products, fixed's compound functions
// verbatim with no checked error (the 3b rule), and mat3 * number, number * mat3 (fixMulSM: per component, number's
// checked error). Every other operand of a layer 3c value is the ordinary arithmetic error, vec2's included: fixed
// has no vec2 arithmetic.
bool moo_narrow_arith(lua_State* L, int op, StkId ra, const TValue* rb, const TValue* rc)
{
    if (!ttismat3(rb) && !ttismat3(rc))
        return false;

    if (ttismat3(rb) && ttismat3(rc))
    {
        fixMatrix3 a = moo_mat3_payload(mat3value(rb));
        fixMatrix3 b = moo_mat3_payload(mat3value(rc));
        switch (op)
        {
        case TM_ADD:
#if MOO_OVERFLOW_ERRORS
            if (mooMat3AddSaturates(a, b, false))
                luaG_runerror(L, "number overflow in '+'");
#endif
            mooMat3SetResult(L, ra, fixAddMM(a, b));
            return true;
        case TM_SUB:
#if MOO_OVERFLOW_ERRORS
            if (mooMat3AddSaturates(a, b, true))
                luaG_runerror(L, "number overflow in '-'");
#endif
            mooMat3SetResult(L, ra, fixSubMM(a, b));
            return true;
        case TM_MUL:
            mooMat3SetResult(L, ra, fixMulMM(a, b));
            return true;
        case TM_UNM:
            mooMat3SetResult(L, ra, fixNegateMat3(a));
            return true;
        default:
            return false;
        }
    }
    if (op != TM_MUL)
        return false;
    if (ttismat3(rb) && ttisvec3(rc))
    {
        mooVec3Result(L, ra, fixMulMV(moo_mat3_payload(mat3value(rb)), moo_vec3_payload(vec3value(rc))));
        return true;
    }
    if ((ttismat3(rb) && ttisnumber(rc)) || (ttisnumber(rb) && ttismat3(rc)))
    {
        fixMatrix3 m = moo_mat3_payload(mat3value(ttismat3(rb) ? rb : rc));
        fixed_t s = nvalue(ttismat3(rb) ? rc : rb);
#if MOO_OVERFLOW_ERRORS
        if (mooMat3ScaleSaturates(s, m))
            luaG_runerror(L, "number overflow in '*'");
#endif
        mooMat3SetResult(L, ra, fixMulSM(s, m));
        return true;
    }
    return false;
}

// v.x, v.y (and X, Y) of a vec2; m.cx, m.cy, m.cz of a mat3, p.normal and p.offset of a plane, a.lower and a.upper
// of an aabb, each column, normal and bound a new vec3. Any other key is the index error: none has a metatable.
bool moo_narrow_index(lua_State* L, const TValue* t, const TValue* key, StkId val)
{
    if (!ttisstring(key))
        return false;
    const char* k = getstr(tsvalue(key));
    size_t len = tsvalue(key)->len;
    fixVec3 v;
    switch (ttype(t))
    {
    case LUA_TVEC2:
        if (len != 1)
            return false;
        if (k[0] == 'x' || k[0] == 'X')
        {
            setnvalue(val, vec2value(t)->x);
        }
        else if (k[0] == 'y' || k[0] == 'Y')
        {
            setnvalue(val, vec2value(t)->y);
        }
        else
            return false;
        return true;
    case LUA_TMAT3:
        if (len != 2 || k[0] != 'c')
            return false;
        if (k[1] == 'x')
            v = mat3value(t)->cx;
        else if (k[1] == 'y')
            v = mat3value(t)->cy;
        else if (k[1] == 'z')
            v = mat3value(t)->cz;
        else
            return false;
        break;
    case LUA_TPLANE:
        if (len == 6 && memcmp(k, "offset", 6) == 0)
        {
            setnvalue(val, planevalue(t)->offset);
            return true;
        }
        if (len != 6 || memcmp(k, "normal", 6) != 0)
            return false;
        v = planevalue(t)->normal;
        break;
    case LUA_TAABB:
        if (len == 5 && memcmp(k, "lower", 5) == 0)
            v = aabbvalue(t)->lower;
        else if (len == 5 && memcmp(k, "upper", 5) == 0)
            v = aabbvalue(t)->upper;
        else
            return false;
        break;
    default:
        return false;
    }
    // no GC step here: the debt is paid at the next check, as for t.p of a transform
    setvec3value(L, val, moo_vec3_new(L, v.x, v.y, v.z));
    return true;
}

namespace
{

// fixed asserts the rotation of a transform it bounds is normalized (fixTransformAABBCenterExtents)
fixTransform mooCheckRigid(lua_State* L, int idx)
{
    lua_Number raw[7];
    if (!moo_totransform(L, idx, raw))
        luaL_typeerrorL(L, idx, "transform");
    fixTransform t = FIX_LITERAL(fixTransform){{raw[0], raw[1], raw[2]}, {{raw[3], raw[4], raw[5]}, raw[6]}};
    luaL_argcheck(L, fixIsNormalizedQuat(t.q), idx, "rotation must be a unit quaternion");
    return t;
}

// --- vec2: fixed has no vec2 function, so the library is the constructor ---

int vec2_new(lua_State* L)
{
    moo_pushvec2(L, luaL_optnumber(L, 1, 0), luaL_optnumber(L, 2, 0));
    return 1;
}

const luaL_Reg vec2lib[] = {
    {"new", vec2_new},
    {NULL, NULL},
};

// --- mat3 ---

// mat3.new(cx, cy, cz): three vec3 columns; omitted columns are the identity's, so mat3.new() is the identity
int mat3_new(lua_State* L)
{
    fixMatrix3 m = fixMat3_identity;
    if (!lua_isnoneornil(L, 1))
        m.cx = moo_vec3_check(L, 1);
    if (!lua_isnoneornil(L, 2))
        m.cy = moo_vec3_check(L, 2);
    if (!lua_isnoneornil(L, 3))
        m.cz = moo_vec3_check(L, 3);
    moo_mat3_push(L, m);
    return 1;
}

int mat3_transpose(lua_State* L)
{
    moo_mat3_push(L, fixTranspose(moo_mat3_check(L, 1)));
    return 1;
}

// a singular matrix gives fixed's answer for it (the zero matrix)
int mat3_inverse(lua_State* L)
{
    moo_mat3_push(L, fixInvertMatrix(moo_mat3_check(L, 1)));
    return 1;
}

int mat3_inversewide(lua_State* L)
{
    moo_mat3_push(L, fixInvertMatrixWide(moo_mat3_check(L, 1)));
    return 1;
}

int mat3_inverset(lua_State* L)
{
    moo_mat3_push(L, fixInvertT(moo_mat3_check(L, 1)));
    return 1;
}

int mat3_solve(lua_State* L)
{
    fixMatrix3 m = moo_mat3_check(L, 1);
    moo_vec3_push(L, fixSolve3(m, moo_vec3_check(L, 2)));
    return 1;
}

int mat3_solvewide(lua_State* L)
{
    fixMatrix3 m = moo_mat3_check(L, 1);
    moo_vec3_push(L, fixSolve3Wide(m, moo_vec3_check(L, 2)));
    return 1;
}

int mat3_abs(lua_State* L)
{
    moo_mat3_push(L, fixAbsMatrix3(moo_mat3_check(L, 1)));
    return 1;
}

int mat3_det(lua_State* L)
{
    lua_pushnumber(L, fixDet(moo_mat3_check(L, 1)));
    return 1;
}

int mat3_fromquat(lua_State* L)
{
    moo_mat3_push(L, fixMakeMatrixFromQuat(moo_quat_check(L, 1)));
    return 1;
}

int mat3_isvalid(lua_State* L)
{
    lua_pushboolean(L, fixIsValidMatrix3(moo_mat3_check(L, 1)));
    return 1;
}

const luaL_Reg mat3lib[] = {
    {"new", mat3_new},
    {"transpose", mat3_transpose},
    {"inverse", mat3_inverse},
    {"inversewide", mat3_inversewide},
    {"inverset", mat3_inverset},
    {"solve", mat3_solve},
    {"solvewide", mat3_solvewide},
    {"abs", mat3_abs},
    {"det", mat3_det},
    {"fromquat", mat3_fromquat},
    {"isvalid", mat3_isvalid},
    {NULL, NULL},
};

// --- plane ---

// plane.new(normal, offset): fixIsValidPlane must hold, so the normal is a unit vector
int plane_new(lua_State* L)
{
    fixPlane p = FIX_LITERAL(fixPlane){moo_vec3_check(L, 1), luaL_optnumber(L, 2, 0)};
    luaL_argcheck(L, fixIsValidPlane(p), 1, "normal must be a unit vector");
    moo_pushplane(L, p.normal.x, p.normal.y, p.normal.z, p.offset);
    return 1;
}

int plane_isvalid(lua_State* L)
{
    lua_pushboolean(L, fixIsValidPlane(moo_plane_check(L, 1)));
    return 1;
}

const luaL_Reg planelib[] = {
    {"new", plane_new},
    {"isvalid", plane_isvalid},
    {NULL, NULL},
};

// --- aabb ---

// aabb.new(lower, upper): fixIsValidAABB must hold, so lower <= upper on every axis
int aabb_new(lua_State* L)
{
    fixAABB a = FIX_LITERAL(fixAABB){moo_vec3_check(L, 1), moo_vec3_check(L, 2)};
    luaL_argcheck(L, fixIsValidAABB(a), 2, "upper must not be below lower");
    moo_aabb_push(L, a);
    return 1;
}

// aabb.frompoints(points, radius): fixMakeAABB on an array of vec3, expanded by radius (default 0); fixed asserts the
// array is not empty. The points are copied into a userdata, an allocation of a size the program chose.
int aabb_frompoints(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    fixed_t radius = luaL_optnumber(L, 2, 0);
    int count = lua_objlen(L, 1);
    luaL_argcheck(L, count > 0, 1, "no points");
    fixVec3* points = static_cast<fixVec3*>(lua_newuserdata(L, sizeof(fixVec3) * size_t(count)));
    for (int i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, 1, i);
        lua_Number raw[3];
        if (!moo_tovec3(L, -1, raw))
            luaL_error(L, "invalid point #%d to 'frompoints' (vec3 expected, got %s)", i, luaL_typename(L, -1));
        lua_pop(L, 1);
        points[i - 1] = FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
    }
    moo_aabb_push(L, fixMakeAABB(points, count, radius));
    return 1;
}

int aabb_contains(lua_State* L)
{
    fixAABB a = moo_aabb_check(L, 1);
    lua_pushboolean(L, fixAABB_Contains(a, moo_aabb_check(L, 2)));
    return 1;
}

int aabb_area(lua_State* L)
{
    lua_pushnumber(L, fixAABB_Area(moo_aabb_check(L, 1)));
    return 1;
}

int aabb_center(lua_State* L)
{
    moo_vec3_push(L, fixAABB_Center(moo_aabb_check(L, 1)));
    return 1;
}

int aabb_extents(lua_State* L)
{
    moo_vec3_push(L, fixAABB_Extents(moo_aabb_check(L, 1)));
    return 1;
}

int aabb_union(lua_State* L)
{
    fixAABB a = moo_aabb_check(L, 1);
    moo_aabb_push(L, fixAABB_Union(a, moo_aabb_check(L, 2)));
    return 1;
}

int aabb_inflate(lua_State* L)
{
    fixAABB a = moo_aabb_check(L, 1);
    moo_aabb_push(L, fixAABB_Inflate(a, luaL_checknumber(L, 2)));
    return 1;
}

int aabb_overlaps(lua_State* L)
{
    fixAABB a = moo_aabb_check(L, 1);
    lua_pushboolean(L, fixAABB_Overlaps(a, moo_aabb_check(L, 2)));
    return 1;
}

// aabb.transform(t, a), fixed's argument order; the rotation must be a unit quaternion
int aabb_transform(lua_State* L)
{
    fixTransform t = mooCheckRigid(L, 1);
    moo_aabb_push(L, fixAABB_Transform(t, moo_aabb_check(L, 2)));
    return 1;
}

int aabb_fromcenterextents(lua_State* L)
{
    fixTransform t = mooCheckRigid(L, 1);
    fixVec3 c = moo_vec3_check(L, 2);
    moo_aabb_push(L, fixTransformAABBCenterExtents(t, c, moo_vec3_check(L, 3)));
    return 1;
}

// aabb.closestpoint(point, a), fixed's argument order
int aabb_closestpoint(lua_State* L)
{
    fixVec3 p = moo_vec3_check(L, 1);
    moo_vec3_push(L, fixClosestPointToAABB(p, moo_aabb_check(L, 2)));
    return 1;
}

int aabb_isvalid(lua_State* L)
{
    lua_pushboolean(L, fixIsValidAABB(moo_aabb_check(L, 1)));
    return 1;
}

const luaL_Reg aabblib[] = {
    {"new", aabb_new},
    {"frompoints", aabb_frompoints},
    {"contains", aabb_contains},
    {"area", aabb_area},
    {"center", aabb_center},
    {"extents", aabb_extents},
    {"union", aabb_union},
    {"inflate", aabb_inflate},
    {"overlaps", aabb_overlaps},
    {"transform", aabb_transform},
    {"fromcenterextents", aabb_fromcenterextents},
    {"closestpoint", aabb_closestpoint},
    {"isvalid", aabb_isvalid},
    {NULL, NULL},
};

// --- fix30 ---

// fix30.new(x): fix30FromFix, which saturates outside [-2, 2); a checked build raises instead
int fix30_new(lua_State* L)
{
    fixed_t x = luaL_checknumber(L, 1);
#if MOO_OVERFLOW_ERRORS
    if (x >= (fixed_t(2) << FIX_FRACTION_BITS) || x < -(fixed_t(2) << FIX_FRACTION_BITS))
        luaL_error(L, "fix30 out of range");
#endif
    moo_pushfix30(L, fix30FromFix(x).raw);
    return 1;
}

int fix30_tonumber(lua_State* L)
{
    lua_pushnumber(L, fixFromFix30(moo_fix30_check(L, 1)));
    return 1;
}

// fix30.normalizecomponent(x, length): fixNormalizeComponent30 on the raw Q48.16 bits, x * 2^30 / length rounded and
// clamped to [-1, 1]; fixed asserts length is not zero, and a negative length has no direction either
int fix30_normalizecomponent(lua_State* L)
{
    fixed_t x = luaL_checknumber(L, 1);
    fixed_t length = luaL_checknumber(L, 2);
    luaL_argcheck(L, length > 0, 2, "length must be positive");
    moo_pushfix30(L, fixNormalizeComponent30(x, uint64_t(length)));
    return 1;
}

// fix30.raw(x): the Q2.30 bits as an integer in [-2^31, 2^31), exactly; fix30.fromraw(n) is its inverse. The wire
// (serialize.moo's fixed(2,30)) carries these bits, which fix30.tonumber rounds away (layer 3c N4).
int fix30_raw(lua_State* L)
{
    lua_pushnumber(L, moo_numfromint(moo_fix30_check(L, 1).raw));
    return 1;
}

int fix30_fromraw(lua_State* L)
{
    fixed_t n = luaL_checknumber(L, 1);
    luaL_argcheck(L, (n & (MOO_NUMBER_ONE - 1)) == 0, 1, "number has no integer representation");
    int64_t raw = n / MOO_NUMBER_ONE; // exact: no fraction bits
    luaL_argcheck(L, raw >= INT32_MIN && raw <= INT32_MAX, 1, "raw must be in [-2^31, 2^31)");
    moo_pushfix30(L, int32_t(raw));
    return 1;
}

const luaL_Reg fix30lib[] = {
    {"new", fix30_new},
    {"tonumber", fix30_tonumber},
    {"normalizecomponent", fix30_normalizecomponent},
    {"raw", fix30_raw},
    {"fromraw", fix30_fromraw},
    {NULL, NULL},
};

} // namespace

int luaopen_vec2(lua_State* L)
{
    luaL_register(L, "vec2", vec2lib);
    return 1;
}

int luaopen_mat3(lua_State* L)
{
    luaL_register(L, "mat3", mat3lib);
    return 1;
}

int luaopen_plane(lua_State* L)
{
    luaL_register(L, "plane", planelib);
    return 1;
}

int luaopen_aabb(lua_State* L)
{
    luaL_register(L, "aabb", aabblib);
    return 1;
}

int luaopen_fix30(lua_State* L)
{
    luaL_register(L, "fix30", fix30lib);
    return 1;
}
