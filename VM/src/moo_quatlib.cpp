// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the quat library. Each function is fixed's function on the raw Q48.16 components, so the bits are fixed3d's.
#include "lualib.h"

#include "lobject.h"
#include "moo_number.h"
#include "moo_quat.h"
#include "moo_vec3.h"

#include "fixed/fixed_vec.h"

fixQuat moo_quat_check(lua_State* L, int idx)
{
    lua_Number raw[4];
    if (!moo_toquat(L, idx, raw))
        luaL_typeerrorL(L, idx, "quat");
    return FIX_LITERAL(fixQuat){{raw[0], raw[1], raw[2]}, raw[3]};
}

void moo_quat_push(lua_State* L, fixQuat q)
{
    moo_pushquat(L, q.v.x, q.v.y, q.v.z, q.s);
}

namespace
{

// quat.new(x, y, z, w): w is fixed's scalar s. Omitted components are the identity's, so quat.new() is the identity.
int quat_new(lua_State* L)
{
    fixed_t x = luaL_optnumber(L, 1, 0);
    fixed_t y = luaL_optnumber(L, 2, 0);
    fixed_t z = luaL_optnumber(L, 3, 0);
    fixed_t w = luaL_optnumber(L, 4, MOO_NUMBER_ONE);
    moo_pushquat(L, x, y, z, w);
    return 1;
}

int quat_conjugate(lua_State* L)
{
    moo_quat_push(L, fixConjugate(moo_quat_check(L, 1)));
    return 1;
}

// fixed asserts alpha is in [0, 1] (fixVecLerp), so anything else is refused here by name
int quat_nlerp(lua_State* L)
{
    fixQuat a = moo_quat_check(L, 1);
    fixQuat b = moo_quat_check(L, 2);
    fixed_t alpha = luaL_checknumber(L, 3);
    luaL_argcheck(L, alpha >= 0 && alpha <= MOO_NUMBER_ONE, 3, "alpha must be in [0, 1]");
    moo_quat_push(L, fixNLerp(a, b, alpha));
    return 1;
}

// fixed has no slerp, so quat.slerp is a named error
int quat_slerp(lua_State* L)
{
    luaL_error(L, "quat.slerp waits for fixed's slerp");
    return 0;
}

// --- layer 3c: the rest of fixed's quat functions (docs/DESIGN.md "Layer 3c as built"). Each is one fixed call on
// the raw components; where fixed asserts a precondition the argument is refused here by name, otherwise fixed's
// own result is the answer in release and checked builds alike (the layer 3b rule for compound functions). ---

int quat_normalize(lua_State* L)
{
    moo_quat_push(L, fixNormalizeQuat(moo_quat_check(L, 1)));
    return 1;
}

// inv(a) * b, the relative rotation
int quat_invmul(lua_State* L)
{
    fixQuat a = moo_quat_check(L, 1);
    fixQuat b = moo_quat_check(L, 2);
    moo_quat_push(L, fixInvMulQuat(a, b));
    return 1;
}

// exact 128-bit accumulation of the four products, one rounding
int quat_dot(lua_State* L)
{
    fixQuat a = moo_quat_check(L, 1);
    fixQuat b = moo_quat_check(L, 2);
    lua_pushnumber(L, fixDotQuat(a, b));
    return 1;
}

// all four components negated: the same rotation with the other polarity
int quat_negate(lua_State* L)
{
    moo_quat_push(L, fixNegateQuat(moo_quat_check(L, 1)));
    return 1;
}

// fixed asserts the axis is normalized (fixMakeQuatFromAxisAngle), so anything else is refused here by name
int quat_fromaxisangle(lua_State* L)
{
    fixVec3 axis = moo_vec3_check(L, 1);
    fixed_t radians = luaL_checknumber(L, 2);
    luaL_argcheck(L, fixIsNormalized(axis), 1, "axis must be a unit vector");
    moo_quat_push(L, fixMakeQuatFromAxisAngle(axis, radians));
    return 1;
}

// the axis and the angle, two values; fixed assumes the quaternion is normalized and asserts nothing, so a
// non-unit quaternion gives fixed's result for it
int quat_axisangle(lua_State* L)
{
    fixed_t radians = 0;
    fixVec3 axis = fixGetAxisAngle(&radians, moo_quat_check(L, 1));
    moo_vec3_push(L, axis);
    lua_pushnumber(L, radians);
    return 2;
}

int quat_angle(lua_State* L)
{
    lua_pushnumber(L, fixGetQuatAngle(moo_quat_check(L, 1)));
    return 1;
}

// the twist about z, in [-pi, pi]
int quat_twistangle(lua_State* L)
{
    lua_pushnumber(L, fixGetTwistAngle(moo_quat_check(L, 1)));
    return 1;
}

// the swing away from z, in [0, pi]
int quat_swingangle(lua_State* L)
{
    lua_pushnumber(L, fixGetSwingAngle(moo_quat_check(L, 1)));
    return 1;
}

// fixed asserts both vectors are normalized (fixComputeQuatBetweenUnitVectors), so anything else is refused by name
int quat_betweenunitvectors(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    luaL_argcheck(L, fixIsNormalized(a), 1, "vector must be a unit vector");
    luaL_argcheck(L, fixIsNormalized(b), 2, "vector must be a unit vector");
    moo_quat_push(L, fixComputeQuatBetweenUnitVectors(a, b));
    return 1;
}

// fixIsValidQuat: representable and normalized (fixed counts normalization as part of validity)
int quat_isvalid(lua_State* L)
{
    lua_pushboolean(L, fixIsValidQuat(moo_quat_check(L, 1)));
    return 1;
}

int quat_isnormalized(lua_State* L)
{
    lua_pushboolean(L, fixIsNormalizedQuat(moo_quat_check(L, 1)));
    return 1;
}

const luaL_Reg quatlib[] = {
    {"new", quat_new},
    {"conjugate", quat_conjugate},
    {"nlerp", quat_nlerp},
    {"slerp", quat_slerp},
    // layer 3c
    {"normalize", quat_normalize},
    {"invmul", quat_invmul},
    {"dot", quat_dot},
    {"negate", quat_negate},
    {"fromaxisangle", quat_fromaxisangle},
    {"axisangle", quat_axisangle},
    {"angle", quat_angle},
    {"twistangle", quat_twistangle},
    {"swingangle", quat_swingangle},
    {"betweenunitvectors", quat_betweenunitvectors},
    {"isvalid", quat_isvalid},
    {"isnormalized", quat_isnormalized},
    {NULL, NULL},
};

} // namespace

int luaopen_quat(lua_State* L)
{
    luaL_register(L, "quat", quatlib);
    return 1;
}
