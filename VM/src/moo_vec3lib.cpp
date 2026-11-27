// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec3 library. Each function is fixed's function on the raw Q48.16 components, so the bits are fixed3d's.
#include "lualib.h"

#include "lobject.h"
#include "moo_number.h"
#include "moo_vec3.h"

#include "fixed/fixed_vec.h"

fixVec3 moo_vec3_check(lua_State* L, int idx)
{
    lua_Number raw[3];
    if (!moo_tovec3(L, idx, raw))
        luaL_typeerrorL(L, idx, "vec3");
    return FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
}

void moo_vec3_push(lua_State* L, fixVec3 v)
{
    moo_pushvec3(L, v.x, v.y, v.z);
}

namespace
{

int vec3_new(lua_State* L)
{
    fixed_t x = luaL_optnumber(L, 1, 0);
    fixed_t y = luaL_optnumber(L, 2, 0);
    fixed_t z = luaL_optnumber(L, 3, 0);
    moo_pushvec3(L, x, y, z);
    return 1;
}

// exact 128-bit accumulation, one rounding
int vec3_dot(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    lua_pushnumber(L, fixDot(a, b));
    return 1;
}

int vec3_cross(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    moo_vec3_push(L, fixCross(a, b));
    return 1;
}

int vec3_length(lua_State* L)
{
    lua_pushnumber(L, fixLength(moo_vec3_check(L, 1)));
    return 1;
}

int vec3_normalize(lua_State* L)
{
    moo_vec3_push(L, fixNormalize(moo_vec3_check(L, 1)));
    return 1;
}

// --- layer 3c: the rest of fixed's vec3 functions (docs/DESIGN.md "Layer 3c as built"). Each is one fixed call on
// the raw components; where fixed asserts a precondition the argument is refused here by name, otherwise fixed's
// own result, saturation included, is the answer in release and checked builds alike (the layer 3b rule for
// compound functions). ---

int vec3_distance(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    lua_pushnumber(L, fixDistance(a, b));
    return 1;
}

int vec3_distancesquared(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    lua_pushnumber(L, fixDistanceSquared(a, b));
    return 1;
}

int vec3_lengthsquared(lua_State* L)
{
    lua_pushnumber(L, fixLengthSquared(moo_vec3_check(L, 1)));
    return 1;
}

// fixed asserts alpha is in [0, 1] (fixVecLerp), so anything else is refused here by name, as quat.nlerp does
int vec3_lerp(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    fixed_t alpha = luaL_checknumber(L, 3);
    luaL_argcheck(L, alpha >= 0 && alpha <= MOO_NUMBER_ONE, 3, "alpha must be in [0, 1]");
    moo_vec3_push(L, fixVecLerp(a, b, alpha));
    return 1;
}

int vec3_min(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    moo_vec3_push(L, fixVecMin(a, b));
    return 1;
}

int vec3_max(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    moo_vec3_push(L, fixVecMax(a, b));
    return 1;
}

int vec3_clamp(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 lower = moo_vec3_check(L, 2);
    fixVec3 upper = moo_vec3_check(L, 3);
    moo_vec3_push(L, fixVecClamp(a, lower, upper));
    return 1;
}

int vec3_abs(lua_State* L)
{
    moo_vec3_push(L, fixVecAbs(moo_vec3_check(L, 1)));
    return 1;
}

// component-wise -1 or 1 (1 for a zero component), fixed's convention
int vec3_sign(lua_State* L)
{
    moo_vec3_push(L, fixSign(moo_vec3_check(L, 1)));
    return 1;
}

// a unit vector perpendicular to the argument (fixed's choice of which)
int vec3_perp(lua_State* L)
{
    moo_vec3_push(L, fixPerp(moo_vec3_check(L, 1)));
    return 1;
}

// a + s * b
int vec3_muladd(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixed_t s = luaL_checknumber(L, 2);
    fixVec3 b = moo_vec3_check(L, 3);
    moo_vec3_push(L, fixMulAdd(a, s, b));
    return 1;
}

// a - s * b
int vec3_mulsub(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixed_t s = luaL_checknumber(L, 2);
    fixVec3 b = moo_vec3_check(L, 3);
    moo_vec3_push(L, fixMulSub(a, s, b));
    return 1;
}

// s * a + t * b
int vec3_blend2(lua_State* L)
{
    fixed_t s = luaL_checknumber(L, 1);
    fixVec3 a = moo_vec3_check(L, 2);
    fixed_t t = luaL_checknumber(L, 3);
    fixVec3 b = moo_vec3_check(L, 4);
    moo_vec3_push(L, fixBlend2(s, a, t, b));
    return 1;
}

// a scale kept at least FIX_MIN_SCALE from zero on each axis, sign preserved
int vec3_safescale(lua_State* L)
{
    moo_vec3_push(L, fixSafeScale(moo_vec3_check(L, 1)));
    return 1;
}

int vec3_isnormalized(lua_State* L)
{
    lua_pushboolean(L, fixIsNormalized(moo_vec3_check(L, 1)));
    return 1;
}

// the unit vector and the length, fixGetLengthAndNormalize's bits (its fast paths divide by the floored length)
int vec3_getlengthandnormalize(lua_State* L)
{
    fixed_t length = 0;
    fixVec3 unit = fixGetLengthAndNormalize(&length, moo_vec3_check(L, 1));
    moo_vec3_push(L, unit);
    lua_pushnumber(L, length);
    return 2;
}

// the unit vector and the length, fixNormalizeWithLength's bits (the lifted divisor fixNormalize uses)
int vec3_normalizewithlength(lua_State* L)
{
    fixed_t length = 0;
    fixVec3 unit = fixNormalizeWithLength(moo_vec3_check(L, 1), &length);
    moo_vec3_push(L, unit);
    lua_pushnumber(L, length);
    return 2;
}

// the position functions: fixPos is fixVec3, so a position is a vec3
int vec3_subpos(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    moo_vec3_push(L, fixSubPos(a, b));
    return 1;
}

int vec3_offsetpos(lua_State* L)
{
    fixVec3 p = moo_vec3_check(L, 1);
    fixVec3 d = moo_vec3_check(L, 2);
    moo_vec3_push(L, fixOffsetPos(p, d));
    return 1;
}

// fixLerpPosition asserts nothing about t, so any t is fixed's result (an extrapolation past the ends)
int vec3_lerpposition(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    fixed_t t = luaL_checknumber(L, 3);
    moo_vec3_push(L, fixLerpPosition(a, b, t));
    return 1;
}

// fixIsValidVec3: every component is in the number's domain (never INT64_MIN, which only a C host can push)
int vec3_isvalid(lua_State* L)
{
    lua_pushboolean(L, fixIsValidVec3(moo_vec3_check(L, 1)));
    return 1;
}

const luaL_Reg veclib[] = {
    {"new", vec3_new},
    {"create", vec3_new}, // moo: Luau's vector.create
    {"dot", vec3_dot},
    {"cross", vec3_cross},
    {"length", vec3_length},
    {"magnitude", vec3_length}, // moo: Luau's vector.magnitude
    {"normalize", vec3_normalize},
    // layer 3c
    {"distance", vec3_distance},
    {"distancesquared", vec3_distancesquared},
    {"lengthsquared", vec3_lengthsquared},
    {"lerp", vec3_lerp},
    {"min", vec3_min},
    {"max", vec3_max},
    {"clamp", vec3_clamp},
    {"abs", vec3_abs},
    {"sign", vec3_sign},
    {"perp", vec3_perp},
    {"muladd", vec3_muladd},
    {"mulsub", vec3_mulsub},
    {"blend2", vec3_blend2},
    {"safescale", vec3_safescale},
    {"isnormalized", vec3_isnormalized},
    {"getlengthandnormalize", vec3_getlengthandnormalize},
    {"normalizewithlength", vec3_normalizewithlength},
    {"subpos", vec3_subpos},
    {"offsetpos", vec3_offsetpos},
    {"lerpposition", vec3_lerpposition},
    {"isvalid", vec3_isvalid},
    {NULL, NULL},
};

} // namespace

int luaopen_vec3(lua_State* L)
{
    luaL_register(L, "vec3", veclib);
    return 1;
}
