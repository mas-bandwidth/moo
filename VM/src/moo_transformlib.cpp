// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the transform library. Each function is fixed's function on the raw Q48.16 components, so the bits are
// fixed3d's b3Transform functions'.
#include "lualib.h"

#include "lobject.h"
#include "moo_quat.h"
#include "moo_transform.h"
#include "moo_vec3.h"

#include "fixed/fixed_vec.h"

namespace
{

fixTransform mooCheckTransform(lua_State* L, int idx)
{
    lua_Number raw[7];
    if (!moo_totransform(L, idx, raw))
        luaL_typeerrorL(L, idx, "transform");
    return FIX_LITERAL(fixTransform){{raw[0], raw[1], raw[2]}, {{raw[3], raw[4], raw[5]}, raw[6]}};
}

void mooPushTransform(lua_State* L, fixTransform t)
{
    moo_pushtransform(L, t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s);
}

// transform.new(p, q): omitted parts are the identity's, so transform.new() is the identity. q is taken as given,
// not normalized, as quat.new's components are.
int transform_new(lua_State* L)
{
    fixTransform t = fixTransform_identity;
    if (!lua_isnoneornil(L, 1))
        t.p = moo_vec3_check(L, 1);
    if (!lua_isnoneornil(L, 2))
        t.q = moo_quat_check(L, 2);
    mooPushTransform(L, t);
    return 1;
}

int transform_inverse(lua_State* L)
{
    mooPushTransform(L, fixInvertTransform(mooCheckTransform(L, 1)));
    return 1;
}

// the same as t * v
int transform_point(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    moo_vec3_push(L, fixTransformPoint(t, moo_vec3_check(L, 2)));
    return 1;
}

// a direction turns with the rotation and ignores the position
int transform_direction(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    moo_vec3_push(L, fixRotateVector(t.q, moo_vec3_check(L, 2)));
    return 1;
}

int transform_invpoint(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    moo_vec3_push(L, fixInvTransformPoint(t, moo_vec3_check(L, 2)));
    return 1;
}

int transform_invdirection(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    moo_vec3_push(L, fixInvRotateVector(t.q, moo_vec3_check(L, 2)));
    return 1;
}

// --- layer 3c: the rest of fixed's transform functions (docs/DESIGN.md "Layer 3c as built"). Each is one fixed call
// on the raw components, fixed's own result in release and checked builds alike (the layer 3b rule for compound
// functions). fixWorldTransform and fixPos are fixTransform and fixVec3 at this pin, so the world family takes and
// returns transforms and vec3s. ---

// inv(a) * b: the transform of frame b in frame a (fixInvMulTransforms)
int transform_invmul(lua_State* L)
{
    fixTransform a = mooCheckTransform(L, 1);
    fixTransform b = mooCheckTransform(L, 2);
    mooPushTransform(L, fixInvMulTransforms(a, b));
    return 1;
}

// fixMakeWorldTransform: the lossless promotion, the identity on these types
int transform_makeworld(lua_State* L)
{
    mooPushTransform(L, fixMakeWorldTransform(mooCheckTransform(L, 1)));
    return 1;
}

// a world transform composed with a local one (fixMulWorldTransforms)
int transform_mulworld(lua_State* L)
{
    fixTransform a = mooCheckTransform(L, 1);
    fixTransform b = mooCheckTransform(L, 2);
    mooPushTransform(L, fixMulWorldTransforms(a, b));
    return 1;
}

// the relative transform of world frame b in world frame a (fixInvMulWorldTransforms)
int transform_invmulworld(lua_State* L)
{
    fixTransform a = mooCheckTransform(L, 1);
    fixTransform b = mooCheckTransform(L, 2);
    mooPushTransform(L, fixInvMulWorldTransforms(a, b));
    return 1;
}

// a local point to a world position (fixTransformWorldPoint)
int transform_transformworldpoint(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    moo_vec3_push(L, fixTransformWorldPoint(t, moo_vec3_check(L, 2)));
    return 1;
}

// a world position to a local point (fixInvTransformWorldPoint)
int transform_invtransformworldpoint(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    moo_vec3_push(L, fixInvTransformWorldPoint(t, moo_vec3_check(L, 2)));
    return 1;
}

// a world transform shifted into the frame of a base position (fixToRelativeTransform)
int transform_torelative(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    mooPushTransform(L, fixToRelativeTransform(t, moo_vec3_check(L, 2)));
    return 1;
}

// fixIsValidTransform: a representable position and a representable, normalized rotation
int transform_isvalid(lua_State* L)
{
    lua_pushboolean(L, fixIsValidTransform(mooCheckTransform(L, 1)));
    return 1;
}

const luaL_Reg transformlib[] = {
    {"new", transform_new},
    {"inverse", transform_inverse},
    {"point", transform_point},
    {"direction", transform_direction},
    {"invpoint", transform_invpoint},
    {"invdirection", transform_invdirection},
    // layer 3c
    {"invmul", transform_invmul},
    {"makeworld", transform_makeworld},
    {"mulworld", transform_mulworld},
    {"invmulworld", transform_invmulworld},
    {"transformworldpoint", transform_transformworldpoint},
    {"invtransformworldpoint", transform_invtransformworldpoint},
    {"torelative", transform_torelative},
    {"isvalid", transform_isvalid},
    {NULL, NULL},
};

} // namespace

int luaopen_transform(lua_State* L)
{
    luaL_register(L, "transform", transformlib);
    return 1;
}
